#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <ncurses.h>
#include <pwd.h>
#include <grp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef CTRL
#define CTRL(x) ((x) & 0x1f)
#endif

#define MAX_NAME     256
#define MIN_COLS     80
#define MIN_ROWS     16
#define TYPE_W       8
#define DATE_W      11
#define SIZE_W       8
#define NUM_TABS     4
#define MAX_CLIP     256
#define MAX_BOOKMARKS 64
#define MAX_ASSOC    64
#define MAX_BINDINGS 32
#define VIEW_MAX_SIZE (5 * 1024 * 1024)
#define MENU_MAX_VIS 16
#define THEME_FIELDS 22
#define CALC_SIZE_MAX_DEPTH 100
#define MAX_ARGS     64

#define COL_TYPE_MIN     52
#define COL_DATE_MIN     38
#define COL_SIZE_MIN     28

#define NEW_FIELD_SEL_INACTIVE_BG  1
#define NEW_FIELD_SEL_INACTIVE_FG  2
#define NEW_FIELD_MARKED_BG        4
#define NEW_FIELD_MARKED_FG        8

enum {
    PAIR_SEL_ACTIVE = 1, PAIR_SEL_INACTIVE, PAIR_DIR, PAIR_EXEC,
    PAIR_HEADER, PAIR_TITLE, PAIR_TAB_ACTIVE, PAIR_TAB_INACTIVE,
    PAIR_QUIT, PAIR_QUIT_SEL, PAIR_GRAY_BG, PAIR_MARKED,
    PAIR_BG, PAIR_DIALOG, PAIR_SHADOW, PAIR_STATUS, PAIR_MENU_SEL,
    PAIR_SCROLL
};

typedef enum { SORT_NAME, SORT_TYPE, SORT_DATE, SORT_SIZE, SORT_COUNT } SortField;

typedef enum {
    ACT_OPEN, ACT_OPEN_AS, ACT_EDIT, ACT_COPY, ACT_CUT, ACT_PASTE, ACT_DELETE,
    ACT_MKDIR, ACT_TOUCH, ACT_TEMPLATE, ACT_ARCHIVE, ACT_UNARCHIVE,
    ACT_RENAME, ACT_INFO, ACT_PIN, ACT_REFRESH, ACT_QUIT, ACT_COUNT
} Action;

static const char *ACTION_NAMES[ACT_COUNT] = {
    "Открыть", "Открыть как", "Правка",
    "Копировать", "Вырезать", "Вставить", "Удалить",
    "Создать папку", "Создать файл", "Создать из шаблона",
    "Архивировать", "Разархивировать",
    "Переименовать", "Свойства", "В избранное", "Обновить", "Выход"
};

typedef struct {
    char   name[MAX_NAME];
    int    is_dir;
    int    is_parent;
    int    selected;
    mode_t mode;
    off_t  size;
    time_t mtime;
} Entry;

typedef struct {
    Entry *items;
    size_t count;
} EntryList;

typedef struct {
    char      cwd[PATH_MAX];
    EntryList list;
    int       file_sel;
    int       file_top;
    SortField sort_field;
    int       sort_desc;
    char      pending_sel[MAX_NAME];
} FilePane;

typedef struct {
    FilePane panes[2];
    int active_pane;
    int split_mode;
    int preview_mode;
} TabState;

typedef struct {
    char ext[32];
    char cmd[PATH_MAX];
} Association;

typedef struct {
    char name[24];
    int  key;
} Binding;

typedef struct {
    TabState tabs[NUM_TABS];
    int active_tab;

    char clip_paths[MAX_CLIP][PATH_MAX];
    int  clip_count;
    int  clip_is_cut;

    int  search_active;
    char search_buf[256];
    int  search_len;

    char bookmarks[MAX_BOOKMARKS][PATH_MAX];
    int  bookmark_count;

    Association associations[MAX_ASSOC];
    int         assoc_count;

    Binding bindings[MAX_BINDINGS];
    int     binding_count;

    int g_x_name[2], g_x_type[2], g_x_date[2], g_x_size[2];
    int running;
    char last_action[PATH_MAX + 128];
} AppState;

typedef struct {
    off_t total;
    off_t done;
    WINDOW *win;
    const char *disp;
    int last_pct;
} CopyProgress;

typedef struct {
    char bg[8], fg[8];
    char sel_bg[8], sel_fg[8];
    char sel_inactive_bg[8], sel_inactive_fg[8];
    char marked_bg[8], marked_fg[8];
    char dir[8], exec[8];
    char header_bg[8], header_fg[8];
    char title_bg[8], title_fg[8];
    char tab_active_bg[8], tab_active_fg[8];
    char tab_inactive_bg[8], tab_inactive_fg[8];
    char status_bg[8], status_fg[8];
    char dialog_bg[8], dialog_fg[8];
} ThemeConfig;

typedef struct {
    SortField field;
    int       desc;
} SortCtx;

typedef struct {
    int show_type, show_date, show_size;
    int name_w;
    int x_name, x_type, x_date, x_size;
} PaneGeom;

static ThemeConfig g_theme;
static char g_theme_raw[4096] = "";
static int g_new_theme_seen = 0;

static AppState g_app;
#define CURRENT_TAB (g_app.tabs[g_app.active_tab])
#define ACTIVE_PANE (CURRENT_TAB.panes[CURRENT_TAB.active_pane])

static int A_PANEL        = 0;
static int A_SEL_ACTIVE   = A_REVERSE | A_BOLD;
static int A_SEL_INACTIVE = A_REVERSE;
static int A_DIR          = A_BOLD;
static int A_EXEC         = A_BOLD;
static int A_HEADER_GRAY  = A_REVERSE | A_BOLD;
static int A_TITLE        = A_REVERSE | A_BOLD;
static int A_TAB_ACTIVE   = A_REVERSE | A_BOLD;
static int A_TAB_INACTIVE = A_REVERSE;
static int A_MARKED       = A_REVERSE | A_BOLD;
static int A_BUTTON       = A_NORMAL;
static int A_BUTTON_SEL   = A_REVERSE | A_BOLD;
static int A_MENU_SEL     = 0;
static int A_MENU_NORMAL  = 0;
static int A_SCROLL_THUMB = A_REVERSE;
static int A_SCROLL_TRACK = 0;

static void init_attributes(void);
static int get_key(int raw_mode);
static void handle_key(int ch);
static void handle_mouse(MEVENT ev);
static void switch_tab(int idx);
static void draw(void);
static void load_config(void);
static void save_config(void);
static void add_bookmark(const char *path);
static int enter_dir(const char *path, FilePane *p);
static void open_bookmark(const char *path);
static void view_file(const char *path);
static int is_text_file(const char *name);
static int is_binary_content(const char *path);
static void action_info(FilePane *p);
static void launch_path(const char *name, const char *full);
static void run_settings_menu(void);

static char *theme_field_by_idx(int idx);
static const char *theme_name_by_idx(int idx);

static int run_menu(FilePane *p);
static int run_submenu(const char *title, const char **options, int count);
static void run_bookmarks_menu(void);
static void show_progress(int pct, const char *filename, WINDOW *win);
static void action_open(FilePane *p);
static void action_mkdir(FilePane *p);
static void action_touch(FilePane *p);
static void action_template(FilePane *p);
static void action_archive(FilePane *p);
static void action_unarchive(FilePane *p);
static void action_refresh(FilePane *p);
static void action_delete(FilePane *p);

static void sig_handler(int sig) {
    (void)sig;
    g_app.running = 0;
}

/* =========================================================================
 * УТИЛИТЫ
 * ========================================================================= */

static void copy_str(char *dst, size_t dstsz, const char *src) {
    if (dstsz == 0) return;
    size_t len = strlen(src);
    if (len >= dstsz) len = dstsz - 1;
    memmove(dst, src, len);
    dst[len] = '\0';
}

static int utf8_strlen(const char *s) {
    int n = 0;
    for (; *s; s++)
        if (((unsigned char)*s & 0xC0) != 0x80) n++;
    return n;
}

static size_t utf8_prefix_bytes(const char *s, int max_chars) {
    size_t bytes = 0;
    int chars = 0;
    while (s[bytes] && chars < max_chars) {
        unsigned char c = s[bytes];
        int step = 1;
        if ((c & 0xE0) == 0xC0) step = 2;
        else if ((c & 0xF0) == 0xE0) step = 3;
        else if ((c & 0xF8) == 0xF0) step = 4;
        bytes += step;
        chars++;
    }
    return bytes;
}

static void build_padded(char *out, size_t outsz, const char *in, int width, int right) {
    if (outsz == 0) return;
    int max_bytes = (int)outsz - 1;
    int chars = 0, bytes = 0;
    while (in[bytes] && chars < width) {
        unsigned char ch = in[bytes];
        int step = 1;
        if ((ch & 0xE0) == 0xC0)      step = 2;
        else if ((ch & 0xF0) == 0xE0) step = 3;
        else if ((ch & 0xF8) == 0xF0) step = 4;
        if (bytes + step > max_bytes) break;
        bytes += step;
        chars++;
    }
    int pad = width - chars;
    if (pad < 0) pad = 0;
    int o = 0;
    if (right) for (int i = 0; i < pad && o < max_bytes; i++) out[o++] = ' ';
    for (int i = 0; i < bytes && o < max_bytes; i++)         out[o++] = in[i];
    if (!right) for (int i = 0; i < pad && o < max_bytes; i++) out[o++] = ' ';
    out[o] = 0;
}

static void format_size(char *out, size_t outsz, off_t size) {
    const char *units[] = { "B", "K", "M", "G", "T" };
    double v = (double)size;
    int u = 0;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; u++; }
    if (u == 0) snprintf(out, outsz, "%lld%s", (long long)size, units[0]);
    else        snprintf(out, outsz, "%.1f%s", v, units[u]);
}

static void format_mode(char *out, size_t outsz, mode_t mode) {
    if (outsz < 10) { if (outsz) out[0] = '\0'; return; }
    out[0] = (mode & S_IRUSR) ? 'r' : '-';
    out[1] = (mode & S_IWUSR) ? 'w' : '-';
    out[2] = (mode & S_IXUSR) ? 'x' : '-';
    out[3] = (mode & S_IRGRP) ? 'r' : '-';
    out[4] = (mode & S_IWGRP) ? 'w' : '-';
    out[5] = (mode & S_IXGRP) ? 'x' : '-';
    out[6] = (mode & S_IROTH) ? 'r' : '-';
    out[7] = (mode & S_IWOTH) ? 'w' : '-';
    out[8] = (mode & S_IXOTH) ? 'x' : '-';
    out[9] = '\0';
}

static char file_icon(const char *name, int is_dir, mode_t mode) {
    if (is_dir)          return 'D';
    if (name[0] == '.')  return '.';
    if (mode & (S_IXUSR | S_IXGRP | S_IXOTH)) return 'X';

    const char *dot = strrchr(name, '.');
    if (!dot || dot == name) return '-';
    dot++;

    if (!strcasecmp(dot, "txt") || !strcasecmp(dot, "log")) return 'T';
    if (!strcasecmp(dot, "c") || !strcasecmp(dot, "h"))     return 'C';
    if (!strcasecmp(dot, "cpp") || !strcasecmp(dot, "cc") || !strcasecmp(dot, "hpp")) return 'C';
    if (!strcasecmp(dot, "py"))                              return 'P';
    if (!strcasecmp(dot, "rs"))                              return 'R';
    if (!strcasecmp(dot, "go"))                              return 'G';
    if (!strcasecmp(dot, "js") || !strcasecmp(dot, "ts"))    return 'J';
    if (!strcasecmp(dot, "html") || !strcasecmp(dot, "htm")) return 'W';
    if (!strcasecmp(dot, "css"))                             return 'S';
    if (!strcasecmp(dot, "md"))                              return 'M';
    if (!strcasecmp(dot, "json"))                            return '{';
    if (!strcasecmp(dot, "yaml") || !strcasecmp(dot, "yml") || !strcasecmp(dot, "toml")) return 'Y';
    if (!strcasecmp(dot, "xml"))                             return '<';
    if (!strcasecmp(dot, "sh") || !strcasecmp(dot, "bash") || !strcasecmp(dot, "zsh")) return '$';
    if (!strcasecmp(dot, "png") || !strcasecmp(dot, "jpg") || !strcasecmp(dot, "jpeg") ||
        !strcasecmp(dot, "gif") || !strcasecmp(dot, "bmp") || !strcasecmp(dot, "svg")) return 'I';
    if (!strcasecmp(dot, "mp3") || !strcasecmp(dot, "wav") || !strcasecmp(dot, "flac") || !strcasecmp(dot, "ogg")) return 'A';
    if (!strcasecmp(dot, "mp4") || !strcasecmp(dot, "mkv") || !strcasecmp(dot, "avi") || !strcasecmp(dot, "webm")) return 'V';
    if (!strcasecmp(dot, "zip") || !strcasecmp(dot, "tar") || !strcasecmp(dot, "gz") ||
        !strcasecmp(dot, "xz") || !strcasecmp(dot, "bz2") || !strcasecmp(dot, "7z")) return 'Z';
    if (!strcasecmp(dot, "pdf"))                             return 'F';
    return '-';
}

static const char *type_str(const char *name, int is_dir, mode_t mode) {
    if (is_dir) return "папка";
    int is_exec = (mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0;
    const char *dot = strrchr(name, '.');
    if (!dot || dot == name) return is_exec ? "исп" : "файл";
    dot++;
    if (!strcasecmp(dot, "txt") || !strcasecmp(dot, "log") || !strcasecmp(dot, "md") || !strcasecmp(dot, "rst")) return "текст";
    if (!strcasecmp(dot, "c") || !strcasecmp(dot, "h") || !strcasecmp(dot, "cpp") || !strcasecmp(dot, "cc") ||
        !strcasecmp(dot, "hpp") || !strcasecmp(dot, "py") || !strcasecmp(dot, "rs") || !strcasecmp(dot, "go") ||
        !strcasecmp(dot, "js") || !strcasecmp(dot, "ts") || !strcasecmp(dot, "java") || !strcasecmp(dot, "rb")) return "код";
    if (!strcasecmp(dot, "json") || !strcasecmp(dot, "yaml") || !strcasecmp(dot, "yml") ||
        !strcasecmp(dot, "toml") || !strcasecmp(dot, "ini") || !strcasecmp(dot, "xml")) return "конфиг";
    if (!strcasecmp(dot, "sh") || !strcasecmp(dot, "bash") || !strcasecmp(dot, "zsh") || !strcasecmp(dot, "fish")) return "скрипт";
    if (!strcasecmp(dot, "html") || !strcasecmp(dot, "htm") || !strcasecmp(dot, "css") || !strcasecmp(dot, "scss")) return "веб";
    if (!strcasecmp(dot, "png") || !strcasecmp(dot, "jpg") || !strcasecmp(dot, "jpeg") ||
        !strcasecmp(dot, "gif") || !strcasecmp(dot, "bmp") || !strcasecmp(dot, "svg") || !strcasecmp(dot, "webp")) return "изобр";
    if (!strcasecmp(dot, "mp3") || !strcasecmp(dot, "wav") || !strcasecmp(dot, "flac") || !strcasecmp(dot, "ogg")) return "аудио";
    if (!strcasecmp(dot, "mp4") || !strcasecmp(dot, "mkv") || !strcasecmp(dot, "avi") || !strcasecmp(dot, "webm")) return "видео";
    if (!strcasecmp(dot, "zip") || !strcasecmp(dot, "tar") || !strcasecmp(dot, "gz") ||
        !strcasecmp(dot, "xz") || !strcasecmp(dot, "bz2") || !strcasecmp(dot, "7z")) return "архив";
    if (!strcasecmp(dot, "pdf")) return "PDF";
    return "файл";
}

static int make_path(char *out, size_t outsz, const char *dir, const char *name) {
    size_t dl = strlen(dir), nl = strlen(name);
    if (dl + 1 + nl + 1 > outsz) return -1;
    memcpy(out, dir, dl);
    out[dl] = '/';
    memcpy(out + dl + 1, name, nl);
    out[dl + 1 + nl] = '\0';
    return 0;
}

/* =========================================================================
 * БЕЗОПАСНЫЙ ЗАПУСК ПРОЦЕССОВ (fork + execvp, без /bin/sh)
 * ========================================================================= */

static int tokenize_cmd(const char *cmd, char **argv, int max_args) {
    if (!cmd || !*cmd || max_args < 2) return 0;
    char *work = strdup(cmd);
    if (!work) return 0;
    int n = 0;
    char *save = NULL;
    char *tok = strtok_r(work, " \t\n\r", &save);
    while (tok && n < max_args - 1) {
        argv[n] = strdup(tok);
        if (!argv[n]) break;
        n++;
        tok = strtok_r(NULL, " \t\n\r", &save);
    }
    free(work);
    argv[n] = NULL;
    return n;
}

static void free_argv(char **argv, int n) {
    for (int i = 0; i < n; i++) free(argv[i]);
}

static void spawn_detached_argv(char *const argv[]) {
    if (!argv || !argv[0]) return;
    pid_t pid = fork();
    if (pid < 0) return;
    if (pid == 0) {
        pid_t pid2 = fork();
        if (pid2 < 0) _exit(1);
        if (pid2 > 0) _exit(0);
        setsid();
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > 2) close(devnull);
        }
        execvp(argv[0], argv);
        _exit(127);
    }
    waitpid(pid, NULL, 0);
}

static int run_foreground(char *const argv[], const char *workdir) {
    if (!argv || !argv[0]) return -1;
    def_prog_mode(); endwin();
    pid_t pid = fork();
    if (pid < 0) { reset_prog_mode(); refresh(); return -1; }
    if (pid == 0) {
        if (workdir && *workdir && chdir(workdir) != 0) _exit(127);
        execvp(argv[0], argv);
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    reset_prog_mode(); refresh();
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return 0;
    return -1;
}

static int get_mime_type(const char *path, char *out, size_t outsz) {
    if (!path || !out || outsz < 2) return -1;
    out[0] = '\0';
    int fds[2];
    if (pipe(fds) != 0) return -1;
    pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return -1; }
    if (pid == 0) {
        close(fds[0]);
        if (dup2(fds[1], STDOUT_FILENO) < 0) _exit(127);
        if (fds[1] > 2) close(fds[1]);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }
        execlp("file", "file", "-b", "--mime-type", path, (char *)NULL);
        _exit(127);
    }
    close(fds[1]);
    ssize_t total = 0;
    while (total < (ssize_t)outsz - 1) {
        ssize_t n = read(fds[0], out + total, outsz - 1 - total);
        if (n <= 0) break;
        total += n;
    }
    close(fds[0]);
    waitpid(pid, NULL, 0);
    if (total <= 0) return -1;
    out[total] = '\0';
    while (total > 0 && (out[total - 1] == '\n' || out[total - 1] == '\r')) out[--total] = '\0';
    return 0;
}

static int fs_delete_recursive(const char *path) {
    if (!path || !*path) return -1;
    struct stat st;
    if (lstat(path, &st) != 0) return -1;
    if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
        DIR *d = opendir(path);
        if (!d) return -1;
        struct dirent *de;
        size_t plen = strlen(path);
        int rc = 0;
        while ((de = readdir(d))) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
            size_t nlen = strlen(de->d_name);
            char *child = malloc(plen + 1 + nlen + 1);
            if (!child) { rc = -1; break; }
            memcpy(child, path, plen);
            child[plen] = '/';
            memcpy(child + plen + 1, de->d_name, nlen + 1);
            if (fs_delete_recursive(child) != 0) rc = -1;
            free(child);
        }
        closedir(d);
        if (rc != 0) return -1;
        return rmdir(path);
    }
    return unlink(path);
}

static void fs_delete(const char *target) {
    fs_delete_recursive(target);
}

/* =========================================================================
 * КОНФИГУРАЦИЯ
 * ========================================================================= */

static void default_config(void) {
    snprintf(g_theme.bg, 8, "#0000AA");
    snprintf(g_theme.fg, 8, "#FFFFFF");
    snprintf(g_theme.sel_bg, 8, "#00AAAA");
    snprintf(g_theme.sel_fg, 8, "#FFFFFF");
    snprintf(g_theme.sel_inactive_bg, 8, "#808080");
    snprintf(g_theme.sel_inactive_fg, 8, "#00AAAA");
    snprintf(g_theme.marked_bg, 8, "#000000");
    snprintf(g_theme.marked_fg, 8, "#FFFF00");
    snprintf(g_theme.dir, 8, "#FFFF55");
    snprintf(g_theme.exec, 8, "#55FF55");
    snprintf(g_theme.header_bg, 8, "#FFFFFF");
    snprintf(g_theme.header_fg, 8, "#000000");
    snprintf(g_theme.title_bg, 8, "#FFFFFF");
    snprintf(g_theme.title_fg, 8, "#000000");
    snprintf(g_theme.tab_active_bg, 8, "#55FF55");
    snprintf(g_theme.tab_active_fg, 8, "#000000");
    snprintf(g_theme.tab_inactive_bg, 8, "#000000");
    snprintf(g_theme.tab_inactive_fg, 8, "#FFFFFF");
    snprintf(g_theme.status_bg, 8, "#FFFFFF");
    snprintf(g_theme.status_fg, 8, "#000000");
    snprintf(g_theme.dialog_bg, 8, "#FFFFFF");
    snprintf(g_theme.dialog_fg, 8, "#000000");
}

static void default_bindings(void) {
    static const struct { const char *name; int key; } defs[] = {
        {"open", '\n'}, {"open_l", 'l'},
        {"menu", 'a'}, {"bookmarks", 'p'}, {"settings", 'o'},
        {"help", 'i'}, {"split", 'm'}, {"preview", 'v'},
        {"sort_next", 's'}, {"sort_reverse", 'S'},
        {"select", ' '}, {"search", '/'},
        {"refresh", 'r'}, {"refresh_full", 'R'},
        {"quit", 'q'}, {"up", 'k'}, {"down", 'j'},
        {"home", 'g'}, {"end", 'G'}, {"parent", 'h'},
    };
    g_app.binding_count = 0;
    for (size_t i = 0; i < sizeof(defs) / sizeof(defs[0]); i++) {
        copy_str(g_app.bindings[g_app.binding_count].name, 24, defs[i].name);
        g_app.bindings[g_app.binding_count].key = defs[i].key;
        g_app.binding_count++;
    }
}

static int color_to_256(const char *hex) {
    if (!hex || hex[0] != '#') return COLOR_WHITE;
    if (strlen(hex) < 7) return COLOR_WHITE;
    for (int i = 1; i < 7; i++) {
        if (!isxdigit((unsigned char)hex[i])) return COLOR_WHITE;
    }
    int r = 0, g = 0, b = 0;
    if (sscanf(hex + 1, "%02x%02x%02x", &r, &g, &b) != 3) return COLOR_WHITE;
    int cr = (r * 5 + 127) / 255;
    int cg = (g * 5 + 127) / 255;
    int cb = (b * 5 + 127) / 255;
    return 16 + (36 * cr) + (6 * cg) + cb;
}

static char *theme_field_by_idx(int idx) {
    switch (idx) {
    case 0:  return g_theme.bg;
    case 1:  return g_theme.fg;
    case 2:  return g_theme.sel_bg;
    case 3:  return g_theme.sel_fg;
    case 4:  return g_theme.sel_inactive_bg;
    case 5:  return g_theme.sel_inactive_fg;
    case 6:  return g_theme.marked_bg;
    case 7:  return g_theme.marked_fg;
    case 8:  return g_theme.dir;
    case 9:  return g_theme.exec;
    case 10: return g_theme.header_bg;
    case 11: return g_theme.header_fg;
    case 12: return g_theme.title_bg;
    case 13: return g_theme.title_fg;
    case 14: return g_theme.tab_active_bg;
    case 15: return g_theme.tab_active_fg;
    case 16: return g_theme.tab_inactive_bg;
    case 17: return g_theme.tab_inactive_fg;
    case 18: return g_theme.status_bg;
    case 19: return g_theme.status_fg;
    case 20: return g_theme.dialog_bg;
    case 21: return g_theme.dialog_fg;
    }
    return NULL;
}

static const char *theme_name_by_idx(int idx) {
    static const char *names[THEME_FIELDS] = {
        "bg", "fg", "sel_bg", "sel_fg", "sel_inactive_bg", "sel_inactive_fg",
        "marked_bg", "marked_fg", "dir", "exec",
        "header_bg", "header_fg", "title_bg", "title_fg",
        "tab_active_bg", "tab_active_fg", "tab_inactive_bg", "tab_inactive_fg",
        "status_bg", "status_fg", "dialog_bg", "dialog_fg"
    };
    if (idx < 0 || idx >= THEME_FIELDS) return "";
    return names[idx];
}

static void load_config(void) {
    default_config();
    default_bindings();
    g_theme_raw[0] = '\0';
    g_new_theme_seen = 0;

    char path[PATH_MAX];
    const char *home = getenv("HOME");
    if (!home) return;
    snprintf(path, sizeof path, "%s/.vibeyfilerc", home);

    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[PATH_MAX + 64];
    int in_theme = 0, in_fav = 0, in_hist = 0, in_exec = 0, in_binds = 0;
    int first_line = 1;

    while (fgets(line, sizeof line, f)) {
        char *start = line;
        if (first_line) {
            first_line = 0;
            if ((unsigned char)start[0] == 0xEF && (unsigned char)start[1] == 0xBB && (unsigned char)start[2] == 0xBF) {
                start += 3;
            }
        }
        char *p = start;
        while (*p == ' ' || *p == '\t') p++;

        if (*p == '[') {
            in_theme = !strncmp(p, "[THEME]", 7);
            in_fav   = !strncmp(p, "[FAVORITE]", 10);
            in_hist  = !strncmp(p, "[HISTORY]", 9);
            in_exec  = !strncmp(p, "[EXECUTION]", 11);
            in_binds = !strncmp(p, "[KEYBINDS]", 10);
            if (in_theme) {
                size_t add = strlen(start);
                if (add > sizeof(g_theme_raw) - 1) add = sizeof(g_theme_raw) - 1;
                memcpy(g_theme_raw, start, add);
                g_theme_raw[add] = '\0';
            }
            continue;
        }

        if (in_theme) {
            size_t cur = strlen(g_theme_raw);
            size_t add = strlen(start);
            if (cur + add + 1 < sizeof(g_theme_raw)) {
                memcpy(g_theme_raw + cur, start, add);
                g_theme_raw[cur + add] = '\0';
            }
        }

        size_t len = strlen(start);
        while (len > 0 && (start[len-1] == '\n' || start[len-1] == '\r')) start[--len] = '\0';
        if (len == 0) continue;

        if (in_theme) {
            char *eq = strchr(start, '=');
            if (!eq) continue;
            *eq = '\0';
            char *key = start, *val = eq + 1;
            while (*key == ' ' || *key == '\t') key++;
            while (*val == ' ' || *val == '\t') val++;

            char *kend = key + strlen(key);
            while (kend > key && (kend[-1] == ' ' || kend[-1] == '\t')) {
                *--kend = '\0';
            }

            if      (!strcmp(key, "bg"))               snprintf(g_theme.bg, 8, "%s", val);
            else if (!strcmp(key, "fg"))               snprintf(g_theme.fg, 8, "%s", val);
            else if (!strcmp(key, "sel_bg"))           snprintf(g_theme.sel_bg, 8, "%s", val);
            else if (!strcmp(key, "sel_fg"))           snprintf(g_theme.sel_fg, 8, "%s", val);
            else if (!strcmp(key, "sel_inactive_bg")) { snprintf(g_theme.sel_inactive_bg, 8, "%s", val); g_new_theme_seen |= NEW_FIELD_SEL_INACTIVE_BG; }
            else if (!strcmp(key, "sel_inactive_fg")) { snprintf(g_theme.sel_inactive_fg, 8, "%s", val); g_new_theme_seen |= NEW_FIELD_SEL_INACTIVE_FG; }
            else if (!strcmp(key, "marked_bg"))       { snprintf(g_theme.marked_bg, 8, "%s", val); g_new_theme_seen |= NEW_FIELD_MARKED_BG; }
            else if (!strcmp(key, "marked_fg"))       { snprintf(g_theme.marked_fg, 8, "%s", val); g_new_theme_seen |= NEW_FIELD_MARKED_FG; }
            else if (!strcmp(key, "dir"))              snprintf(g_theme.dir, 8, "%s", val);
            else if (!strcmp(key, "exec"))             snprintf(g_theme.exec, 8, "%s", val);
            else if (!strcmp(key, "header_bg"))        snprintf(g_theme.header_bg, 8, "%s", val);
            else if (!strcmp(key, "header_fg"))        snprintf(g_theme.header_fg, 8, "%s", val);
            else if (!strcmp(key, "title_bg"))         snprintf(g_theme.title_bg, 8, "%s", val);
            else if (!strcmp(key, "title_fg"))         snprintf(g_theme.title_fg, 8, "%s", val);
            else if (!strcmp(key, "tab_active_bg"))    snprintf(g_theme.tab_active_bg, 8, "%s", val);
            else if (!strcmp(key, "tab_active_fg"))    snprintf(g_theme.tab_active_fg, 8, "%s", val);
            else if (!strcmp(key, "tab_inactive_bg"))  snprintf(g_theme.tab_inactive_bg, 8, "%s", val);
            else if (!strcmp(key, "tab_inactive_fg"))  snprintf(g_theme.tab_inactive_fg, 8, "%s", val);
            else if (!strcmp(key, "status_bg"))        snprintf(g_theme.status_bg, 8, "%s", val);
            else if (!strcmp(key, "status_fg"))        snprintf(g_theme.status_fg, 8, "%s", val);
            else if (!strcmp(key, "dialog_bg"))        snprintf(g_theme.dialog_bg, 8, "%s", val);
            else if (!strcmp(key, "dialog_fg"))        snprintf(g_theme.dialog_fg, 8, "%s", val);
        } else if (in_fav) {
            if (*start) add_bookmark(start);
        } else if (in_exec) {
            char *eq = strchr(start, '=');
            if (eq) {
                *eq = '\0';
                char *key = start;
                char *val = eq + 1;
                while (*key == ' ' || *key == '\t') key++;
                while (*val == ' ' || *val == '\t') val++;
                char *kend = key + strlen(key);
                while (kend > key && (kend[-1] == ' ' || kend[-1] == '\t')) {
                    *--kend = '\0';
                }
                char *k = key;
                while (*k == '.') k++;
                if (*k && g_app.assoc_count < MAX_ASSOC) {
                    copy_str(g_app.associations[g_app.assoc_count].ext, 32, k);
                    copy_str(g_app.associations[g_app.assoc_count].cmd, PATH_MAX, val);
                    g_app.assoc_count++;
                }
            }
        } else if (in_binds) {
            char *eq = strchr(start, '=');
            if (eq) {
                *eq = '\0';
                char *key = start, *val = eq + 1;
                while (*key == ' ' || *key == '\t') key++;
                while (*val == ' ' || *val == '\t') val++;
                char *kend = key + strlen(key);
                while (kend > key && (kend[-1] == ' ' || kend[-1] == '\t')) *--kend = '\0';

                int nk = 0;
                if (val[0] == '\\' && val[1] == 'n') nk = '\n';
                else if (val[0] == '\\' && val[1] == 't') nk = '\t';
                else if (val[0] == '\\' && val[1] == 's') nk = ' ';
                else if (val[0] == '\\' && val[1] == 'e') nk = 27;
                else if ((unsigned char)val[0] >= 32 && (unsigned char)val[0] < 127) nk = (unsigned char)val[0];

                if (nk) {
                    for (int i = 0; i < g_app.binding_count; i++) {
                        if (strcmp(g_app.bindings[i].name, key) == 0) {
                            g_app.bindings[i].key = nk;
                            break;
                        }
                    }
                }
            }
        } else if (in_hist) {
            char *eq = strchr(start, '=');
            if (eq) {
                *eq = '\0';
                char *key = start;
                char *val = eq + 1;
                while (*key == ' ' || *key == '\t') key++;
                while (*val == ' ' || *val == '\t') val++;

                char *kend = key + strlen(key);
                while (kend > key && (kend[-1] == ' ' || kend[-1] == '\t')) {
                    *--kend = '\0';
                }

                if (!strcmp(key, "active_tab")) {
                    int idx = atoi(val);
                    if (idx >= 0 && idx < NUM_TABS) g_app.active_tab = idx;
                } else {
                    int idx = -1;
                    char suffix[32] = {0};
                    if (sscanf(key, "tab%d_%31s", &idx, suffix) == 2) {
                        if (idx >= 0 && idx < NUM_TABS) {
                            if (!strcmp(suffix, "split")) g_app.tabs[idx].split_mode = atoi(val);
                            else if (!strcmp(suffix, "preview")) g_app.tabs[idx].preview_mode = atoi(val);
                            else if (!strcmp(suffix, "pane")) g_app.tabs[idx].active_pane = atoi(val);
                            else if (!strcmp(suffix, "sel"))
                                copy_str(g_app.tabs[idx].panes[0].pending_sel, MAX_NAME, val);
                            else if (!strcmp(suffix, "p0_sel"))
                                copy_str(g_app.tabs[idx].panes[0].pending_sel, MAX_NAME, val);
                            else if (!strcmp(suffix, "p1_sel"))
                                copy_str(g_app.tabs[idx].panes[1].pending_sel, MAX_NAME, val);
                            else if (!strcmp(suffix, "p0"))
                                copy_str(g_app.tabs[idx].panes[0].cwd, PATH_MAX, val);
                            else if (!strcmp(suffix, "p1"))
                                copy_str(g_app.tabs[idx].panes[1].cwd, PATH_MAX, val);
                        }
                    } else if (sscanf(key, "tab%d", &idx) == 1) {
                        if (idx >= 0 && idx < NUM_TABS) {
                            copy_str(g_app.tabs[idx].panes[0].cwd, PATH_MAX, val);
                        }
                    }
                }
            }
        }
    }
    fclose(f);

    if (g_app.bookmark_count == 0) {
        add_bookmark(home);
        add_bookmark("/etc");
        add_bookmark("/usr");
        char cfg[PATH_MAX];
        snprintf(cfg, sizeof cfg, "%s/.config", home);
        add_bookmark(cfg);
    }
}

static void save_config(void) {
    char path[PATH_MAX];
    const char *home = getenv("HOME");
    if (!home) return;
    snprintf(path, sizeof path, "%s/.vibeyfilerc", home);

    FILE *f = fopen(path, "w");
    if (!f) return;

    if (g_theme_raw[0]) {
        size_t len = strlen(g_theme_raw);
        while (len > 0 && (g_theme_raw[len-1] == '\n' || g_theme_raw[len-1] == '\r')) {
            g_theme_raw[--len] = '\0';
        }
        fputs(g_theme_raw, f);
        fputc('\n', f);

        if (!(g_new_theme_seen & NEW_FIELD_SEL_INACTIVE_BG)) fprintf(f, "sel_inactive_bg = %s\n", g_theme.sel_inactive_bg);
        if (!(g_new_theme_seen & NEW_FIELD_SEL_INACTIVE_FG)) fprintf(f, "sel_inactive_fg = %s\n", g_theme.sel_inactive_fg);
        if (!(g_new_theme_seen & NEW_FIELD_MARKED_BG))       fprintf(f, "marked_bg = %s\n", g_theme.marked_bg);
        if (!(g_new_theme_seen & NEW_FIELD_MARKED_FG))       fprintf(f, "marked_fg = %s\n", g_theme.marked_fg);
    } else {
        fprintf(f, "[THEME]\n");
        fprintf(f, "bg = %s\n", g_theme.bg);
        fprintf(f, "fg = %s\n", g_theme.fg);
        fprintf(f, "sel_bg = %s\n", g_theme.sel_bg);
        fprintf(f, "sel_fg = %s\n", g_theme.sel_fg);
        fprintf(f, "sel_inactive_bg = %s\n", g_theme.sel_inactive_bg);
        fprintf(f, "sel_inactive_fg = %s\n", g_theme.sel_inactive_fg);
        fprintf(f, "marked_bg = %s\n", g_theme.marked_bg);
        fprintf(f, "marked_fg = %s\n", g_theme.marked_fg);
        fprintf(f, "dir = %s\n", g_theme.dir);
        fprintf(f, "exec = %s\n", g_theme.exec);
        fprintf(f, "header_bg = %s\n", g_theme.header_bg);
        fprintf(f, "header_fg = %s\n", g_theme.header_fg);
        fprintf(f, "title_bg = %s\n", g_theme.title_bg);
        fprintf(f, "title_fg = %s\n", g_theme.title_fg);
        fprintf(f, "tab_active_bg = %s\n", g_theme.tab_active_bg);
        fprintf(f, "tab_active_fg = %s\n", g_theme.tab_active_fg);
        fprintf(f, "tab_inactive_bg = %s\n", g_theme.tab_inactive_bg);
        fprintf(f, "tab_inactive_fg = %s\n", g_theme.tab_inactive_fg);
        fprintf(f, "status_bg = %s\n", g_theme.status_bg);
        fprintf(f, "status_fg = %s\n", g_theme.status_fg);
        fprintf(f, "dialog_bg = %s\n", g_theme.dialog_bg);
        fprintf(f, "dialog_fg = %s\n", g_theme.dialog_fg);
    }

    fprintf(f, "\n[FAVORITE]\n");
    for (int i = 0; i < g_app.bookmark_count; i++) {
        fprintf(f, "%s\n", g_app.bookmarks[i]);
    }

    fprintf(f, "\n[EXECUTION]\n");
    for (int i = 0; i < g_app.assoc_count; i++) {
        fprintf(f, ".%s = %s\n", g_app.associations[i].ext, g_app.associations[i].cmd);
    }

    fprintf(f, "\n[KEYBINDS]\n");
    for (int i = 0; i < g_app.binding_count; i++) {
        int k = g_app.bindings[i].key;
        char kbuf[8];
        if (k == '\n') snprintf(kbuf, sizeof kbuf, "\\n");
        else if (k == '\t') snprintf(kbuf, sizeof kbuf, "\\t");
        else if (k == ' ') snprintf(kbuf, sizeof kbuf, "\\s");
        else if (k == 27) snprintf(kbuf, sizeof kbuf, "\\e");
        else if (k >= 32 && k < 127) { kbuf[0] = (char)k; kbuf[1] = '\0'; }
        else snprintf(kbuf, sizeof kbuf, "?");
        fprintf(f, "%s = %s\n", g_app.bindings[i].name, kbuf);
    }

    fprintf(f, "\n[HISTORY]\n");
    for (int i = 0; i < NUM_TABS; i++) {
        FilePane *p0 = &g_app.tabs[i].panes[0];
        FilePane *p1 = &g_app.tabs[i].panes[1];
        if (p0->cwd[0]) {
            fprintf(f, "tab%d = %s\n", i, p0->cwd);
            if (p0->list.count > 0 && p0->file_sel > 0 && p0->file_sel < (int)p0->list.count) {
                fprintf(f, "tab%d_sel = %s\n", i, p0->list.items[p0->file_sel].name);
            }
        }
        if (p1->cwd[0]) {
            fprintf(f, "tab%d_p1 = %s\n", i, p1->cwd);
            if (p1->list.count > 0 && p1->file_sel > 0 && p1->file_sel < (int)p1->list.count) {
                fprintf(f, "tab%d_p1_sel = %s\n", i, p1->list.items[p1->file_sel].name);
            }
        }
        if (g_app.tabs[i].split_mode) {
            fprintf(f, "tab%d_split = 1\n", i);
        }
        if (g_app.tabs[i].preview_mode) {
            fprintf(f, "tab%d_preview = 1\n", i);
        }
        if (g_app.tabs[i].active_pane) {
            fprintf(f, "tab%d_pane = 1\n", i);
        }
    }
    fprintf(f, "active_tab = %d\n", g_app.active_tab);
    fclose(f);
}

static void add_bookmark(const char *path) {
    if (!path || !*path) return;
    for (int i = 0; i < g_app.bookmark_count; i++) {
        if (strcmp(g_app.bookmarks[i], path) == 0) return;
    }
    if (g_app.bookmark_count < MAX_BOOKMARKS) {
        copy_str(g_app.bookmarks[g_app.bookmark_count], PATH_MAX, path);
        g_app.bookmark_count++;
    }
}

/* =========================================================================
 * ФАЙЛОВАЯ СИСТЕМА
 * ========================================================================= */

/* GNU qsort_r компаратор: (const void *, const void *, void *).
 * Сигнатура строго соответствует glibc. */
static int cmp_entry_ctx(const void *a, const void *b, void *ctx) {
    const Entry *ea = a, *eb = b;
    const SortCtx *c = (const SortCtx *)ctx;
    if (ea->is_parent != eb->is_parent) return eb->is_parent - ea->is_parent;
    if (ea->is_dir != eb->is_dir) return eb->is_dir - ea->is_dir;

    int res = 0;
    if (c->field == SORT_NAME) {
        res = strcmp(ea->name, eb->name);
    } else if (c->field == SORT_TYPE) {
        const char *ta = type_str(ea->name, ea->is_dir, ea->mode);
        const char *tb = type_str(eb->name, eb->is_dir, eb->mode);
        res = strcmp(ta, tb);
        if (res == 0) res = strcmp(ea->name, eb->name);
    } else if (c->field == SORT_DATE) {
        if (ea->mtime < eb->mtime) res = -1;
        else if (ea->mtime > eb->mtime) res = 1;
        else res = strcmp(ea->name, eb->name);
    } else if (c->field == SORT_SIZE) {
        if (ea->size < eb->size) res = -1;
        else if (ea->size > eb->size) res = 1;
        else res = strcmp(ea->name, eb->name);
    }

    if (c->desc) res = -res;
    return res;
}

/* Явный каст функции-компаратора: страховка на случай, если заголовок
 * qsort_r где-то объявлен через несовместимый typedef и -Werror ругается
 * 'incompatible pointer type'. На glibc каст тождественен. */
typedef int (*GnuQsortRComparator)(const void *, const void *, void *);

static void do_sort(FilePane *p) {
    char keep_name[MAX_NAME] = {0};
    if (p->list.count > 1 && p->file_sel > 0 && p->file_sel < (int)p->list.count) {
        copy_str(keep_name, sizeof keep_name, p->list.items[p->file_sel].name);
    }

    if (p->list.count > 1) {
        SortCtx ctx = { p->sort_field, p->sort_desc };
        qsort_r(p->list.items + 1, p->list.count - 1, sizeof(*p->list.items),
                (GnuQsortRComparator)cmp_entry_ctx, &ctx);
    }

    if (keep_name[0]) {
        for (size_t i = 1; i < p->list.count; i++) {
            if (strcmp(p->list.items[i].name, keep_name) == 0) {
                p->file_sel = (int)i;
                break;
            }
        }
    }
}

static void free_list(EntryList *l) {
    free(l->items);
    l->items = NULL;
    l->count = 0;
}

static int load_dir(FilePane *p) {
    free_list(&p->list);
    DIR *d = opendir(p->cwd);
    if (!d) return -1;

    size_t cap = 64, n = 0;
    Entry *items = malloc(cap * sizeof(*items));
    if (!items) { closedir(d); return -1; }

    memset(&items[0], 0, sizeof(items[0]));
    snprintf(items[0].name, MAX_NAME, "..");
    items[0].is_dir    = 1;
    items[0].is_parent = 1;
    items[0].mode      = S_IFDIR | 0755;
    n = 1;

    struct dirent *de;
    while ((de = readdir(d))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        if (n == cap) {
            size_t ncap = cap * 2;
            Entry *tmp = realloc(items, ncap * sizeof(*tmp));
            if (!tmp) { free(items); closedir(d); return -1; }
            items = tmp; cap = ncap;
        }
        memset(&items[n], 0, sizeof(items[n]));

        size_t dnl = strlen(de->d_name);
        if (dnl >= sizeof(items[n].name)) dnl = sizeof(items[n].name) - 1;
        memcpy(items[n].name, de->d_name, dnl);
        items[n].name[dnl] = '\0';

        char full[PATH_MAX];
        if (make_path(full, sizeof full, p->cwd, de->d_name) == 0) {
            struct stat st;
            if (stat(full, &st) == 0) {
                items[n].is_dir = S_ISDIR(st.st_mode);
                items[n].mode   = st.st_mode;
                items[n].size   = st.st_size;
                items[n].mtime  = st.st_mtime;
            }
        }
        n++;
    }
    closedir(d);

    p->list.items = items;
    p->list.count = n;

    if (p->file_sel >= (int)n) p->file_sel = n ? (int)n - 1 : 0;
    if (p->file_sel < 0)       p->file_sel = 0;

    if (n > 1) {
        SortCtx ctx = { p->sort_field, p->sort_desc };
        qsort_r(items + 1, n - 1, sizeof(*items),
                (GnuQsortRComparator)cmp_entry_ctx, &ctx);
    }

    return 0;
}

static int enter_dir(const char *path, FilePane *p) {
    if (chdir(path) != 0)         return -1;
    if (!getcwd(p->cwd, sizeof p->cwd)) return -1;
    if (load_dir(p) != 0)      return -1;
    p->file_sel = (p->list.count > 1) ? 1 : 0;
    p->file_top = 0;
    return 0;
}

static off_t calc_size(const char *path, int depth) {
    if (depth > CALC_SIZE_MAX_DEPTH) return 0;
    struct stat st;
    if (lstat(path, &st) != 0) return 0;
    if (S_ISLNK(st.st_mode)) return 0;
    if (!S_ISDIR(st.st_mode)) return st.st_size;
    DIR *d = opendir(path);
    if (!d) return 0;
    struct dirent *de;
    off_t total = 0;
    size_t plen = strlen(path);
    while ((de = readdir(d))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        size_t nlen = strlen(de->d_name);
        char *child = malloc(plen + 1 + nlen + 1);
        if (!child) continue;
        memcpy(child, path, plen);
        child[plen] = '/';
        memcpy(child + plen + 1, de->d_name, nlen + 1);
        total += calc_size(child, depth + 1);
        free(child);
    }
    closedir(d);
    return total;
}

static int do_copy_file(const char *src, const char *dst, CopyProgress *cp) {
    int in = open(src, O_RDONLY);
    if (in < 0) return -1;
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) { close(in); return -1; }
    char buf[65536];
    ssize_t n;
    while ((n = read(in, buf, sizeof(buf))) > 0) {
        ssize_t written = 0;
        while (written < n) {
            ssize_t w = write(out, buf + written, n - written);
            if (w < 0 || w == 0) {
                close(in); close(out); unlink(dst); return -1;
            }
            written += w;
        }
        cp->done += n;
        if (cp->total > 0) {
            int pct = (int)((cp->done * 100) / cp->total);
            if (pct > 100) pct = 100;
            if (pct != cp->last_pct) {
                show_progress(pct, cp->disp, cp->win);
                cp->last_pct = pct;
            }
        }
    }
    if (n < 0) { close(in); close(out); unlink(dst); return -1; }
    close(in); close(out);
    return 0;
}

static int do_copy_recursive(const char *src, const char *dst, CopyProgress *cp) {
    struct stat st;
    if (lstat(src, &st) != 0) return -1;
    if (S_ISLNK(st.st_mode)) {
        char target[PATH_MAX];
        ssize_t len = readlink(src, target, sizeof(target) - 1);
        if (len < 0) return -1;
        target[len] = '\0';
        if (symlink(target, dst) != 0) return -1;
        return 0;
    }
    if (S_ISDIR(st.st_mode)) {
        if (mkdir(dst, 0755) != 0 && errno != EEXIST) return -1;
        DIR *d = opendir(src);
        if (!d) return -1;
        struct dirent *de;
        int rc = 0;
        size_t plen = strlen(src);
        while ((de = readdir(d))) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
            size_t nlen = strlen(de->d_name);
            char *s_child = malloc(plen + 1 + nlen + 1);
            if (!s_child) { rc = -1; break; }
            memcpy(s_child, src, plen);
            s_child[plen] = '/';
            memcpy(s_child + plen + 1, de->d_name, nlen + 1);

            char d_child[PATH_MAX];
            if (make_path(d_child, sizeof d_child, dst, de->d_name) != 0) {
                free(s_child);
                rc = -1;
                break;
            }
            if (do_copy_recursive(s_child, d_child, cp) != 0) {
                free(s_child);
                rc = -1;
                break;
            }
            free(s_child);
        }
        closedir(d);
        return rc;
    } else if (S_ISREG(st.st_mode)) {
        return do_copy_file(src, dst, cp);
    }
    return -1;
}

static int get_selected_or_current(FilePane *p, char paths[][PATH_MAX], int max_paths) {
    int count = 0;
    for (size_t i = 0; i < p->list.count && count < max_paths; i++) {
        if (p->list.items[i].selected) {
            if (make_path(paths[count], PATH_MAX, p->cwd, p->list.items[i].name) == 0) count++;
        }
    }
    if (count == 0 && p->list.count > 0 && p->file_sel > 0 && p->file_sel < (int)p->list.count) {
        if (make_path(paths[count], PATH_MAX, p->cwd, p->list.items[p->file_sel].name) == 0) count = 1;
    }
    return count;
}

/* =========================================================================
 * ДИАЛОГИ И ИНТЕРФЕЙС
 * ========================================================================= */

static void init_attributes(void) {
    if (has_colors()) {
        start_color();
        use_default_colors();

        int bg = color_to_256(g_theme.bg);
        int fg = color_to_256(g_theme.fg);
        int dir = color_to_256(g_theme.dir);
        int exec = color_to_256(g_theme.exec);
        int header_bg = color_to_256(g_theme.header_bg);
        int header_fg = color_to_256(g_theme.header_fg);
        int title_bg = color_to_256(g_theme.title_bg);
        int title_fg = color_to_256(g_theme.title_fg);
        int tab_active_bg = color_to_256(g_theme.tab_active_bg);
        int tab_active_fg = color_to_256(g_theme.tab_active_fg);
        int tab_inactive_bg = color_to_256(g_theme.tab_inactive_bg);
        int tab_inactive_fg = color_to_256(g_theme.tab_inactive_fg);
        int status_bg = color_to_256(g_theme.status_bg);
        int status_fg = color_to_256(g_theme.status_fg);
        int dialog_bg = color_to_256(g_theme.dialog_bg);
        int dialog_fg = color_to_256(g_theme.dialog_fg);

        init_pair(PAIR_BG,           fg, bg);
        init_pair(PAIR_SEL_ACTIVE,   color_to_256(g_theme.sel_fg), color_to_256(g_theme.sel_bg));
        init_pair(PAIR_SEL_INACTIVE, color_to_256(g_theme.sel_inactive_fg), color_to_256(g_theme.sel_inactive_bg));
        init_pair(PAIR_DIR,          dir, bg);
        init_pair(PAIR_EXEC,         exec, bg);
        init_pair(PAIR_HEADER,       header_fg, header_bg);
        init_pair(PAIR_TITLE,        title_fg, title_bg);
        init_pair(PAIR_TAB_ACTIVE,   tab_active_fg, tab_active_bg);
        init_pair(PAIR_TAB_INACTIVE, tab_inactive_fg, tab_inactive_bg);
        init_pair(PAIR_STATUS,       status_fg, status_bg);
        init_pair(PAIR_DIALOG,       dialog_fg, dialog_bg);
        init_pair(PAIR_SHADOW,       COLOR_BLACK, COLOR_BLACK);
        init_pair(PAIR_MARKED,       color_to_256(g_theme.marked_fg), color_to_256(g_theme.marked_bg));
        init_pair(PAIR_MENU_SEL,     color_to_256(g_theme.sel_fg), color_to_256(g_theme.sel_bg));
        init_pair(PAIR_SCROLL,       color_to_256(g_theme.sel_fg), color_to_256(g_theme.sel_bg));

        A_PANEL        = COLOR_PAIR(PAIR_BG);
        A_SEL_ACTIVE   = COLOR_PAIR(PAIR_SEL_ACTIVE) | A_BOLD;
        A_SEL_INACTIVE = COLOR_PAIR(PAIR_SEL_INACTIVE);
        A_DIR          = COLOR_PAIR(PAIR_DIR) | A_BOLD;
        A_EXEC         = COLOR_PAIR(PAIR_EXEC);
        A_HEADER_GRAY  = COLOR_PAIR(PAIR_HEADER) | A_BOLD;
        A_TITLE        = COLOR_PAIR(PAIR_TITLE) | A_BOLD;
        A_TAB_ACTIVE   = COLOR_PAIR(PAIR_TAB_ACTIVE) | A_BOLD;
        A_TAB_INACTIVE = COLOR_PAIR(PAIR_TAB_INACTIVE);
        A_MARKED       = COLOR_PAIR(PAIR_MARKED) | A_BOLD;
        A_BUTTON       = A_NORMAL;
        A_BUTTON_SEL   = A_REVERSE | A_BOLD;
        A_MENU_SEL     = COLOR_PAIR(PAIR_MENU_SEL) | A_BOLD;
        A_MENU_NORMAL  = COLOR_PAIR(PAIR_DIALOG);
        A_SCROLL_THUMB = COLOR_PAIR(PAIR_SCROLL) | A_BOLD;
        A_SCROLL_TRACK = A_PANEL;
    } else {
        A_MENU_SEL = A_REVERSE | A_BOLD;
        A_MENU_NORMAL = A_NORMAL;
        A_SCROLL_THUMB = A_REVERSE;
        A_SCROLL_TRACK = A_NORMAL;
    }
}

static void apply_shadow(int y, int x, int h, int w) {
    attron(COLOR_PAIR(PAIR_SHADOW));
    for (int i = 1; i <= h; i++) mvhline(y + i, x + w, ' ', 2);
    for (int i = 1; i <= w; i++) mvvline(y + h, x + i, ' ', 1);
    attroff(COLOR_PAIR(PAIR_SHADOW));
    refresh();
}

static int get_key(int raw_mode) {
    int ch = getch();
    if (ch == 0xD0 || ch == 0xD1) {
        int ch2 = getch();
        if (raw_mode) {
            return ((unsigned char)ch << 8) | (unsigned char)ch2;
        }
        if (ch == 0xD0) {
            switch (ch2) {
                case 0xB9: return 'q'; case 0xBE: return 'j'; case 0xB4: return 'l';
                case 0xBB: return 'k'; case 0xBF: return 'g'; case 0xBA: return 'r';
                case 0xBC: return 'v'; case 0xBD: return 'y'; case 0x99: return 'Q';
                case 0x9E: return 'J'; case 0x94: return 'L'; case 0x9B: return 'K';
                case 0x9F: return 'G'; case 0x9A: return 'R'; case 0x9C: return 'V';
                case 0x9D: return 'Y'; case 0xA0: return 'H'; case 0xB7: return 'p';
                case 0x97: return 'P';
            }
        } else if (ch == 0xD1) {
            switch (ch2) {
                case 0x84: return 'a'; case 0x8B: return 's'; case 0x8C: return 'm';
                case 0x80: return 'h'; case 0x88: return 'i'; case 0x82: return 'n';
                case 0xA4: return 'A'; case 0xAB: return 'S'; case 0xAC: return 'M';
                case 0xA8: return 'I'; case 0xA2: return 'N';
                case 0x9E: return 'O';
            }
        }
        return 0;
    }
    return ch;
}

/* =========================================================================
 * ГЕОМЕТРИЯ ПАНЕЛИ (единая точка расчёта колонок)
 * ========================================================================= */

static void compute_geometry(int x, int w, int show_type, int show_date, int show_size, PaneGeom *g) {
    int name_w = w - 6 - 1;
    if (show_size) name_w -= (SIZE_W + 1);
    if (show_date) name_w -= (DATE_W + 1);
    if (show_type) name_w -= (TYPE_W + 1);
    if (name_w < 4) name_w = 4;

    g->show_type = show_type;
    g->show_date = show_date;
    g->show_size = show_size;
    g->name_w = name_w;
    g->x_name = x + 6;
    g->x_type = show_type ? g->x_name + name_w + 1 : -1;
    g->x_date = show_date ? (show_type ? g->x_type + TYPE_W + 1 : g->x_name + name_w + 1) : -1;
    g->x_size = show_size ? x + w - SIZE_W : -1;
}

static void message_box(const char *title, const char *msg) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    char *lines_buf = strdup(msg);
    if (!lines_buf) return;

    char **lines = NULL;
    int line_count = 0, line_cap = 0;
    char *saveptr = NULL;
    char *line = strtok_r(lines_buf, "\n", &saveptr);
    while (line) {
        if (line_count == line_cap) {
            int ncap = line_cap ? line_cap * 2 : 16;
            char **tmp = realloc(lines, ncap * sizeof(char*));
            if (!tmp) {
                free(lines);
                free(lines_buf);
                return;
            }
            lines = tmp;
            line_cap = ncap;
        }
        lines[line_count++] = line;
        line = strtok_r(NULL, "\n", &saveptr);
    }

    int max_chars = utf8_strlen(title);
    for (int i = 0; i < line_count; i++) {
        int l = utf8_strlen(lines[i]);
        if (l > max_chars) max_chars = l;
    }
    int w = max_chars + 6;
    if (w > cols - 4) w = cols - 4;
    if (w < 40) w = 40;

    int h = line_count + 4;
    if (h > rows - 4) h = rows - 4;
    if (h < 6) h = 6;

    int y = (rows - h) / 2, x = (cols - w) / 2;

    int vis = h - 4;
    if (vis < 1) vis = 1;
    int max_scroll = line_count - vis;
    if (max_scroll < 0) max_scroll = 0;
    int scroll = 0;

    apply_shadow(y, x, h, w);
    WINDOW *win = newwin(h, w, y, x);
    wbkgd(win, COLOR_PAIR(PAIR_DIALOG));
    keypad(win, TRUE);

    int running = 1;
    while (running) {
        werase(win);
        box(win, 0, 0);
        {
            size_t tbytes = utf8_prefix_bytes(title, w - 4);
            mvwaddnstr(win, 0, 2, title, tbytes);
        }

        for (int i = 0; i < vis && scroll + i < line_count; i++) {
            const char *ln = lines[scroll + i];
            size_t lbytes = utf8_prefix_bytes(ln, w - 4);
            mvwaddnstr(win, 1 + i, 2, ln, lbytes);
        }

        if (max_scroll > 0) {
            int bar_x = w - 2;
            int bar_h = h - 3;
            int thumb_h = (bar_h * vis) / line_count;
            if (thumb_h < 1) thumb_h = 1;
            int thumb_pos = (bar_h * scroll) / max_scroll;
            if (thumb_pos + thumb_h > bar_h) thumb_pos = bar_h - thumb_h;
            for (int i = 0; i < bar_h; i++) {
                if (i >= thumb_pos && i < thumb_pos + thumb_h) {
                    wattron(win, A_REVERSE);
                    mvwaddch(win, 1 + i, bar_x, ' ');
                    wattroff(win, A_REVERSE);
                }
            }
        }

        char footer[160];
        if (max_scroll > 0) {
            snprintf(footer, sizeof footer, "j/k, PgUp/PgDn - прокрутка | q/Esc/Enter - закрыть [%d/%d]",
                     scroll + 1, max_scroll + 1);
        } else {
            snprintf(footer, sizeof footer, "Нажмите любую клавишу...");
        }
        {
            size_t fbytes = utf8_prefix_bytes(footer, w - 4);
            mvwaddnstr(win, h - 2, 2, footer, fbytes);
        }
        wrefresh(win);

        int ch = wgetch(win);
        if (ch == 'q' || ch == 'Q' || ch == 27 || ch == '\n' || ch == KEY_ENTER) {
            running = 0;
        } else if (ch == KEY_DOWN || ch == 'j') {
            if (scroll < max_scroll) scroll++;
        } else if (ch == KEY_UP || ch == 'k') {
            if (scroll > 0) scroll--;
        } else if (ch == KEY_NPAGE || ch == CTRL('d')) {
            scroll += vis;
            if (scroll > max_scroll) scroll = max_scroll;
        } else if (ch == KEY_PPAGE || ch == CTRL('u')) {
            scroll -= vis;
            if (scroll < 0) scroll = 0;
        } else if (ch == KEY_HOME) {
            scroll = 0;
        } else if (ch == KEY_END) {
            scroll = max_scroll;
        } else if (max_scroll == 0) {
            running = 0;
        }
    }

    delwin(win);
    free(lines);
    free(lines_buf);
    touchwin(stdscr);
    refresh();
}

/* =========================================================================
 * ВСТРОЕННЫЙ ПРОСМОТРЩИК
 * ========================================================================= */

static int is_text_file(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot || dot == name) return 0;
    dot++;
    if (!strcasecmp(dot, "txt") || !strcasecmp(dot, "log") || !strcasecmp(dot, "md") || !strcasecmp(dot, "rst")) return 1;
    if (!strcasecmp(dot, "c") || !strcasecmp(dot, "h") || !strcasecmp(dot, "cpp") || !strcasecmp(dot, "cc") ||
        !strcasecmp(dot, "hpp") || !strcasecmp(dot, "py") || !strcasecmp(dot, "rs") || !strcasecmp(dot, "go") ||
        !strcasecmp(dot, "js") || !strcasecmp(dot, "ts") || !strcasecmp(dot, "java") || !strcasecmp(dot, "rb")) return 1;
    if (!strcasecmp(dot, "json") || !strcasecmp(dot, "yaml") || !strcasecmp(dot, "yml") ||
        !strcasecmp(dot, "toml") || !strcasecmp(dot, "ini") || !strcasecmp(dot, "xml")) return 1;
    if (!strcasecmp(dot, "sh") || !strcasecmp(dot, "bash") || !strcasecmp(dot, "zsh") || !strcasecmp(dot, "fish")) return 1;
    return 0;
}

static int is_binary_content(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 1;
    char buf[512];
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    for (size_t i = 0; i < n; i++) {
        if (buf[i] == 0) return 1;
    }
    return 0;
}

static void view_file(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        message_box("Ошибка", "Не удалось открыть файл");
        return;
    }
    if (st.st_size > VIEW_MAX_SIZE) {
        message_box("Слишком большой файл", "Файл больше 5 МБ. Используйте внешний просмотрщик.");
        return;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        message_box("Ошибка", "Не удалось открыть файл");
        return;
    }

    char **lines = NULL;
    int line_count = 0, line_cap = 0;
    char *line = NULL;
    size_t line_len = 0;
    ssize_t n;

    while ((n = getline(&line, &line_len, f)) != -1) {
        while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = '\0';
        if (line_count == line_cap) {
            int ncap = line_cap ? line_cap * 2 : 256;
            char **tmp = realloc(lines, ncap * sizeof(char*));
            if (!tmp) {
                free(line);
                fclose(f);
                for (int i = 0; i < line_count; i++) free(lines[i]);
                free(lines);
                message_box("Ошибка", "Недостаточно памяти для просмотра файла");
                return;
            }
            lines = tmp;
            line_cap = ncap;
        }
        /* OOM-safe: strdup в отдельную переменную; слот lines[line_count]
         * не заполняется, пока копия не создана успешно. На OOM цикл
         * очистки обходит только валидные [0, line_count). */
        char *dup = strdup(line);
        if (!dup) {
            free(line);
            fclose(f);
            for (int i = 0; i < line_count; i++) free(lines[i]);
            free(lines);
            message_box("Ошибка", "Недостаточно памяти для просмотра файла");
            return;
        }
        lines[line_count] = dup;
        line_count++;
    }
    free(line);
    fclose(f);

    if (line_count == 0) {
        free(lines);
        message_box("Просмотр", "Файл пуст");
        return;
    }

    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    WINDOW *win = newwin(rows, cols, 0, 0);
    wbkgd(win, COLOR_PAIR(PAIR_BG));
    keypad(win, TRUE);

    int scroll = 0;
    int vis = rows - 2;
    int running = 1;
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;

    while (running) {
        werase(win);

        wattron(win, A_TITLE);
        mvwhline(win, 0, 0, ' ', cols);
        char title[PATH_MAX + 32];
        snprintf(title, sizeof title, " %s ", base);
        mvwaddnstr(win, 0, 1, title, cols - 4);
        wattroff(win, A_TITLE);

        for (int i = 0; i < vis && scroll + i < line_count; i++) {
            const char *ln = lines[scroll + i];
            int safe_bytes = 0, chars = 0;
            while (ln[safe_bytes] && chars < cols - 2) {
                unsigned char ch = ln[safe_bytes];
                int step = 1;
                if ((ch & 0xE0) == 0xC0) step = 2;
                else if ((ch & 0xF0) == 0xE0) step = 3;
                else if ((ch & 0xF8) == 0xF0) step = 4;
                if (safe_bytes + step > (int)strlen(ln)) break;
                safe_bytes += step;
                chars++;
            }
            mvwaddnstr(win, 1 + i, 1, ln, safe_bytes);
        }

        wattron(win, COLOR_PAIR(PAIR_STATUS));
        mvwhline(win, rows - 1, 0, ' ', cols);
        char status[512];
        snprintf(status, sizeof status, " %s | %d/%d | q/Esc - выход", base, scroll + 1, line_count);
        mvwaddnstr(win, rows - 1, 1, status, cols - 2);
        wattroff(win, COLOR_PAIR(PAIR_STATUS));

        wrefresh(win);

        int ch = wgetch(win);
        if (ch == 'q' || ch == 'Q' || ch == 27) {
            running = 0;
        } else if (ch == 'j' || ch == KEY_DOWN) {
            if (scroll < line_count - 1) scroll++;
        } else if (ch == 'k' || ch == KEY_UP) {
            if (scroll > 0) scroll--;
        } else if (ch == KEY_NPAGE || ch == CTRL('d')) {
            scroll += vis;
            if (scroll > line_count - vis) scroll = line_count - vis;
            if (scroll < 0) scroll = 0;
        } else if (ch == KEY_PPAGE || ch == CTRL('u')) {
            scroll -= vis;
            if (scroll < 0) scroll = 0;
        } else if (ch == 'g' || ch == KEY_HOME) {
            scroll = 0;
        } else if (ch == 'G' || ch == KEY_END) {
            scroll = line_count - vis;
            if (scroll < 0) scroll = 0;
        }
    }

    for (int i = 0; i < line_count; i++) free(lines[i]);
    free(lines);
    delwin(win);
    touchwin(stdscr);
    refresh();
}

static void show_help(void) {
    static const char *help_lines[] = {
        " __     ___ _          _____ _ _      ",
        " \\ \\   / (_) |__   ___|  ___(_) | ___ ",
        "  \\ \\ / /| | '_ \\ / _ \\ |_  | | | |/ _ \\",
        "   \\ V / | | |_) |  __/  _| | | | |  __/",
        "    \\_/  |_|_.__/ \\___|_|   |_|_|_|\\___|",
        "",
        "Навигация:",
        "  j/↓, k/↑     - Вверх / Вниз",
        "  h/←          - Вверх по каталогам",
        "  l/→/Enter    - Войти / Открыть (текст/код — встроенный просмотр)",
        "  g / G        - В начало / В конец списка",
        "  Tab          - Переключение панелей (в режиме сплита)",
        "  Space        - Выделить / Снять выделение файла",
        "",
        "Действия и вкладки:",
        "  a / [A]      - Открыть меню действий",
        "  p / [=]      - Открыть избранное",
        "  O / [O]      - Настройки (тема, бинды, ассоциации)",
        "  m            - Вкл/выкл режим сплита",
        "  v            - Вкл/выкл режим предпросмотра",
        "  s / S        - Сортировка по колонкам / Реверс",
        "  /            - Поиск по имени",
        "  r            - Обновить список",
        "  R            - Перерисовать экран + обновить",
        "  F1..F4       - Переключение вкладок",
        "  q / [X]      - Выход",
        "  i            - Эта справка",
        "",
        "Клавиши можно переназначить в меню Настройки (O).",
        "",
        "Сотворено AINOnoko.",
        "Свободно распространяется под лицензией GPLv3.",
        NULL
    };
    int total = 0;
    while (help_lines[total]) total++;

    int max_len = 0;
    for (int i = 0; i < total; i++) {
        int len = utf8_strlen(help_lines[i]);
        if (len > max_len) max_len = len;
    }
    if (max_len < 55) max_len = 55;

    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int w = max_len + 6;
    if (w > cols - 4) w = cols - 4;
    if (w < 40) w = 40;

    int h = (rows * 64) / 100;
    if (h > rows - 4) h = rows - 4;
    if (h < 10) h = 10;

    int y = (rows - h) / 2, x = (cols - w) / 2;

    int vis = h - 4;
    if (vis < 1) vis = 1;
    int max_scroll = total - vis;
    if (max_scroll < 0) max_scroll = 0;
    int scroll = 0;

    apply_shadow(y, x, h, w);
    WINDOW *win = newwin(h, w, y, x);
    wbkgd(win, COLOR_PAIR(PAIR_DIALOG));
    keypad(win, TRUE);

    int running = 1;
    while (running) {
        werase(win);
        box(win, 0, 0);
        mvwaddnstr(win, 0, 2, " Справка ", w - 4);

        for (int i = 0; i < vis; i++) {
            int idx = scroll + i;
            if (idx >= total) break;
            mvwprintw(win, 1 + i, 2, "%s", help_lines[idx]);
        }

        if (max_scroll > 0) {
            int bar_x = w - 2;
            int bar_h = h - 3;
            int thumb_h = (bar_h * vis) / total;
            if (thumb_h < 1) thumb_h = 1;
            int thumb_pos = (bar_h * scroll) / max_scroll;
            if (thumb_pos + thumb_h > bar_h) thumb_pos = bar_h - thumb_h;
            for (int i = 0; i < bar_h; i++) {
                int row = 1 + i;
                if (i >= thumb_pos && i < thumb_pos + thumb_h) {
                    wattron(win, A_REVERSE);
                    mvwaddch(win, row, bar_x, ' ');
                    wattroff(win, A_REVERSE);
                }
            }
        }

        char footer[128];
        if (max_scroll > 0) {
            snprintf(footer, sizeof footer, "j/k, PgUp/PgDn - прокрутка | q/Esc - выход [%d/%d]",
                     scroll + 1, max_scroll + 1);
        } else {
            snprintf(footer, sizeof footer, "q/Esc - выход");
        }
        mvwaddnstr(win, h - 2, 2, footer, w - 4);
        wrefresh(win);

        int ch = wgetch(win);
        if (ch == 'q' || ch == 'Q' || ch == 27 || ch == 'i' || ch == 'I' ||
            ch == '\n' || ch == KEY_ENTER) {
            running = 0;
        } else if (ch == KEY_DOWN || ch == 'j') {
            if (scroll < max_scroll) scroll++;
        } else if (ch == KEY_UP || ch == 'k') {
            if (scroll > 0) scroll--;
        } else if (ch == KEY_NPAGE || ch == CTRL('d')) {
            scroll += vis;
            if (scroll > max_scroll) scroll = max_scroll;
        } else if (ch == KEY_PPAGE || ch == CTRL('u')) {
            scroll -= vis;
            if (scroll < 0) scroll = 0;
        } else if (ch == KEY_HOME || ch == 'g') {
            scroll = 0;
        } else if (ch == KEY_END || ch == 'G') {
            scroll = max_scroll;
        }
    }

    delwin(win);
    touchwin(stdscr);
    refresh();
}

static void show_progress(int pct, const char *filename, WINDOW *win) {
    werase(win);
    box(win, 0, 0);
    mvwaddnstr(win, 0, 2, " Копирование ", 20);
    mvwaddnstr(win, 2, 2, filename, getmaxx(win) - 4);
    int w = getmaxx(win) - 10;
    if (w < 10) w = 10;
    int fill = (w * pct) / 100;
    mvwaddch(win, 4, 4, '[');
    for (int i = 0; i < w; i++) {
        if (i < fill) wattron(win, A_REVERSE);
        else wattroff(win, A_REVERSE);
        mvwaddch(win, 4, 5 + i, ' ');
    }
    wattroff(win, A_REVERSE);
    mvwaddch(win, 4, 5 + w, ']');
    mvwprintw(win, 6, 4, "%d%%", pct);
    wrefresh(win);
}

static WINDOW* progress_init(void) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int w = 60, h = 9;
    if (w > cols - 4) w = cols - 4;
    int y = (rows - h) / 2, x = (cols - w) / 2;
    apply_shadow(y, x, h, w);
    WINDOW *win = newwin(h, w, y, x);
    wbkgd(win, COLOR_PAIR(PAIR_DIALOG));
    keypad(win, TRUE);
    return win;
}

static int dialog_input(const char *title, const char *prompt, char *out, size_t outsz) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int w = cols - 20;
    if (w > 76) w = 76;
    if (w < 40) w = 40;
    int h = 7;
    int y = (rows - h) / 2, x = (cols - w) / 2;

    apply_shadow(y, x, h, w);
    WINDOW *win = newwin(h, w, y, x);
    wbkgd(win, COLOR_PAIR(PAIR_DIALOG));
    keypad(win, TRUE);
    box(win, 0, 0);

    char buf[PATH_MAX];
    copy_str(buf, sizeof buf, out);
    int len = strlen(buf);
    int ch;

    while (1) {
        werase(win);
        box(win, 0, 0);
        mvwaddnstr(win, 0, 2, title, w - 4);
        mvwaddnstr(win, 2, 2, prompt, w - 4);
        mvwprintw(win, 3, 2, "%s", buf);
        wmove(win, 3, 2 + utf8_strlen(buf));
        wrefresh(win);

        ch = wgetch(win);
        if (ch == '\n' || ch == KEY_ENTER) break;
        else if (ch == 27) { len = -1; break; }
        else if (ch == 127 || ch == KEY_BACKSPACE) {
            if (len > 0) {
                do {
                    len--;
                } while (len > 0 && (buf[len] & 0xC0) == 0x80);
                buf[len] = 0;
            }
        } else if (ch >= 32 && ch <= 255) {
            if (len < (int)sizeof buf - 1) {
                buf[len++] = (char)ch;
                buf[len] = 0;
            }
        } else if ((ch >> 8) == 0xD0 || (ch >> 8) == 0xD1) {
            if (len < (int)sizeof buf - 2) {
                buf[len++] = (char)(ch >> 8);
                buf[len++] = (char)(ch & 0xFF);
                buf[len] = 0;
            }
        }
    }

    delwin(win);
    touchwin(stdscr);
    refresh();

    if (len >= 0) {
        copy_str(out, outsz, buf);
        return 1;
    }
    return 0;
}

/* Корректная обработка русской раскладки в UTF-8:
 *   "д" U+0434 = 0xD0 0xB4   } Да (Yes)
 *   "Д" U+0414 = 0xD0 0x94   }
 *   "н" U+043D = 0xD0 0xBD   } Нет (No)
 *   "Н" U+041D = 0xD0 0x9D   }
 * Ранее код ошибочно трактовал 0xBD/0x9D ("н"/"Н") как "Да". */
static int dialog_confirm(const char *title, const char *prompt) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int w = cols - 20;
    if (w > 76) w = 76;
    if (w < 40) w = 40;
    int h = 9;
    int y = (rows - h) / 2, x = (cols - w) / 2;

    apply_shadow(y, x, h, w);
    WINDOW *win = newwin(h, w, y, x);
    wbkgd(win, COLOR_PAIR(PAIR_DIALOG));
    keypad(win, TRUE);
    box(win, 0, 0);

    int btn_sel = 0;
    int res = 0;
    int running = 1;
    int ch;
    int btn_w = 10;
    int btn_x = (w - btn_w) / 2;

    while (running) {
        werase(win);
        box(win, 0, 0);
        mvwaddnstr(win, 0, 2, title, w - 4);
        mvwaddnstr(win, 2, 2, prompt, w - 4);

        int y_yes = 4;
        int attr_yes = (btn_sel == 0) ? A_BUTTON_SEL : A_BUTTON;
        wattron(win, attr_yes);
        mvwprintw(win, y_yes, btn_x, "[   Да   ]");
        wattroff(win, attr_yes);

        int y_no = 6;
        int attr_no = (btn_sel == 1) ? A_BUTTON_SEL : A_BUTTON;
        wattron(win, attr_no);
        mvwprintw(win, y_no, btn_x, "[   Нет  ]");
        wattroff(win, attr_no);

        wrefresh(win);

        ch = wgetch(win);
        if (ch == 'y' || ch == 'Y') { res = 1; running = 0; }
        else if (ch == 'n' || ch == 'N' || ch == 27) { res = 0; running = 0; }
        else if (ch == 0xD0) {
            int c2 = wgetch(win);
            if (c2 == 0xB4 || c2 == 0x94) { res = 1; running = 0; }     /* д/Д -> Да */
            else if (c2 == 0xBD || c2 == 0x9D) { res = 0; running = 0; } /* н/Н -> Нет */
        }
        else if (ch == KEY_UP || ch == KEY_DOWN) { btn_sel = 1 - btn_sel; }
        else if (ch == '\n' || ch == KEY_ENTER) { res = (btn_sel == 0) ? 1 : 0; running = 0; }
        else if (ch == KEY_MOUSE) {
            MEVENT ev;
            if (getmouse(&ev) == OK && (ev.bstate & BUTTON1_CLICKED)) {
                if (ev.x >= x + btn_x && ev.x < x + btn_x + btn_w) {
                    if (ev.y == y + y_yes) { res = 1; running = 0; }
                    else if (ev.y == y + y_no) { res = 0; running = 0; }
                }
            }
        }
    }

    delwin(win);
    touchwin(stdscr);
    refresh();
    return res;
}

/* --- Отрисовка главного экрана --- */

static void draw_tabs(int rows, int cols) {
    int tab_w = cols / NUM_TABS;
    for (int i = 0; i < NUM_TABS; i++) {
        int x = i * tab_w;
        int w = (i == NUM_TABS - 1) ? (cols - x) : tab_w;

        char base[64];
        const char *p = g_app.tabs[i].panes[g_app.tabs[i].active_pane].cwd;
        if (p[0] == '\0' || strcmp(p, "/") == 0) {
            snprintf(base, sizeof base, "/");
        } else {
            const char *slash = strrchr(p, '/');
            copy_str(base, sizeof base, slash ? slash + 1 : p);
        }

        char line[128];
        snprintf(line, sizeof line, " %d:%s ", i + 1, base);

        int attr = (i == g_app.active_tab) ? A_TAB_ACTIVE : A_TAB_INACTIVE;
        attron(attr);
        mvhline(rows - 1, x, ' ', w);

        int chars = 0, bytes = 0;
        while (line[bytes] && chars < w) {
            unsigned char c = line[bytes];
            int step = 1;
            if ((c & 0xE0) == 0xC0) step = 2;
            else if ((c & 0xF0) == 0xE0) step = 3;
            else if ((c & 0xF8) == 0xF0) step = 4;
            if (bytes + step > (int)strlen(line)) break;
            bytes += step;
            chars++;
        }
        mvaddnstr(rows - 1, x, line, bytes);
        attroff(attr);
    }
}

static void draw_pane_header(FilePane *p, int pane_idx, int x, int w,
                             int show_type, int show_date, int show_size) {
    PaneGeom g;
    compute_geometry(x, w, show_type, show_date, show_size, &g);

    if (pane_idx >= 0 && pane_idx < 2) {
        g_app.g_x_name[pane_idx] = g.x_name;
        g_app.g_x_type[pane_idx] = g.x_type;
        g_app.g_x_date[pane_idx] = g.x_date;
        g_app.g_x_size[pane_idx] = g.x_size;
    }

    attron(A_HEADER_GRAY);
    mvhline(1, x, ' ', w);

    char h_name[64], h_type[64], h_date[64], h_size[64];
    const char *arrow = p->sort_desc ? "▼" : "▲";
    snprintf(h_name, sizeof h_name, "Имя %s",     (p->sort_field == SORT_NAME) ? arrow : " ");
    snprintf(h_type, sizeof h_type, "Тип %s",     (p->sort_field == SORT_TYPE) ? arrow : " ");
    snprintf(h_date, sizeof h_date, "Изменён %s", (p->sort_field == SORT_DATE) ? arrow : " ");
    snprintf(h_size, sizeof h_size, "Размер %s",  (p->sort_field == SORT_SIZE) ? arrow : " ");

    char name_padded[512], type_padded[64], date_padded[64], size_padded[64];
    build_padded(name_padded, sizeof name_padded, h_name, g.name_w,  0);
    build_padded(type_padded, sizeof type_padded, h_type, TYPE_W,  0);
    build_padded(date_padded, sizeof date_padded, h_date, DATE_W,  0);
    build_padded(size_padded, sizeof size_padded, h_size, SIZE_W,  0);

    mvaddstr(1, x + 2, "[X]");
    mvaddstr(1, g.x_name, name_padded);
    if (g.show_type && g.x_type >= 0 && g.x_type < x + w) mvaddstr(1, g.x_type, type_padded);
    if (g.show_date && g.x_date >= 0 && g.x_date < x + w) mvaddstr(1, g.x_date, date_padded);
    if (g.show_size && g.x_size >= 0 && g.x_size < x + w) mvaddstr(1, g.x_size, size_padded);
    attroff(A_HEADER_GRAY);
}

static void draw_pane_row(FilePane *p, int x, int y, int w, int idx, int is_active,
                          int show_type, int show_date, int show_size) {
    Entry *e = &p->list.items[idx];

    PaneGeom g;
    compute_geometry(x, w, show_type, show_date, show_size, &g);

    int attr;
    if (idx == p->file_sel && is_active)       attr = A_SEL_ACTIVE;
    else if (idx == p->file_sel && !is_active) attr = A_SEL_INACTIVE;
    else if (e->selected)                      attr = A_MARKED;
    else if (e->is_dir || e->is_parent)        attr = A_DIR;
    else if (e->mode & (S_IXUSR | S_IXGRP | S_IXOTH)) attr = A_EXEC;
    else                                       attr = A_PANEL;

    char date_str[16] = "-";
    char size_str[16] = "-";

    if (e->is_parent) {
        snprintf(size_str, sizeof size_str, "DIR");
    } else {
        struct tm tmv;
        time_t now = time(NULL);
        struct tm now_tm;
        localtime_r(&now, &now_tm);
        if (localtime_r(&e->mtime, &tmv)) {
            if (tmv.tm_year == now_tm.tm_year)
                strftime(date_str, sizeof date_str, "%m-%d %H:%M", &tmv);
            else
                strftime(date_str, sizeof date_str, "%Y-%m-%d ", &tmv);
        }
        if (e->is_dir) snprintf(size_str, sizeof size_str, "DIR");
        else           format_size(size_str, sizeof size_str, e->size);
    }

    char name_disp[MAX_NAME + 16];
    if (e->is_parent) snprintf(name_disp, sizeof name_disp, "<--- %s", e->name);
    else              snprintf(name_disp, sizeof name_disp, "%s", e->name);

    char icon = e->is_parent ? '<' : file_icon(e->name, e->is_dir, e->mode);
    const char *tname = type_str(e->name, e->is_dir, e->mode);

    char name_padded[MAX_NAME * 4 + 32];
    char type_padded[64], date_padded[64], size_padded[64];
    build_padded(name_padded, sizeof name_padded, name_disp,  g.name_w,  0);
    build_padded(type_padded, sizeof type_padded, tname,       TYPE_W,  0);
    build_padded(date_padded, sizeof date_padded, date_str,    DATE_W,  0);
    build_padded(size_padded, sizeof size_padded, size_str,    SIZE_W,  1);

    char mark_char = (idx == p->file_sel) ? '>' : (e->selected ? '*' : ' ');
    char row[2048];
    int pos = 0;

    #define ROW_APPEND(...) do { \
        if ((size_t)pos < sizeof(row) - 1) { \
            int _r = snprintf(row + pos, sizeof(row) - (size_t)pos, __VA_ARGS__); \
            if (_r > 0 && (size_t)_r < sizeof(row) - (size_t)pos) { \
                pos += _r; \
            } else { \
                pos = (int)sizeof(row) - 1; \
                row[pos] = '\0'; \
            } \
        } \
    } while (0)

    ROW_APPEND("%c [%c] %s", mark_char, icon, name_padded);
    if (g.show_type) ROW_APPEND(" %s", type_padded);
    if (g.show_date) ROW_APPEND(" %s", date_padded);
    if (g.show_size) ROW_APPEND(" %s", size_padded);
    #undef ROW_APPEND

    if (pos < (int)sizeof(row)) row[pos] = '\0';
    else row[sizeof(row) - 1] = '\0';

    int safe_bytes = 0, chars = 0;
    while (row[safe_bytes] && chars < w) {
        unsigned char c = row[safe_bytes];
        int step = 1;
        if ((c & 0xE0) == 0xC0) step = 2;
        else if ((c & 0xF0) == 0xE0) step = 3;
        else if ((c & 0xF8) == 0xF0) step = 4;
        if (safe_bytes + step > (int)strlen(row)) break;
        safe_bytes += step;
        chars++;
    }

    attron(attr);
    mvhline(y, x, ' ', w);
    mvaddnstr(y, x, row, safe_bytes);
    attroff(attr);
}

static void draw_pane(FilePane *p, int pane_idx, int x, int w, int is_active,
                      int show_type, int show_date, int show_size) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    (void)cols;

    int items_y = 2;
    int vis = rows - 4;
    if (vis < 1) vis = 1;

    int needs_scroll = ((int)p->list.count > vis);
    int content_w = needs_scroll ? w - 1 : w;
    if (content_w < 4) { content_w = w; needs_scroll = 0; }

    draw_pane_header(p, pane_idx, x, content_w, show_type, show_date, show_size);

    attron(A_PANEL);
    for (int i = 0; i < vis; i++) {
        mvhline(items_y + i, x, ' ', content_w);
    }
    attroff(A_PANEL);

    if (p->file_top < 0) p->file_top = 0;
    if (p->file_sel >= (int)p->list.count) p->file_sel = p->list.count ? (int)p->list.count - 1 : 0;
    if (p->file_sel < 0) p->file_sel = 0;
    if (p->file_sel < p->file_top) p->file_top = p->file_sel;
    if (p->file_sel >= p->file_top + vis) p->file_top = p->file_sel - vis + 1;

    for (int i = 0; i < vis; i++) {
        int idx = p->file_top + i;
        if (idx >= (int)p->list.count) break;
        draw_pane_row(p, x, items_y + i, content_w, idx, is_active, show_type, show_date, show_size);
    }

    if (needs_scroll) {
        int bar_x = x + w - 1;
        int total = (int)p->list.count;
        int thumb_h = (vis * vis) / total;
        if (thumb_h < 1) thumb_h = 1;
        if (thumb_h > vis) thumb_h = vis;
        int max_scroll = total - vis;
        int thumb_pos = max_scroll > 0 ? (vis - thumb_h) * p->file_top / max_scroll : 0;
        if (thumb_pos + thumb_h > vis) thumb_pos = vis - thumb_h;
        if (thumb_pos < 0) thumb_pos = 0;

        for (int i = 0; i < vis; i++) {
            if (i >= thumb_pos && i < thumb_pos + thumb_h) {
                attron(A_SCROLL_THUMB);
                mvaddch(items_y + i, bar_x, ' ');
                attroff(A_SCROLL_THUMB);
            } else {
                attron(A_SCROLL_TRACK);
                mvaddch(items_y + i, bar_x, ACS_VLINE);
                attroff(A_SCROLL_TRACK);
            }
        }
    }
}

static void draw_preview(int x, int w, int rows) {
    FilePane *src = &CURRENT_TAB.panes[0];
    int idx = src->file_sel;

    attron(A_HEADER_GRAY);
    mvhline(1, x, ' ', w);
    mvaddstr(1, x + 2, "Предпросмотр");
    attroff(A_HEADER_GRAY);

    int vis = rows - 4;
    if (vis < 1) vis = 1;
    attron(A_PANEL);
    for (int i = 0; i < vis; i++) {
        mvhline(2 + i, x, ' ', w);
    }
    attroff(A_PANEL);

    if (idx < 0 || idx >= (int)src->list.count) return;
    Entry *e = &src->list.items[idx];

    if (e->is_parent) { mvaddstr(2, x + 2, "<--- .."); return; }

    char full[PATH_MAX];
    if (make_path(full, sizeof full, src->cwd, e->name) != 0) return;

    struct stat st;
    if (stat(full, &st) != 0) return;
    if (S_ISDIR(st.st_mode)) { mvaddstr(2, x + 2, "[Папка]"); return; }

    FILE *f = fopen(full, "r");
    if (!f) { mvaddstr(2, x + 2, "[Нет доступа]"); return; }

    int y = 2;
    int max_lines = rows - 4;
    char line_buf[1024];

    int c, is_binary = 0;
    while ((c = fgetc(f)) != EOF && y < 2 + 20) {
        if (c == 0) { is_binary = 1; break; }
    }

    if (is_binary) {
        mvaddstr(2, x + 2, "[Двоичный файл]");
        fclose(f);
        return;
    }

    rewind(f);
    while (fgets(line_buf, sizeof line_buf, f) && y < 2 + max_lines) {
        size_t len = strlen(line_buf);
        while (len > 0 && (line_buf[len-1] == '\n' || line_buf[len-1] == '\r')) {
            line_buf[--len] = '\0';
        }

        int safe_bytes = 0, chars = 0;
        while (line_buf[safe_bytes] && chars < w - 2) {
            unsigned char ch = line_buf[safe_bytes];
            int step = 1;
            if ((ch & 0xE0) == 0xC0) step = 2;
            else if ((ch & 0xF0) == 0xE0) step = 3;
            else if ((ch & 0xF8) == 0xF0) step = 4;
            if (safe_bytes + step > (int)strlen(line_buf)) break;
            safe_bytes += step;
            chars++;
        }
        mvaddnstr(y, x + 2, line_buf, safe_bytes);
        y++;
    }
    fclose(f);
}

static void build_file_info(FilePane *p, char *out, size_t outsz) {
    if (outsz == 0) return;
    out[0] = '\0';
    if (p->list.count == 0 || p->file_sel < 0 || p->file_sel >= (int)p->list.count) return;

    Entry *e = &p->list.items[p->file_sel];
    if (e->is_parent) {
        snprintf(out, outsz, " ..  (родительский каталог)");
        return;
    }

    char size_str[32];
    if (e->is_dir) snprintf(size_str, sizeof size_str, "DIR");
    else           format_size(size_str, sizeof size_str, e->size);

    char date_str[32] = "?";
    struct tm tmv, now_tm;
    time_t now = time(NULL);
    localtime_r(&now, &now_tm);
    if (localtime_r(&e->mtime, &tmv)) {
        if (tmv.tm_year == now_tm.tm_year)
            strftime(date_str, sizeof date_str, "%m-%d %H:%M", &tmv);
        else
            strftime(date_str, sizeof date_str, "%Y-%m-%d ", &tmv);
    }

    char perm[16];
    format_mode(perm, sizeof perm, e->mode);

    snprintf(out, outsz, " %s  |  %s  |  %s  |  %s",
             e->name, size_str, date_str, perm);
}

static void draw(void) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    erase();

    attron(A_TITLE);
    mvhline(0, 0, ' ', cols);
    mvprintw(0, 1, " fm: %s", ACTIVE_PANE.cwd);
    mvprintw(0, cols - 16, "[A] [=] [O] [X]");
    attroff(A_TITLE);

    if (CURRENT_TAB.split_mode) {
        int half = cols / 2;
        int pw0 = half;
        int pw1 = cols - half - 1;
        draw_pane(&CURRENT_TAB.panes[0], 0, 0, pw0, CURRENT_TAB.active_pane == 0,
                  pw0 >= COL_TYPE_MIN, pw0 >= COL_DATE_MIN, pw0 >= COL_SIZE_MIN);
        if (CURRENT_TAB.preview_mode) {
            draw_preview(half + 1, pw1, rows);
        } else {
            draw_pane(&CURRENT_TAB.panes[1], 1, half + 1, pw1, CURRENT_TAB.active_pane == 1,
                      pw1 >= COL_TYPE_MIN, pw1 >= COL_DATE_MIN, pw1 >= COL_SIZE_MIN);
        }
        attron(A_TITLE);
        for (int y = 1; y < rows - 2; y++) mvaddch(y, half, ACS_VLINE);
        attroff(A_TITLE);
    } else {
        draw_pane(&ACTIVE_PANE, CURRENT_TAB.active_pane, 0, cols, 1,
                  cols >= COL_TYPE_MIN, cols >= COL_DATE_MIN, cols >= COL_SIZE_MIN);
    }

    attron(COLOR_PAIR(PAIR_STATUS));
    mvhline(rows - 2, 0, ' ', cols);
    if (g_app.search_active) {
        mvprintw(rows - 2, 1, "Поиск: %s", g_app.search_buf);
        curs_set(1);
        move(rows - 2, 8 + utf8_strlen(g_app.search_buf));
    } else {
        const char *left_text;
        char file_info[1024];
        if (g_app.last_action[0]) {
            left_text = g_app.last_action;
        } else {
            build_file_info(&ACTIVE_PANE, file_info, sizeof file_info);
            left_text = file_info;
        }
        int avail = cols - 18;
        if (avail < 20) avail = 20;
        if (avail > 1023) avail = 1023;
        size_t bytes = utf8_prefix_bytes(left_text, avail);
        mvaddnstr(rows - 2, 1, left_text, bytes);
        mvprintw(rows - 2, cols - 15, "i: справка");
        curs_set(0);
    }
    attroff(COLOR_PAIR(PAIR_STATUS));

    draw_tabs(rows, cols);
    refresh();
}

/* =========================================================================
 * ЗАПУСК ФАЙЛА С УЧЁТОМ АССОЦИАЦИЙ (без shell)
 * ========================================================================= */

static void launch_path(const char *name, const char *full) {
    char fullbuf[PATH_MAX];
    copy_str(fullbuf, sizeof fullbuf, full);

    const char *dot = strrchr(name, '.');
    if (dot && dot != name && dot[1]) {
        const char *ext = dot + 1;
        for (int i = 0; i < g_app.assoc_count; i++) {
            if (strcasecmp(g_app.associations[i].ext, ext) == 0) {
                char *argv[MAX_ARGS];
                int n = tokenize_cmd(g_app.associations[i].cmd, argv, MAX_ARGS);
                if (n > 0 && n < MAX_ARGS - 1) {
                    argv[n] = fullbuf;
                    argv[n + 1] = NULL;
                    spawn_detached_argv(argv);
                    free_argv(argv, n);
                    snprintf(g_app.last_action, sizeof g_app.last_action,
                             "Открыто: %s (%s)", name, g_app.associations[i].cmd);
                    return;
                }
                free_argv(argv, n);
            }
        }
    }

    if (is_text_file(name) && !is_binary_content(fullbuf)) {
        view_file(fullbuf);
        return;
    }

    char *xdg_argv[] = { "xdg-open", fullbuf, NULL };
    spawn_detached_argv(xdg_argv);
    snprintf(g_app.last_action, sizeof g_app.last_action, "Открыто: %s", name);
}

/* =========================================================================
 * ДЕЙСТВИЯ
 * ========================================================================= */

static void action_open(FilePane *p) {
    if (p->list.count == 0) { message_box("Внимание", "Ничего не выбрано"); return; }
    Entry *e = &p->list.items[p->file_sel];

    if (e->is_parent) {
        char prev[PATH_MAX];
        copy_str(prev, sizeof prev, p->cwd);
        if (chdir("..") != 0) { message_box("Ошибка", "Не удалось перейти вверх"); return; }
        if (!getcwd(p->cwd, sizeof p->cwd)) return;
        load_dir(p);
        const char *base = strrchr(prev, '/');
        base = base ? base + 1 : prev;
        p->file_sel = ((p->list.count > 1) ? 1 : 0);
        for (size_t i = 0; i < p->list.count; i++) {
            if (strcmp(p->list.items[i].name, base) == 0) { p->file_sel = i; break; }
        }
        p->file_top = 0;
        return;
    }

    if (e->is_dir) {
        char full[PATH_MAX];
        if (make_path(full, sizeof full, p->cwd, e->name) != 0) { message_box("Ошибка", "Путь слишком длинный"); return; }
        if (chdir(full) != 0) { message_box("Ошибка", "Не удалось войти"); return; }
        if (!getcwd(p->cwd, sizeof p->cwd)) return;
        load_dir(p);
        p->file_sel = (p->list.count > 1) ? 1 : 0;
        p->file_top = 0;
        return;
    }

    char full[PATH_MAX];
    if (make_path(full, sizeof full, p->cwd, e->name) != 0) {
        message_box("Ошибка", "Путь слишком длинный");
        return;
    }

    launch_path(e->name, full);
}

static void action_open_as(FilePane *p) {
    if (p->list.count == 0 || p->file_sel == 0) { message_box("Внимание", "Ничего не выбрано"); return; }
    Entry *e = &p->list.items[p->file_sel];
    if (e->is_parent) { message_box("Ошибка", "Нельзя открыть .."); return; }

    char cmd_buf[256];
    snprintf(cmd_buf, sizeof cmd_buf, "xdg-open");
    if (dialog_input("Открыть как", "Команда (без shell):", cmd_buf, sizeof cmd_buf)) {
        char target[PATH_MAX];
        if (make_path(target, sizeof target, p->cwd, e->name) == 0) {
            char *argv[MAX_ARGS];
            int n = tokenize_cmd(cmd_buf, argv, MAX_ARGS);
            if (n > 0 && n < MAX_ARGS - 1) {
                argv[n] = target;
                argv[n + 1] = NULL;
                spawn_detached_argv(argv);
                free_argv(argv, n);
            } else {
                free_argv(argv, n);
                message_box("Ошибка", "Некорректная команда");
            }
        }
    }
}

static void action_edit(FilePane *p) {
    if (p->list.count == 0) { message_box("Внимание", "Ничего не выбрано"); return; }
    Entry *e = &p->list.items[p->file_sel];
    if (e->is_dir || e->is_parent) { message_box("Ошибка", "Нельзя редактировать каталог"); return; }

    const char *editors[] = {"nvim", "vim", "vi", "nano"};
    const char *ed = NULL;
    const char *env_ed = getenv("EDITOR");

    if (env_ed && *env_ed) {
        ed = env_ed;
    } else {
        for (int i = 0; i < 4; i++) {
            const char *path = getenv("PATH");
            if (!path) break;
            char *path_copy = strdup(path);
            if (!path_copy) break;
            char *save = NULL;
            char *dir = strtok_r(path_copy, ":", &save);
            char full[PATH_MAX];
            while (dir) {
                snprintf(full, sizeof full, "%s/%s", dir, editors[i]);
                if (access(full, X_OK) == 0) { ed = editors[i]; break; }
                dir = strtok_r(NULL, ":", &save);
            }
            free(path_copy);
            if (ed) break;
        }
    }

    if (!ed) { message_box("Ошибка", "Редактор не найден"); return; }

    char full[PATH_MAX];
    if (make_path(full, sizeof full, p->cwd, e->name) != 0) {
        message_box("Ошибка", "Путь слишком длинный");
        return;
    }

    char edbuf[PATH_MAX];
    copy_str(edbuf, sizeof edbuf, ed);

    char *argv[3];
    argv[0] = edbuf;
    argv[1] = full;
    argv[2] = NULL;
    run_foreground(argv, NULL);

    load_dir(p);
}

static void action_refresh(FilePane *p) {
    load_dir(p);
}

static void action_mkdir(FilePane *p) {
    char name[MAX_NAME] = "Новая папка";
    if (dialog_input("Создать папку", "Имя папки:", name, sizeof name)) {
        char target[PATH_MAX];
        if (make_path(target, sizeof target, p->cwd, name) == 0) {
            if (mkdir(target, 0755) == 0) {
                snprintf(g_app.last_action, sizeof g_app.last_action, "Создана папка: %s", name);
                load_dir(p);
            } else {
                snprintf(g_app.last_action, sizeof g_app.last_action, "Ошибка: не удалось создать папку");
            }
        }
    }
}

static void action_touch(FilePane *p) {
    char name[MAX_NAME] = "Новый файл.txt";
    if (dialog_input("Создать файл", "Имя файла:", name, sizeof name)) {
        char target[PATH_MAX];
        if (make_path(target, sizeof target, p->cwd, name) == 0) {
            FILE *f = fopen(target, "w");
            if (f) {
                fclose(f);
                snprintf(g_app.last_action, sizeof g_app.last_action, "Создан файл: %s", name);
                load_dir(p);
            } else {
                snprintf(g_app.last_action, sizeof g_app.last_action, "Ошибка: не удалось создать файл");
            }
        }
    }
}

static void action_template(FilePane *p) {
    const char *templates[] = {"Пустой txt", "Python (.py)", "C (.c)", "Shell (.sh)"};
    int sel = run_submenu("Шаблоны", templates, 4);
    if (sel < 0) return;

    char name[MAX_NAME];
    if (sel == 0) copy_str(name, sizeof name, "новый.txt");
    else if (sel == 1) copy_str(name, sizeof name, "новый.py");
    else if (sel == 2) copy_str(name, sizeof name, "новый.c");
    else copy_str(name, sizeof name, "новый.sh");

    if (dialog_input("Создать из шаблона", "Имя файла:", name, sizeof name)) {
        char target[PATH_MAX];
        if (make_path(target, sizeof target, p->cwd, name) == 0) {
            FILE *f = fopen(target, "w");
            if (f) {
                if (sel == 1) fprintf(f, "#!/usr/bin/env python3\n\ndef main():\n    pass\n\nif __name__ == '__main__':\n    main()\n");
                else if (sel == 2) fprintf(f, "#include <stdio.h>\n\nint main(void) {\n    return 0;\n}\n");
                else if (sel == 3) fprintf(f, "#!/bin/bash\n\necho \"Hello\"\n");
                fclose(f);
                if (sel == 3) chmod(target, 0755);
                snprintf(g_app.last_action, sizeof g_app.last_action, "Создан из шаблона: %s", name);
                load_dir(p);
            } else {
                snprintf(g_app.last_action, sizeof g_app.last_action, "Ошибка создания шаблона");
            }
        }
    }
}

static void action_archive(FilePane *p) {
    char paths[MAX_CLIP][PATH_MAX];
    int count = get_selected_or_current(p, paths, MAX_CLIP);
    if (count == 0) { message_box("Внимание", "Нечего архивировать"); return; }

    const char *formats[] = {".zip", ".tar.gz", ".tar.xz"};
    int fmt = run_submenu("Формат", formats, 3);
    if (fmt < 0) return;

    char archname[MAX_NAME] = "archive";
    if (count == 1) {
        const char *base = strrchr(paths[0], '/');
        copy_str(archname, sizeof archname, base ? base + 1 : paths[0]);
    }
    size_t cur_len = strlen(archname);
    size_t fmt_len = strlen(formats[fmt]);
    if (cur_len + fmt_len < sizeof(archname)) memcpy(archname + cur_len, formats[fmt], fmt_len + 1);

    if (!dialog_input("Архивация", "Имя архива:", archname, sizeof archname)) return;

    char *argv[MAX_ARGS];
    int ai = 0;
    char full_arch[PATH_MAX];
    if (make_path(full_arch, sizeof full_arch, p->cwd, archname) != 0) {
        message_box("Ошибка", "Путь слишком длинный");
        return;
    }

    if (fmt == 0) {
        argv[ai++] = "zip";
        argv[ai++] = "-r";
        argv[ai++] = full_arch;
    } else if (fmt == 1) {
        argv[ai++] = "tar";
        argv[ai++] = "-czf";
        argv[ai++] = full_arch;
    } else {
        argv[ai++] = "tar";
        argv[ai++] = "-cJf";
        argv[ai++] = full_arch;
    }
    for (int i = 0; i < count && ai < MAX_ARGS - 1; i++) {
        char *base = strrchr(paths[i], '/');
        argv[ai++] = base ? base + 1 : paths[i];
    }
    argv[ai] = NULL;

    int rc = run_foreground(argv, p->cwd);

    if (rc == 0) {
        snprintf(g_app.last_action, sizeof g_app.last_action, "Создан архив: %s", archname);
        load_dir(p);
    } else {
        snprintf(g_app.last_action, sizeof g_app.last_action, "Ошибка архивации");
    }
}

static void action_unarchive(FilePane *p) {
    if (p->list.count == 0 || p->file_sel == 0) { message_box("Внимание", "Ничего не выбрано"); return; }
    Entry *e = &p->list.items[p->file_sel];
    const char *dot = strrchr(e->name, '.');
    if (!dot || e->is_dir) { message_box("Ошибка", "Не архив"); return; }

    char full[PATH_MAX];
    if (make_path(full, sizeof full, p->cwd, e->name) != 0) {
        message_box("Ошибка", "Путь слишком длинный");
        return;
    }

    char *argv[8];
    int ai = 0;
    if (!strcasecmp(dot, ".zip")) {
        argv[ai++] = "unzip";
        argv[ai++] = "-o";
        argv[ai++] = full;
    } else if (!strcasecmp(dot, ".gz")) {
        argv[ai++] = "tar"; argv[ai++] = "-xzf"; argv[ai++] = full;
    } else if (!strcasecmp(dot, ".xz")) {
        argv[ai++] = "tar"; argv[ai++] = "-xJf"; argv[ai++] = full;
    } else if (!strcasecmp(dot, ".bz2")) {
        argv[ai++] = "tar"; argv[ai++] = "-xjf"; argv[ai++] = full;
    } else if (!strcasecmp(dot, ".tar")) {
        argv[ai++] = "tar"; argv[ai++] = "-xf"; argv[ai++] = full;
    } else {
        message_box("Ошибка", "Неизвестный формат");
        return;
    }
    argv[ai] = NULL;

    int rc = run_foreground(argv, p->cwd);

    if (rc == 0) {
        snprintf(g_app.last_action, sizeof g_app.last_action, "Разархивировано: %s", e->name);
        load_dir(p);
    } else {
        snprintf(g_app.last_action, sizeof g_app.last_action, "Ошибка разархивации");
    }
}

static void action_copy(FilePane *p) {
    g_app.clip_count = get_selected_or_current(p, g_app.clip_paths, MAX_CLIP);
    if (g_app.clip_count > 0) {
        g_app.clip_is_cut = 0;
        snprintf(g_app.last_action, sizeof g_app.last_action, "Скопировано файлов: %d", g_app.clip_count);
    } else {
        snprintf(g_app.last_action, sizeof g_app.last_action, "Нечего копировать");
    }
}

static void action_cut(FilePane *p) {
    g_app.clip_count = get_selected_or_current(p, g_app.clip_paths, MAX_CLIP);
    if (g_app.clip_count > 0) {
        g_app.clip_is_cut = 1;
        snprintf(g_app.last_action, sizeof g_app.last_action, "Вырезано файлов: %d", g_app.clip_count);
    } else {
        snprintf(g_app.last_action, sizeof g_app.last_action, "Нечего вырезать");
    }
}

static void action_paste(FilePane *p) {
    if (g_app.clip_count == 0) { message_box("Внимание", "Буфер пуст"); return; }

    int processed = 0;
    int failed = 0;

    for (int i = 0; i < g_app.clip_count; i++) {
        const char *base = strrchr(g_app.clip_paths[i], '/');
        base = base ? base + 1 : g_app.clip_paths[i];
        char dst[PATH_MAX], new_dst[PATH_MAX];

        if (make_path(dst, sizeof dst, p->cwd, base) != 0) { failed = 1; break; }

        if (strcmp(g_app.clip_paths[i], dst) == 0 || access(dst, F_OK) == 0) {
            char new_name[MAX_NAME];
            snprintf(new_name, sizeof new_name, "%s_copy", base);
            if (make_path(new_dst, sizeof new_dst, p->cwd, new_name) != 0) { failed = 1; break; }
        } else {
            copy_str(new_dst, sizeof new_dst, dst);
        }

        if (g_app.clip_is_cut && rename(g_app.clip_paths[i], new_dst) == 0) {
            processed = i + 1;
            continue;
        }

        off_t total = calc_size(g_app.clip_paths[i], 0);
        CopyProgress cp = {total, 0, progress_init(), base, -1};
        show_progress(0, cp.disp, cp.win);
        int rc = do_copy_recursive(g_app.clip_paths[i], new_dst, &cp);
        delwin(cp.win);
        touchwin(stdscr);
        refresh();

        if (rc != 0) {
            fs_delete(new_dst);
            snprintf(g_app.last_action, sizeof g_app.last_action, "Ошибка копирования: %s", base);
            message_box("Ошибка", "Не удалось скопировать файл. Частичная копия удалена, исходник сохранён.");
            failed = 1;
            break;
        }

        if (g_app.clip_is_cut) fs_delete(g_app.clip_paths[i]);
        processed = i + 1;
    }

    if (g_app.clip_is_cut && processed > 0) {
        int remaining = g_app.clip_count - processed;
        for (int k = 0; k < remaining; k++) {
            copy_str(g_app.clip_paths[k], PATH_MAX, g_app.clip_paths[processed + k]);
        }
        g_app.clip_count = remaining;
        if (g_app.clip_count == 0) g_app.clip_is_cut = 0;
    }

    if (!failed) {
        snprintf(g_app.last_action, sizeof g_app.last_action, "Вставлено файлов: %d", processed);
    }
    load_dir(p);
    touchwin(stdscr);
    refresh();
}

static void action_delete(FilePane *p) {
    char paths[MAX_CLIP][PATH_MAX];
    int count = get_selected_or_current(p, paths, MAX_CLIP);
    if (count == 0) return;

    char msg[512];
    if (count == 1) {
        const char *base = strrchr(paths[0], '/');
        snprintf(msg, sizeof msg, "Удалить %.200s?", base ? base + 1 : paths[0]);
    } else snprintf(msg, sizeof msg, "Удалить %d объектов?", count);

    if (dialog_confirm("Удаление", msg)) {
        for (int i = 0; i < count; i++) fs_delete(paths[i]);
        snprintf(g_app.last_action, sizeof g_app.last_action, "Удалено объектов: %d", count);
        load_dir(p);
    }
}

static void action_rename(FilePane *p) {
    if (p->list.count == 0 || p->file_sel == 0) return;
    Entry *e = &p->list.items[p->file_sel];

    char new_name[MAX_NAME];
    copy_str(new_name, sizeof new_name, e->name);

    if (dialog_input("Переименование", "Новое имя:", new_name, sizeof new_name)) {
        char src[PATH_MAX], dst[PATH_MAX];
        if (make_path(src, sizeof src, p->cwd, e->name) == 0 && make_path(dst, sizeof dst, p->cwd, new_name) == 0) {
            if (rename(src, dst) == 0) {
                snprintf(g_app.last_action, sizeof g_app.last_action, "Переименовано: %s -> %s", e->name, new_name);
                load_dir(p);
            } else {
                snprintf(g_app.last_action, sizeof g_app.last_action, "Ошибка переименования");
            }
        }
    }
}

static void action_pin(FilePane *p) {
    char paths[MAX_CLIP][PATH_MAX];
    int count = get_selected_or_current(p, paths, MAX_CLIP);
    if (count == 0) {
        if (p->cwd[0]) add_bookmark(p->cwd);
    } else {
        for (int i = 0; i < count; i++) {
            add_bookmark(paths[i]);
        }
    }
    save_config();
    snprintf(g_app.last_action, sizeof g_app.last_action, "Добавлено в избранное");
}

static void action_info(FilePane *p) {
    if (p->list.count == 0 || p->file_sel <= 0 || p->file_sel >= (int)p->list.count) {
        message_box("Внимание", "Ничего не выбрано");
        return;
    }

    Entry *e = &p->list.items[p->file_sel];

    char full[PATH_MAX];
    if (make_path(full, sizeof full, p->cwd, e->name) != 0) {
        message_box("Ошибка", "Путь слишком длинный");
        return;
    }

    struct stat st;
    if (lstat(full, &st) != 0) {
        message_box("Ошибка", "Не удалось получить информацию о файле");
        return;
    }

    char type_buf[64];
    if (S_ISREG(st.st_mode))       snprintf(type_buf, sizeof type_buf, "Обычный файл");
    else if (S_ISDIR(st.st_mode))  snprintf(type_buf, sizeof type_buf, "Каталог");
    else if (S_ISLNK(st.st_mode))  snprintf(type_buf, sizeof type_buf, "Символическая ссылка");
    else if (S_ISBLK(st.st_mode))  snprintf(type_buf, sizeof type_buf, "Блочное устройство");
    else if (S_ISCHR(st.st_mode))  snprintf(type_buf, sizeof type_buf, "Символьное устройство");
    else if (S_ISFIFO(st.st_mode)) snprintf(type_buf, sizeof type_buf, "FIFO (pipe)");
    else if (S_ISSOCK(st.st_mode)) snprintf(type_buf, sizeof type_buf, "Сокет");
    else                           snprintf(type_buf, sizeof type_buf, "Неизвестный тип");

    char perm[11];
    perm[0] = S_ISDIR(st.st_mode)  ? 'd' :
              S_ISLNK(st.st_mode)  ? 'l' :
              S_ISBLK(st.st_mode)  ? 'b' :
              S_ISCHR(st.st_mode)  ? 'c' :
              S_ISFIFO(st.st_mode) ? 'p' :
              S_ISSOCK(st.st_mode) ? 's' : '-';
    perm[1] = (st.st_mode & S_IRUSR) ? 'r' : '-';
    perm[2] = (st.st_mode & S_IWUSR) ? 'w' : '-';
    perm[3] = (st.st_mode & S_IXUSR) ? ((st.st_mode & S_ISUID) ? 's' : 'x') : ((st.st_mode & S_ISUID) ? 'S' : '-');
    perm[4] = (st.st_mode & S_IRGRP) ? 'r' : '-';
    perm[5] = (st.st_mode & S_IWGRP) ? 'w' : '-';
    perm[6] = (st.st_mode & S_IXGRP) ? ((st.st_mode & S_ISGID) ? 's' : 'x') : ((st.st_mode & S_ISGID) ? 'S' : '-');
    perm[7] = (st.st_mode & S_IROTH) ? 'r' : '-';
    perm[8] = (st.st_mode & S_IWOTH) ? 'w' : '-';
    perm[9] = (st.st_mode & S_IXOTH) ? ((st.st_mode & S_ISVTX) ? 't' : 'x') : ((st.st_mode & S_ISVTX) ? 'T' : '-');
    perm[10] = '\0';

    char owner_buf[160], group_buf[160];
    struct passwd *pw = getpwuid(st.st_uid);
    struct group  *gr = getgrgid(st.st_gid);
    if (pw) snprintf(owner_buf, sizeof owner_buf, "%s (UID %u)", pw->pw_name, (unsigned)st.st_uid);
    else    snprintf(owner_buf, sizeof owner_buf, "UID %u", (unsigned)st.st_uid);
    if (gr) snprintf(group_buf, sizeof group_buf, "%s (GID %u)", gr->gr_name, (unsigned)st.st_gid);
    else    snprintf(group_buf, sizeof group_buf, "GID %u", (unsigned)st.st_gid);

    char size_human[32];
    format_size(size_human, sizeof size_human, st.st_size);

    char atime_buf[64] = "?", mtime_buf[64] = "?", ctime_buf[64] = "?";
    struct tm tmv;
    if (localtime_r(&st.st_atime, &tmv)) strftime(atime_buf, sizeof atime_buf, "%Y-%m-%d %H:%M:%S", &tmv);
    if (localtime_r(&st.st_mtime, &tmv)) strftime(mtime_buf, sizeof mtime_buf, "%Y-%m-%d %H:%M:%S", &tmv);
    if (localtime_r(&st.st_ctime, &tmv)) strftime(ctime_buf, sizeof ctime_buf, "%Y-%m-%d %H:%M:%S", &tmv);

    char link_target[PATH_MAX] = "";
    if (S_ISLNK(st.st_mode)) {
        ssize_t ln = readlink(full, link_target, sizeof link_target - 1);
        if (ln > 0) link_target[ln] = '\0';
        else link_target[0] = '\0';
    }

    char mime_buf[128] = "";
    struct stat tgt_st;
    if (stat(full, &tgt_st) == 0 && S_ISREG(tgt_st.st_mode)) {
        if (get_mime_type(full, mime_buf, sizeof mime_buf) != 0) mime_buf[0] = '\0';
    }

    char info[4096];
    int pos = 0;
    #define APPEND(...) do { \
        if ((size_t)pos < sizeof(info) - 1) { \
            int _r = snprintf(info + pos, sizeof(info) - (size_t)pos, __VA_ARGS__); \
            if (_r < 0 || (size_t)_r >= sizeof(info) - (size_t)pos) { \
                pos = (int)sizeof(info) - 1; \
                info[pos] = '\0'; \
            } else { \
                pos += _r; \
            } \
        } \
    } while (0)

    APPEND("Имя:            %s\n", e->name);
    APPEND("Полный путь:    %s\n", full);
    APPEND("Тип:            %s\n", type_buf);
    if (mime_buf[0])
        APPEND("MIME:           %s\n", mime_buf);
    if (link_target[0])
        APPEND("Ссылка на:      %s\n", link_target);
    APPEND("\n");
    APPEND("Размер:         %lld байт (%s)\n", (long long)st.st_size, size_human);
    APPEND("Блоков:         %lld × %ld байт\n",
           (long long)st.st_blocks, (long)st.st_blksize);
    APPEND("\n");
    APPEND("Права:          %s  (%04o)\n", perm, (unsigned)(st.st_mode & 07777));
    APPEND("Владелец:       %s\n", owner_buf);
    APPEND("Группа:         %s\n", group_buf);
    APPEND("Жёстких ссылок: %ld\n", (long)st.st_nlink);
    APPEND("\n");
    APPEND("Inode:          %llu\n", (unsigned long long)st.st_ino);
    APPEND("Устройство:     major=%u, minor=%u\n",
           (unsigned)major(st.st_dev), (unsigned)minor(st.st_dev));
    APPEND("\n");
    APPEND("Изменён:        %s\n", mtime_buf);
    APPEND("Открыт:         %s\n", atime_buf);
    APPEND("Статус изменён: %s\n", ctime_buf);
    #undef APPEND

    if (pos < (int)sizeof(info)) info[pos] = '\0';
    else info[sizeof(info) - 1] = '\0';

    message_box("Свойства файла", info);
}

static int execute_action(int act, FilePane *p) {
    switch ((Action)act) {
    case ACT_OPEN: action_open(p); break;
    case ACT_OPEN_AS: action_open_as(p); break;
    case ACT_EDIT: action_edit(p); break;
    case ACT_COPY: action_copy(p); break;
    case ACT_CUT: action_cut(p); break;
    case ACT_PASTE: action_paste(p); break;
    case ACT_DELETE: action_delete(p); break;
    case ACT_MKDIR: action_mkdir(p); break;
    case ACT_TOUCH: action_touch(p); break;
    case ACT_TEMPLATE: action_template(p); break;
    case ACT_ARCHIVE: action_archive(p); break;
    case ACT_UNARCHIVE: action_unarchive(p); break;
    case ACT_RENAME: action_rename(p); break;
    case ACT_PIN: action_pin(p); break;
    case ACT_REFRESH: action_refresh(p); break;
    case ACT_QUIT: return 0;
    case ACT_INFO: action_info(p); break;
    default: break;
    }
    return 1;
}

/* =========================================================================
 * НАСТРОЙКИ (O)
 * ========================================================================= */

enum {
    SK_HEADER,
    SK_EMPTY,
    SK_RESET,
    SK_THEME,
    SK_BIND,
    SK_ASSOC,
    SK_ADD
};

typedef struct {
    int kind;
    int id;
    char label[128];
} SettingItem;

static void binding_key_to_str(int k, char *out, size_t outsz) {
    if (k == '\n') snprintf(out, outsz, "Enter");
    else if (k == '\t') snprintf(out, outsz, "Tab");
    else if (k == ' ') snprintf(out, outsz, "Space");
    else if (k == 27) snprintf(out, outsz, "Esc");
    else if (k >= 32 && k < 127) { out[0] = (char)k; out[1] = '\0'; }
    else snprintf(out, outsz, "?");
}

static void binding_key_to_input(int k, char *out, size_t outsz) {
    if (k == '\n') snprintf(out, outsz, "\\n");
    else if (k == '\t') snprintf(out, outsz, "\\t");
    else if (k == ' ') snprintf(out, outsz, "\\s");
    else if (k == 27) snprintf(out, outsz, "\\e");
    else if (k >= 32 && k < 127) { out[0] = (char)k; out[1] = '\0'; }
    else snprintf(out, outsz, "?");
}

static int binding_input_to_key(const char *s) {
    if (s[0] == '\\') {
        if (s[1] == 'n') return '\n';
        if (s[1] == 't') return '\t';
        if (s[1] == 's') return ' ';
        if (s[1] == 'e') return 27;
        return 0;
    }
    unsigned char c = (unsigned char)s[0];
    if (c >= 32 && c < 127) return c;
    return 0;
}

static void build_settings_items(SettingItem *items, int *count_out) {
    int n = 0;

    items[n].kind = SK_HEADER; items[n].id = 0;
    snprintf(items[n].label, sizeof items[n].label, "Сбросы");
    n++;

    items[n].kind = SK_RESET; items[n].id = 0;
    snprintf(items[n].label, sizeof items[n].label, "Сбросить настройки (тему и ассоциации)");
    n++;
    items[n].kind = SK_RESET; items[n].id = 1;
    snprintf(items[n].label, sizeof items[n].label, "Сбросить избранное");
    n++;
    items[n].kind = SK_RESET; items[n].id = 2;
    snprintf(items[n].label, sizeof items[n].label, "Сбросить цвета темы");
    n++;
    items[n].kind = SK_RESET; items[n].id = 3;
    snprintf(items[n].label, sizeof items[n].label, "Сбросить бинды к стандартным");
    n++;

    items[n].kind = SK_EMPTY; items[n].id = 0; items[n].label[0] = '\0';
    n++;

    items[n].kind = SK_HEADER; items[n].id = 0;
    snprintf(items[n].label, sizeof items[n].label, "Тема");
    n++;
    for (int i = 0; i < THEME_FIELDS; i++) {
        items[n].kind = SK_THEME; items[n].id = i;
        snprintf(items[n].label, sizeof items[n].label, "%s", theme_name_by_idx(i));
        n++;
    }

    items[n].kind = SK_EMPTY; items[n].id = 0; items[n].label[0] = '\0';
    n++;

    items[n].kind = SK_HEADER; items[n].id = 0;
    snprintf(items[n].label, sizeof items[n].label, "Бинды");
    n++;
    for (int i = 0; i < g_app.binding_count; i++) {
        items[n].kind = SK_BIND; items[n].id = i;
        snprintf(items[n].label, sizeof items[n].label, "%s", g_app.bindings[i].name);
        n++;
    }

    items[n].kind = SK_EMPTY; items[n].id = 0; items[n].label[0] = '\0';
    n++;

    items[n].kind = SK_HEADER; items[n].id = 0;
    snprintf(items[n].label, sizeof items[n].label, "Ассоциации файлов");
    n++;
    for (int i = 0; i < g_app.assoc_count; i++) {
        items[n].kind = SK_ASSOC; items[n].id = i;
        snprintf(items[n].label, sizeof items[n].label, ".%s", g_app.associations[i].ext);
        n++;
    }
    items[n].kind = SK_ADD; items[n].id = 0;
    snprintf(items[n].label, sizeof items[n].label, "Добавить ассоциацию");
    n++;

    *count_out = n;
}

static void run_settings_menu(void) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    int h = (rows * 64) / 100;
    if (h > rows - 4) h = rows - 4;
    if (h < 12) h = 12;

    int w = 76;
    int w_max = cols * 80 / 100;
    if (w > w_max) w = w_max;
    if (w > cols - 4) w = cols - 4;
    if (w < 50) w = 50;

    int y = (rows - h) / 2, x = (cols - w) / 2;

    apply_shadow(y, x, h, w);
    WINDOW *win = newwin(h, w, y, x);
    wbkgd(win, COLOR_PAIR(PAIR_DIALOG));
    keypad(win, TRUE);
    box(win, 0, 0);

    int sel = 0;
    int scroll = 0;
    int running = 1;
    int vis = h - 4;
    if (vis < 1) vis = 1;

    while (running) {
        SettingItem items[256];
        int count = 0;
        build_settings_items(items, &count);

        if (sel >= count) sel = count - 1;
        if (sel < 0) sel = 0;
        while (sel < count && (items[sel].kind == SK_HEADER || items[sel].kind == SK_EMPTY)) sel++;
        if (sel >= count) {
            sel = count - 1;
            while (sel > 0 && (items[sel].kind == SK_HEADER || items[sel].kind == SK_EMPTY)) sel--;
        }

        if (sel < scroll) scroll = sel;
        if (sel >= scroll + vis) scroll = sel - vis + 1;
        if (scroll < 0) scroll = 0;

        werase(win);
        box(win, 0, 0);
        mvwaddnstr(win, 0, 2, " Настройки ", w - 4);

        for (int i = 0; i < vis; i++) {
            int idx = scroll + i;
            if (idx >= count) break;
            SettingItem *item = &items[idx];

            if (item->kind == SK_HEADER) {
                wattron(win, A_HEADER_GRAY);
                mvwhline(win, 1 + i, 1, ' ', w - 2);
                mvwprintw(win, 1 + i, 2, " %s ", item->label);
                wattroff(win, A_HEADER_GRAY);
                continue;
            }
            if (item->kind == SK_EMPTY) {
                continue;
            }

            int attr = (idx == sel) ? A_MENU_SEL : A_MENU_NORMAL;
            wattron(win, attr);
            mvwhline(win, 1 + i, 1, ' ', w - 2);

            if (item->kind == SK_RESET) {
                mvwprintw(win, 1 + i, 2, " %s ", item->label);
            } else if (item->kind == SK_THEME) {
                char *val = theme_field_by_idx(item->id);
                mvwprintw(win, 1 + i, 2, " %-20s [%s]", item->label, val ? val : "");
            } else if (item->kind == SK_BIND) {
                char kbuf[16];
                binding_key_to_str(g_app.bindings[item->id].key, kbuf, sizeof kbuf);
                mvwprintw(win, 1 + i, 2, " %-20s [%s]", item->label, kbuf);
            } else if (item->kind == SK_ASSOC) {
                mvwprintw(win, 1 + i, 2, " %-20s [%s]",
                          item->label, g_app.associations[item->id].cmd);
            } else if (item->kind == SK_ADD) {
                mvwprintw(win, 1 + i, 2, " [ %s ]", item->label);
            }
            wattroff(win, attr);
        }

        if (count > vis) {
            int bar_x = w - 2;
            int bar_h = h - 3;
            int thumb_h = (bar_h * vis) / count;
            if (thumb_h < 1) thumb_h = 1;
            int max_scroll = count - vis;
            int thumb_pos = max_scroll > 0 ? (bar_h - thumb_h) * scroll / max_scroll : 0;
            if (thumb_pos + thumb_h > bar_h) thumb_pos = bar_h - thumb_h;
            for (int i = 0; i < bar_h; i++) {
                if (i >= thumb_pos && i < thumb_pos + thumb_h) {
                    wattron(win, A_SCROLL_THUMB);
                    mvwaddch(win, 1 + i, bar_x, ' ');
                    wattroff(win, A_SCROLL_THUMB);
                } else {
                    wattron(win, A_MENU_NORMAL);
                    mvwaddch(win, 1 + i, bar_x, ACS_VLINE);
                    wattroff(win, A_MENU_NORMAL);
                }
            }
        }

        mvwprintw(win, h - 2, 2, "j/k - выбор, Enter - изменить, d - удалить, Esc/O - выход");
        wrefresh(win);

        int ch = wgetch(win);
        if (ch == 27 || ch == 'o' || ch == 'O' || ch == 'q') {
            running = 0;
        } else if (ch == KEY_DOWN || ch == 'j') {
            int next = sel + 1;
            while (next < count && (items[next].kind == SK_HEADER || items[next].kind == SK_EMPTY)) next++;
            if (next < count) sel = next;
        } else if (ch == KEY_UP || ch == 'k') {
            int prev = sel - 1;
            while (prev >= 0 && (items[prev].kind == SK_HEADER || items[prev].kind == SK_EMPTY)) prev--;
            if (prev >= 0) sel = prev;
        } else if (ch == KEY_NPAGE || ch == CTRL('d')) {
            sel += vis / 2;
            if (sel >= count) sel = count - 1;
            while (sel > 0 && (items[sel].kind == SK_HEADER || items[sel].kind == SK_EMPTY)) sel--;
        } else if (ch == KEY_PPAGE || ch == CTRL('u')) {
            sel -= vis / 2;
            if (sel < 0) sel = 0;
            while (sel < count - 1 && (items[sel].kind == SK_HEADER || items[sel].kind == SK_EMPTY)) sel++;
        } else if (ch == '\n' || ch == KEY_ENTER) {
            SettingItem *item = &items[sel];
            if (item->kind == SK_RESET) {
                if (item->id == 0) {
                    if (dialog_confirm("Сброс", "Сбросить настройки (тему, бинды и ассоциации)?")) {
                        default_config();
                        default_bindings();
                        g_app.assoc_count = 0;
                        g_theme_raw[0] = '\0';
                        g_new_theme_seen = 0;
                        init_attributes();
                        save_config();

                        clearok(stdscr, TRUE);
                        wbkgd(stdscr, COLOR_PAIR(PAIR_BG));
                        clearok(win, TRUE);
                        wbkgd(win, COLOR_PAIR(PAIR_DIALOG));
                        wnoutrefresh(stdscr);
                        wnoutrefresh(win);
                        doupdate();
                    }
                } else if (item->id == 1) {
                    if (dialog_confirm("Сброс", "Очистить избранное?")) {
                        g_app.bookmark_count = 0;
                        save_config();
                    }
                } else if (item->id == 2) {
                    if (dialog_confirm("Сброс", "Сбросить цвета темы?")) {
                        default_config();
                        g_theme_raw[0] = '\0';
                        g_new_theme_seen = 0;
                        init_attributes();
                        save_config();

                        clearok(stdscr, TRUE);
                        wbkgd(stdscr, COLOR_PAIR(PAIR_BG));
                        clearok(win, TRUE);
                        wbkgd(win, COLOR_PAIR(PAIR_DIALOG));
                        wnoutrefresh(stdscr);
                        wnoutrefresh(win);
                        doupdate();
                    }
                } else if (item->id == 3) {
                    if (dialog_confirm("Сброс", "Сбросить бинды к стандартным?")) {
                        default_bindings();
                        save_config();
                    }
                }
            } else if (item->kind == SK_THEME) {
                char *val = theme_field_by_idx(item->id);
                if (val) {
                    char buf[16];
                    copy_str(buf, sizeof buf, val);
                    if (dialog_input("Цвет", "Значение (hex):", buf, sizeof buf)) {
                        copy_str(val, 8, buf);
                        g_theme_raw[0] = '\0';
                        init_attributes();
                        save_config();

                        clearok(stdscr, TRUE);
                        wbkgd(stdscr, COLOR_PAIR(PAIR_BG));
                        clearok(win, TRUE);
                        wbkgd(win, COLOR_PAIR(PAIR_DIALOG));
                        wnoutrefresh(stdscr);
                        wnoutrefresh(win);
                        doupdate();
                    }
                }
            } else if (item->kind == SK_BIND) {
                char buf[16];
                binding_key_to_input(g_app.bindings[item->id].key, buf, sizeof buf);
                if (dialog_input("Бинд", "Символ или \\n, \\t, \\s, \\e:", buf, sizeof buf)) {
                    int nk = binding_input_to_key(buf);
                    if (nk) {
                        g_app.bindings[item->id].key = nk;
                        save_config();
                    }
                }
            } else if (item->kind == SK_ASSOC) {
                char buf[PATH_MAX];
                copy_str(buf, sizeof buf, g_app.associations[item->id].cmd);
                if (dialog_input("Команда", "Программа:", buf, sizeof buf)) {
                    copy_str(g_app.associations[item->id].cmd, PATH_MAX, buf);
                    save_config();
                }
            } else if (item->kind == SK_ADD) {
                if (g_app.assoc_count >= MAX_ASSOC) {
                    message_box("Внимание", "Достигнут лимит ассоциаций");
                } else {
                    char ext[64] = "";
                    if (dialog_input("Новая ассоциация", "Расширение (без точки):", ext, sizeof ext)) {
                        char *p = ext;
                        while (*p == '.') p++;
                        while (*p == ' ' || *p == '\t') p++;
                        if (*p) {
                            char cmd[PATH_MAX] = "xdg-open";
                            if (dialog_input("Новая ассоциация", "Программа:", cmd, sizeof cmd)) {
                                copy_str(g_app.associations[g_app.assoc_count].ext, 32, p);
                                copy_str(g_app.associations[g_app.assoc_count].cmd, PATH_MAX, cmd);
                                g_app.assoc_count++;
                                save_config();
                            }
                        }
                    }
                }
            }
        } else if (ch == 'd' || ch == 'D') {
            SettingItem *item = &items[sel];
            if (item->kind == SK_ASSOC) {
                char msg[160];
                snprintf(msg, sizeof msg, "Удалить ассоциацию %s?", item->label);
                if (dialog_confirm("Удаление", msg)) {
                    for (int i = item->id; i < g_app.assoc_count - 1; i++) {
                        g_app.associations[i] = g_app.associations[i + 1];
                    }
                    g_app.assoc_count--;
                    save_config();
                }
            }
        } else if (ch == KEY_RESIZE) {
            delwin(win);
            getmaxyx(stdscr, rows, cols);
            h = (rows * 64) / 100;
            if (h > rows - 4) h = rows - 4;
            if (h < 12) h = 12;
            w = 76;
            w_max = cols * 80 / 100;
            if (w > w_max) w = w_max;
            if (w > cols - 4) w = cols - 4;
            if (w < 50) w = 50;
            y = (rows - h) / 2; x = (cols - w) / 2;
            vis = h - 4;
            if (vis < 1) vis = 1;
            apply_shadow(y, x, h, w);
            win = newwin(h, w, y, x);
            wbkgd(win, COLOR_PAIR(PAIR_DIALOG));
            keypad(win, TRUE);
            box(win, 0, 0);
        }
    }

    delwin(win);
    touchwin(stdscr);
    refresh();
}

/* =========================================================================
 * МЕНЮ
 * ========================================================================= */

static int run_submenu(const char *title, const char **options, int count) {
    int max_len = utf8_strlen(title);
    for (int i = 0; i < count; i++) {
        int len = utf8_strlen(options[i]);
        if (len > max_len) max_len = len;
    }
    int menu_w = max_len + 6, menu_h = count + 2;
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int start_x = (cols - menu_w) / 2, start_y = (rows - menu_h) / 2;

    apply_shadow(start_y, start_x, menu_h, menu_w);
    WINDOW *win = newwin(menu_h, menu_w, start_y, start_x);
    wbkgd(win, COLOR_PAIR(PAIR_DIALOG));
    keypad(win, TRUE);
    box(win, 0, 0);

    int sel = 0, ch, res = -1, running = 1;
    while (running) {
        werase(win);
        box(win, 0, 0);

        wattron(win, A_HEADER_GRAY);
        mvwhline(win, 0, 1, ' ', menu_w - 2);
        mvwprintw(win, 0, (menu_w - utf8_strlen(title)) / 2, "%s", title);
        wattroff(win, A_HEADER_GRAY);

        for (int i = 0; i < count; i++) {
            int attr = (i == sel) ? A_MENU_SEL : A_MENU_NORMAL;
            wattron(win, attr);
            mvwhline(win, 1 + i, 1, ' ', menu_w - 2);
            mvwprintw(win, 1 + i, 2, "%-*s", menu_w - 4, options[i]);
            wattroff(win, attr);
        }
        wrefresh(win);

        ch = get_key(0);
        if (ch == 27 || ch == 'a') running = 0;
        else if (ch == KEY_DOWN || ch == 'j') sel = (sel + 1) % count;
        else if (ch == KEY_UP || ch == 'k') sel = (sel - 1 + count) % count;
        else if (ch == '\n' || ch == KEY_ENTER) { res = sel; running = 0; }
        else if (ch == KEY_MOUSE) {
            MEVENT ev;
            if (getmouse(&ev) == OK && (ev.bstate & (BUTTON1_CLICKED | BUTTON1_DOUBLE_CLICKED))) {
                if (ev.x >= start_x && ev.x < start_x + menu_w && ev.y > start_y && ev.y < start_y + menu_h - 1) {
                    int clicked = ev.y - start_y - 1;
                    if (clicked >= 0 && clicked < count) {
                        sel = clicked;
                        if (ev.bstate & BUTTON1_DOUBLE_CLICKED) { res = sel; running = 0; }
                    }
                }
            }
        } else if (ch == KEY_RESIZE) {
            delwin(win);
            getmaxyx(stdscr, rows, cols);
            start_x = (cols - menu_w) / 2; start_y = (rows - menu_h) / 2;
            win = newwin(menu_h, menu_w, start_y, start_x);
            keypad(win, TRUE);
        }
    }
    delwin(win); touchwin(stdscr); refresh();
    return res;
}

static int run_menu(FilePane *p) {
    int max_len = 0;
    for (int i = 0; i < ACT_COUNT; i++) {
        int len = utf8_strlen(ACTION_NAMES[i]);
        if (len > max_len) max_len = len;
    }

    int vis = ACT_COUNT < MENU_MAX_VIS ? ACT_COUNT : MENU_MAX_VIS;
    int needs_scroll = (ACT_COUNT > vis);
    int menu_w = max_len + 6 + (needs_scroll ? 1 : 0);
    int menu_h = vis + 2;

    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int start_x = (cols - menu_w) / 2, start_y = (rows - menu_h) / 2;

    apply_shadow(start_y, start_x, menu_h, menu_w);
    WINDOW *win = newwin(menu_h, menu_w, start_y, start_x);
    wbkgd(win, COLOR_PAIR(PAIR_DIALOG));
    keypad(win, TRUE);
    box(win, 0, 0);

    int sel = 0, ch, running = 1, res = 1;
    int scroll = 0;
    int content_w = menu_w - 2 - (needs_scroll ? 1 : 0);

    while (running) {
        werase(win);
        box(win, 0, 0);

        wattron(win, A_HEADER_GRAY);
        mvwhline(win, 0, 1, ' ', menu_w - 2);
        mvwprintw(win, 0, (menu_w - utf8_strlen("Действия")) / 2, "Действия");
        wattroff(win, A_HEADER_GRAY);

        if (sel < scroll) scroll = sel;
        if (sel >= scroll + vis) scroll = sel - vis + 1;

        for (int i = 0; i < vis; i++) {
            int idx = scroll + i;
            if (idx >= ACT_COUNT) break;
            int attr = (idx == sel) ? A_MENU_SEL : A_MENU_NORMAL;
            wattron(win, attr);
            mvwhline(win, 1 + i, 1, ' ', content_w);
            mvwprintw(win, 1 + i, 2, "%-*s", content_w - 2, ACTION_NAMES[idx]);
            wattroff(win, attr);
        }

        if (needs_scroll) {
            int bar_x = menu_w - 2;
            int bar_h = menu_h - 2;
            int thumb_h = (bar_h * vis) / ACT_COUNT;
            if (thumb_h < 1) thumb_h = 1;
            int max_scroll = ACT_COUNT - vis;
            int thumb_pos = max_scroll > 0 ? (bar_h - thumb_h) * scroll / max_scroll : 0;
            if (thumb_pos + thumb_h > bar_h) thumb_pos = bar_h - thumb_h;
            for (int i = 0; i < bar_h; i++) {
                if (i >= thumb_pos && i < thumb_pos + thumb_h) {
                    wattron(win, A_SCROLL_THUMB);
                    mvwaddch(win, 1 + i, bar_x, ' ');
                    wattroff(win, A_SCROLL_THUMB);
                } else {
                    wattron(win, A_MENU_NORMAL);
                    mvwaddch(win, 1 + i, bar_x, ACS_VLINE);
                    wattroff(win, A_MENU_NORMAL);
                }
            }
        }
        wrefresh(win);

        ch = get_key(0);
        if (ch == 27 || ch == 'a') running = 0;
        else if (ch == KEY_DOWN || ch == 'j') sel = (sel + 1) % ACT_COUNT;
        else if (ch == KEY_UP || ch == 'k') sel = (sel - 1 + ACT_COUNT) % ACT_COUNT;
        else if (ch == '\n' || ch == KEY_ENTER) { res = execute_action(sel, p); running = 0; }
        else if (ch == KEY_MOUSE) {
            MEVENT ev;
            if (getmouse(&ev) == OK && (ev.bstate & (BUTTON1_CLICKED | BUTTON1_DOUBLE_CLICKED))) {
                if (ev.x >= start_x && ev.x < start_x + menu_w && ev.y > start_y && ev.y < start_y + menu_h - 1) {
                    int clicked = scroll + (ev.y - start_y - 1);
                    if (clicked >= 0 && clicked < ACT_COUNT) {
                        sel = clicked;
                        if (ev.bstate & BUTTON1_DOUBLE_CLICKED) { res = execute_action(sel, p); running = 0; }
                    }
                }
            }
        }
    }
    delwin(win); touchwin(stdscr); refresh();
    return res;
}

static void open_bookmark(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            enter_dir(path, &ACTIVE_PANE);
        } else {
            const char *base = strrchr(path, '/');
            base = base ? base + 1 : path;
            launch_path(base, path);
        }
    } else {
        message_box("Ошибка", "Не удалось открыть закладку");
    }
}

static void run_bookmarks_menu(void) {
    if (g_app.bookmark_count == 0) { message_box("Избранное", "Список пуст"); return; }

    int max_len = 10;
    for (int i = 0; i < g_app.bookmark_count; i++) {
        int len = utf8_strlen(g_app.bookmarks[i]);
        if (len > max_len) max_len = len;
    }
    int menu_w = max_len + 6;
    if (menu_w > 60) menu_w = 60;
    int menu_h = g_app.bookmark_count + 2;
    if (menu_h > 20) menu_h = 20;

    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int start_x = (cols - menu_w) / 2, start_y = (rows - menu_h) / 2;

    apply_shadow(start_y, start_x, menu_h, menu_w);
    WINDOW *win = newwin(menu_h, menu_w, start_y, start_x);
    wbkgd(win, COLOR_PAIR(PAIR_DIALOG));
    keypad(win, TRUE);
    box(win, 0, 0);

    int sel = 0, ch, running = 1, scroll = 0;
    int vis = menu_h - 2;

    while (running) {
        werase(win);
        box(win, 0, 0);

        wattron(win, A_HEADER_GRAY);
        mvwhline(win, 0, 1, ' ', menu_w - 2);
        mvwprintw(win, 0, (menu_w - 13) / 2, " Избранное ");
        wattroff(win, A_HEADER_GRAY);

        if (sel < scroll) scroll = sel;
        if (sel >= scroll + vis) scroll = sel - vis + 1;

        for (int i = 0; i < vis; i++) {
            int idx = scroll + i;
            if (idx >= g_app.bookmark_count) break;
            int attr = (idx == sel) ? A_MENU_SEL : A_MENU_NORMAL;
            wattron(win, attr);
            mvwhline(win, 1 + i, 1, ' ', menu_w - 2);
            char disp[PATH_MAX + 2];
            snprintf(disp, sizeof disp, " %s", g_app.bookmarks[idx]);
            mvwprintw(win, 1 + i, 1, "%-*.*s", menu_w - 2, menu_w - 2, disp);
            wattroff(win, attr);
        }
        wrefresh(win);

        ch = get_key(0);
        if (ch == 27 || ch == 'p' || ch == 'P') {
            running = 0;
        } else if (ch == KEY_DOWN || ch == 'j') {
            sel = (sel + 1) % g_app.bookmark_count;
        } else if (ch == KEY_UP || ch == 'k') {
            sel = (sel - 1 + g_app.bookmark_count) % g_app.bookmark_count;
        } else if (ch == '\n' || ch == KEY_ENTER) {
            open_bookmark(g_app.bookmarks[sel]);
            running = 0;
        } else if (ch == 'd' || ch == 'D') {
            char msg[PATH_MAX + 64];
            snprintf(msg, sizeof msg, "Удалить закладку %.100s?", g_app.bookmarks[sel]);
            if (dialog_confirm("Удаление", msg)) {
                for (int i = sel; i < g_app.bookmark_count - 1; i++) {
                    copy_str(g_app.bookmarks[i], PATH_MAX, g_app.bookmarks[i+1]);
                }
                g_app.bookmark_count--;
                save_config();
                if (g_app.bookmark_count == 0) { running = 0; }
                else if (sel >= g_app.bookmark_count) sel = g_app.bookmark_count - 1;
                werase(stdscr); refresh();
            }
        } else if (ch == KEY_MOUSE) {
            MEVENT ev;
            if (getmouse(&ev) == OK && (ev.bstate & (BUTTON1_CLICKED | BUTTON1_DOUBLE_CLICKED))) {
                if (ev.x >= start_x && ev.x < start_x + menu_w && ev.y > start_y && ev.y < start_y + menu_h - 1) {
                    int clicked = scroll + (ev.y - start_y - 1);
                    if (clicked >= 0 && clicked < g_app.bookmark_count) {
                        sel = clicked;
                        if (ev.bstate & BUTTON1_DOUBLE_CLICKED) {
                            open_bookmark(g_app.bookmarks[sel]);
                            running = 0;
                        }
                    }
                }
            }
        } else if (ch == KEY_RESIZE) {
            delwin(win);
            getmaxyx(stdscr, rows, cols);
            start_x = (cols - menu_w) / 2; start_y = (rows - menu_h) / 2;
            win = newwin(menu_h, menu_w, start_y, start_x);
            keypad(win, TRUE);
        }
    }
    delwin(win);
    touchwin(stdscr);
    refresh();
}

/* =========================================================================
 * ОБРАБОТКА ВВОДА
 * ========================================================================= */

static void switch_tab(int idx) {
    if (idx < 0 || idx >= NUM_TABS || idx == g_app.active_tab) return;
    g_app.active_tab = idx;
    g_app.last_action[0] = '\0';

    if (ACTIVE_PANE.cwd[0] == '\0') {
        const char *home = getenv("HOME");
        copy_str(ACTIVE_PANE.cwd, sizeof ACTIVE_PANE.cwd, home ? home : "/");
        load_dir(&ACTIVE_PANE);
    }
}

static void handle_mouse(MEVENT ev) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    if (ev.y == 0) {
        if (ev.x >= cols - 4 && ev.x <= cols - 2) {
            g_app.running = 0;
            return;
        } else if (ev.x >= cols - 8 && ev.x <= cols - 6) {
            run_settings_menu();
            return;
        } else if (ev.x >= cols - 12 && ev.x <= cols - 10) {
            run_bookmarks_menu();
            return;
        } else if (ev.x >= cols - 16 && ev.x <= cols - 14) {
            if (!run_menu(&ACTIVE_PANE)) g_app.running = 0;
            return;
        }
    }

    if (ev.bstate & BUTTON3_CLICKED) {
        if (!run_menu(&ACTIVE_PANE)) g_app.running = 0;
    } else if (ev.bstate & (BUTTON1_CLICKED | BUTTON1_PRESSED | BUTTON1_DOUBLE_CLICKED)) {
        if (ev.y == rows - 1) {
            if (!g_app.search_active) {
                int tab_idx = ev.x / (cols / NUM_TABS);
                if (tab_idx >= 0 && tab_idx < NUM_TABS) switch_tab(tab_idx);
            }
        }
        else if (ev.y == 1) {
            if (CURRENT_TAB.preview_mode && ev.x >= cols / 2) {
                return;
            }
            int pane_idx = CURRENT_TAB.split_mode ? (ev.x >= cols / 2 ? 1 : 0) : CURRENT_TAB.active_pane;
            FilePane *p = &CURRENT_TAB.panes[pane_idx];
            int new_field = -1;

            if (g_app.g_x_size[pane_idx] != -1 && ev.x >= g_app.g_x_size[pane_idx]) new_field = SORT_SIZE;
            else if (g_app.g_x_date[pane_idx] != -1 && ev.x >= g_app.g_x_date[pane_idx]) new_field = SORT_DATE;
            else if (g_app.g_x_type[pane_idx] != -1 && ev.x >= g_app.g_x_type[pane_idx]) new_field = SORT_TYPE;
            else if (g_app.g_x_name[pane_idx] != 0 && ev.x >= g_app.g_x_name[pane_idx]) new_field = SORT_NAME;

            if (new_field != -1) {
                if (p->sort_field == (SortField)new_field) p->sort_desc = !p->sort_desc;
                else { p->sort_field = (SortField)new_field; p->sort_desc = 0; }
                do_sort(p);
            }
        }
        else if (ev.y >= 2 && ev.y < rows - 2) {
            int pane_idx = CURRENT_TAB.split_mode ? (ev.x >= cols / 2 ? 1 : 0) : CURRENT_TAB.active_pane;
            if (CURRENT_TAB.preview_mode && pane_idx == 1) return;

            FilePane *p = &CURRENT_TAB.panes[pane_idx];
            int idx = p->file_top + (ev.y - 2);
            if (idx >= 0 && idx < (int)p->list.count) {
                CURRENT_TAB.active_pane = pane_idx;
                p->file_sel = idx;
                if (ev.bstate & BUTTON1_DOUBLE_CLICKED) action_open(p);
            }
        }
    }
    if (ev.bstate & BUTTON4_PRESSED) { if (ACTIVE_PANE.file_sel > 0) ACTIVE_PANE.file_sel--; }
    if (ev.bstate & BUTTON5_PRESSED) { if (ACTIVE_PANE.file_sel + 1 < (int)ACTIVE_PANE.list.count) ACTIVE_PANE.file_sel++; }
}

static void handle_key(int ch) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    (void)cols;
    int vis = rows - 4;
    if (vis < 1) vis = 1;

    if (g_app.search_active) {
        if (ch == KEY_F(1)) { g_app.search_active = 0; switch_tab(0); }
        else if (ch == KEY_F(2)) { g_app.search_active = 0; switch_tab(1); }
        else if (ch == KEY_F(3)) { g_app.search_active = 0; switch_tab(2); }
        else if (ch == KEY_F(4)) { g_app.search_active = 0; switch_tab(3); }
        else if (ch == 27 || ch == CTRL('c')) g_app.search_active = 0;
        else if (ch == '\n' || ch == KEY_ENTER) g_app.search_active = 0;
        else if (ch == 127 || ch == KEY_BACKSPACE) {
            if (g_app.search_len > 0) {
                do {
                    g_app.search_len--;
                } while (g_app.search_len > 0 && (g_app.search_buf[g_app.search_len] & 0xC0) == 0x80);
                g_app.search_buf[g_app.search_len] = 0;
                for (size_t i = 0; i < ACTIVE_PANE.list.count; i++) {
                    if (!ACTIVE_PANE.list.items[i].is_parent && strcasestr(ACTIVE_PANE.list.items[i].name, g_app.search_buf)) {
                        ACTIVE_PANE.file_sel = (int)i; break;
                    }
                }
            }
        } else if (ch >= 32 && ch <= 255) {
            if (g_app.search_len < (int)sizeof(g_app.search_buf) - 1) {
                g_app.search_buf[g_app.search_len++] = (char)ch;
                g_app.search_buf[g_app.search_len] = 0;
                for (size_t i = 0; i < ACTIVE_PANE.list.count; i++) {
                    if (!ACTIVE_PANE.list.items[i].is_parent && strcasestr(ACTIVE_PANE.list.items[i].name, g_app.search_buf)) {
                        ACTIVE_PANE.file_sel = (int)i; break;
                    }
                }
            }
        } else if ((ch >> 8) == 0xD0 || (ch >> 8) == 0xD1) {
            if (g_app.search_len < (int)sizeof(g_app.search_buf) - 2) {
                g_app.search_buf[g_app.search_len++] = (char)(ch >> 8);
                g_app.search_buf[g_app.search_len++] = (char)(ch & 0xFF);
                g_app.search_buf[g_app.search_len] = 0;
                for (size_t i = 0; i < ACTIVE_PANE.list.count; i++) {
                    if (!ACTIVE_PANE.list.items[i].is_parent && strcasestr(ACTIVE_PANE.list.items[i].name, g_app.search_buf)) {
                        ACTIVE_PANE.file_sel = (int)i; break;
                    }
                }
            }
        }
        return;
    }

    g_app.last_action[0] = '\0';

    const char *bound = NULL;
    for (int i = 0; i < g_app.binding_count; i++) {
        if (g_app.bindings[i].key == ch) { bound = g_app.bindings[i].name; break; }
    }

    if (bound) {
        if (!strcmp(bound, "quit")) { g_app.running = 0; return; }
        if (!strcmp(bound, "menu")) { if (!run_menu(&ACTIVE_PANE)) g_app.running = 0; return; }
        if (!strcmp(bound, "bookmarks")) { run_bookmarks_menu(); return; }
        if (!strcmp(bound, "settings")) { run_settings_menu(); return; }
        if (!strcmp(bound, "help")) { show_help(); return; }
        if (!strcmp(bound, "sort_next")) {
            ACTIVE_PANE.sort_field = (ACTIVE_PANE.sort_field + 1) % SORT_COUNT;
            do_sort(&ACTIVE_PANE);
            return;
        }
        if (!strcmp(bound, "sort_reverse")) {
            ACTIVE_PANE.sort_desc = !ACTIVE_PANE.sort_desc;
            do_sort(&ACTIVE_PANE);
            return;
        }
        if (!strcmp(bound, "split")) {
            CURRENT_TAB.split_mode = !CURRENT_TAB.split_mode;
            if (!CURRENT_TAB.split_mode) CURRENT_TAB.preview_mode = 0;
            if (CURRENT_TAB.split_mode && CURRENT_TAB.panes[1].cwd[0] == '\0') {
                copy_str(CURRENT_TAB.panes[1].cwd, sizeof CURRENT_TAB.panes[1].cwd, ACTIVE_PANE.cwd);
                load_dir(&CURRENT_TAB.panes[1]);
            }
            return;
        }
        if (!strcmp(bound, "preview")) {
            CURRENT_TAB.preview_mode = !CURRENT_TAB.preview_mode;
            if (CURRENT_TAB.preview_mode) CURRENT_TAB.split_mode = 1;
            return;
        }
        if (!strcmp(bound, "select")) {
            if (ACTIVE_PANE.file_sel > 0 && ACTIVE_PANE.file_sel < (int)ACTIVE_PANE.list.count) {
                ACTIVE_PANE.list.items[ACTIVE_PANE.file_sel].selected ^= 1;
                if (ACTIVE_PANE.file_sel + 1 < (int)ACTIVE_PANE.list.count) ACTIVE_PANE.file_sel++;
            }
            return;
        }
        if (!strcmp(bound, "search")) { g_app.search_active = 1; g_app.search_len = 0; g_app.search_buf[0] = 0; return; }
        if (!strcmp(bound, "refresh")) { action_refresh(&ACTIVE_PANE); return; }
        if (!strcmp(bound, "refresh_full")) { action_refresh(&ACTIVE_PANE); clearok(stdscr, TRUE); return; }
        if (!strcmp(bound, "up")) { if (ACTIVE_PANE.file_sel > 0) ACTIVE_PANE.file_sel--; return; }
        if (!strcmp(bound, "down")) { if (ACTIVE_PANE.file_sel + 1 < (int)ACTIVE_PANE.list.count) ACTIVE_PANE.file_sel++; return; }
        if (!strcmp(bound, "home")) { ACTIVE_PANE.file_sel = 0; ACTIVE_PANE.file_top = 0; return; }
        if (!strcmp(bound, "end")) { ACTIVE_PANE.file_sel = ACTIVE_PANE.list.count ? (int)ACTIVE_PANE.list.count - 1 : 0; return; }
        if (!strcmp(bound, "parent")) {
            if (!CURRENT_TAB.preview_mode || CURRENT_TAB.active_pane == 0) action_open(&ACTIVE_PANE);
            return;
        }
        if (!strcmp(bound, "open") || !strcmp(bound, "open_l")) {
            if (!CURRENT_TAB.preview_mode || CURRENT_TAB.active_pane == 0) action_open(&ACTIVE_PANE);
            return;
        }
    }

    switch (ch) {
    case KEY_F(1): switch_tab(0); break;
    case KEY_F(2): switch_tab(1); break;
    case KEY_F(3): switch_tab(2); break;
    case KEY_F(4): switch_tab(3); break;
    case '\t':
        if (CURRENT_TAB.split_mode && !CURRENT_TAB.preview_mode) CURRENT_TAB.active_pane ^= 1;
        break;
    case KEY_LEFT: if (CURRENT_TAB.split_mode) CURRENT_TAB.active_pane = 0; break;
    case KEY_RIGHT: if (CURRENT_TAB.split_mode && !CURRENT_TAB.preview_mode) CURRENT_TAB.active_pane = 1; break;
    case KEY_HOME: ACTIVE_PANE.file_sel = 0; ACTIVE_PANE.file_top = 0; break;
    case KEY_END: ACTIVE_PANE.file_sel = ACTIVE_PANE.list.count ? (int)ACTIVE_PANE.list.count - 1 : 0; break;
    case KEY_DOWN: if (ACTIVE_PANE.file_sel + 1 < (int)ACTIVE_PANE.list.count) ACTIVE_PANE.file_sel++; break;
    case KEY_UP: if (ACTIVE_PANE.file_sel > 0) ACTIVE_PANE.file_sel--; break;
    case CTRL('d'): case KEY_NPAGE:
        ACTIVE_PANE.file_sel += vis / 2;
        if (ACTIVE_PANE.file_sel >= (int)ACTIVE_PANE.list.count) ACTIVE_PANE.file_sel = ACTIVE_PANE.list.count ? (int)ACTIVE_PANE.list.count - 1 : 0;
        break;
    case CTRL('u'): case KEY_PPAGE:
        ACTIVE_PANE.file_sel -= vis / 2;
        if (ACTIVE_PANE.file_sel < 0) ACTIVE_PANE.file_sel = 0;
        break;
    case 127: case KEY_BACKSPACE:
        if (!CURRENT_TAB.preview_mode || CURRENT_TAB.active_pane == 0) action_open(&ACTIVE_PANE);
        break;
    }
}

/* =========================================================================
 * ТОЧКА ВХОДА
 * ========================================================================= */

int main(void) {
    setlocale(LC_ALL, "");
    memset(&g_app, 0, sizeof(g_app));
    g_app.running = 1;

    if (!getcwd(g_app.tabs[0].panes[0].cwd, sizeof g_app.tabs[0].panes[0].cwd)) {
        perror("getcwd"); return 1;
    }

    load_config();

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGHUP, sig_handler);

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    init_attributes();
    wbkgd(stdscr, COLOR_PAIR(PAIR_BG));

    mousemask(BUTTON1_CLICKED | BUTTON1_PRESSED | BUTTON1_DOUBLE_CLICKED |
              BUTTON3_CLICKED | BUTTON3_PRESSED | BUTTON4_PRESSED | BUTTON5_PRESSED, NULL);

    for (int i = 0; i < NUM_TABS; i++) {
        if (g_app.tabs[i].panes[0].cwd[0])
            load_dir(&g_app.tabs[i].panes[0]);
        if (g_app.tabs[i].panes[1].cwd[0])
            load_dir(&g_app.tabs[i].panes[1]);
    }

    if (g_app.tabs[0].panes[0].cwd[0] == '\0') {
        load_dir(&g_app.tabs[0].panes[0]);
    }
    g_app.tabs[0].panes[0].file_sel = (g_app.tabs[0].panes[0].list.count > 1) ? 1 : 0;

    for (int i = 0; i < NUM_TABS; i++) {
        for (int pi = 0; pi < 2; pi++) {
            FilePane *p = &g_app.tabs[i].panes[pi];
            if (p->pending_sel[0] && p->list.count > 0) {
                for (size_t j = 1; j < p->list.count; j++) {
                    if (strcmp(p->list.items[j].name, p->pending_sel) == 0) {
                        p->file_sel = (int)j;
                        break;
                    }
                }
                p->pending_sel[0] = '\0';
            }
        }
    }

    while (g_app.running) {
        int rows, cols;
        getmaxyx(stdscr, rows, cols);

        if (cols < MIN_COLS || rows < MIN_ROWS) {
            erase();
            mvprintw(0, 0, "Мало места: %dx%d (нужно >= %dx%d).", cols, rows, MIN_COLS, MIN_ROWS);
            mvprintw(1, 0, "Измените размер или 'q' для выхода.");
            refresh();
            int ch = get_key(0);
            if (ch == 'q') break;
            continue;
        }

        draw();
        int ch = get_key(g_app.search_active);
        if (ch == KEY_MOUSE) {
            MEVENT ev;
            if (getmouse(&ev) == OK) handle_mouse(ev);
        } else if (ch == KEY_RESIZE) {
            clearok(stdscr, TRUE);
            refresh();
            continue;
        } else {
            handle_key(ch);
        }
    }

    save_config();

    for (int i = 0; i < NUM_TABS; i++) {
        free_list(&g_app.tabs[i].panes[0].list);
        free_list(&g_app.tabs[i].panes[1].list);
    }
    endwin();
    return 0;
}
