#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <ncurses.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef CTRL
#define CTRL(x) ((x) & 0x1f)
#endif

/* =========================================================================
 * КОНСТАНТЫ И ТИПЫ
 * ========================================================================= */

#define MAX_NAME     256
#define MIN_COLS     80
#define MIN_ROWS     16
#define TYPE_W       8
#define DATE_W      11
#define SIZE_W       8
#define NUM_TABS     4
#define MAX_CLIP     256
#define MAX_BOOKMARKS 64

enum {
    PAIR_SEL_ACTIVE = 1, PAIR_SEL_INACTIVE, PAIR_DIR, PAIR_EXEC,
    PAIR_HEADER, PAIR_TITLE, PAIR_TAB_ACTIVE, PAIR_TAB_INACTIVE,
    PAIR_QUIT, PAIR_QUIT_SEL, PAIR_GRAY_BG, PAIR_MARKED,
    PAIR_BG, PAIR_DIALOG, PAIR_SHADOW,
    PAIR_ROW_EVEN, PAIR_ROW_ODD, PAIR_DIR_EVEN, PAIR_DIR_ODD,
    PAIR_EXEC_EVEN, PAIR_EXEC_ODD
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
} FilePane;

typedef struct {
    FilePane panes[2];
    int active_pane;
    int split_mode;
    int preview_mode;
} TabState;

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
    
    int g_x_name[2], g_x_type[2], g_x_date[2], g_x_size[2];
    int running;
} AppState;

typedef struct {
    off_t total;
    off_t done;
    WINDOW *win;
    const char *disp;
    int last_pct;
} CopyProgress;

/* =========================================================================
 * ГЛОБАЛЬНОЕ СОСТОЯНИЕ И МАКРОСЫ
 * ========================================================================= */

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

/* Прототипы */
static void init_attributes(void);
static int get_key(int raw_mode);
static void handle_key(int ch);
static void handle_mouse(MEVENT ev);
static void switch_tab(int idx);
static void draw(void);
static void load_session(void);
static void save_session(void);
static void add_bookmark(const char *path);
static int enter_dir(const char *path, FilePane *p);

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

static void shell_quote(char *out, size_t outsz, const char *in) {
    size_t o = 0;
    if (o < outsz - 1) out[o++] = '\'';
    for (size_t i = 0; in[i] && o + 6 < outsz; i++) {
        if (in[i] == '\'') { memcpy(out + o, "'\\''", 4); o += 4; }
        else               { out[o++] = in[i]; }
    }
    if (o < outsz - 1) out[o++] = '\'';
    out[o] = '\0';
}

/* =========================================================================
 * СЕССИЯ И ИЗБРАННОЕ
 * ========================================================================= */

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

static void save_session(void) {
    char path[PATH_MAX];
    const char *home = getenv("HOME");
    if (!home) return;
    snprintf(path, sizeof path, "%s/.vibeyfile-h", home);
    
    FILE *f = fopen(path, "w");
    if (!f) return;
    
    for (int i = 0; i < NUM_TABS; i++) {
        if (g_app.tabs[i].panes[0].cwd[0])
            fprintf(f, "TAB:%d:%s\n", i, g_app.tabs[i].panes[0].cwd);
        if (g_app.tabs[i].split_mode && g_app.tabs[i].panes[1].cwd[0])
            fprintf(f, "TAB2:%d:%s\n", i, g_app.tabs[i].panes[1].cwd);
    }
    for (int i = 0; i < g_app.bookmark_count; i++) {
        fprintf(f, "BM:%s\n", g_app.bookmarks[i]);
    }
    fclose(f);
}

static void load_session(void) {
    char path[PATH_MAX];
    const char *home = getenv("HOME");
    if (!home) return;
    snprintf(path, sizeof path, "%s/.vibeyfile-h", home);
    
    FILE *f = fopen(path, "r");
    if (!f) {
        add_bookmark(home);
        add_bookmark("/etc");
        add_bookmark("/usr");
        char cfg[PATH_MAX];
        snprintf(cfg, sizeof cfg, "%s/.config", home);
        add_bookmark(cfg);
        return;
    }
    
    char line[PATH_MAX];
    while (fgets(line, sizeof line, f)) {
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
        
        if (strncmp(line, "TAB:", 4) == 0) {
            int idx = atoi(line + 4);
            char *cwd = strchr(line + 4, ':');
            if (cwd && idx >= 0 && idx < NUM_TABS) {
                copy_str(g_app.tabs[idx].panes[0].cwd, PATH_MAX, cwd + 1);
            }
        } else if (strncmp(line, "TAB2:", 5) == 0) {
            int idx = atoi(line + 5);
            char *cwd = strchr(line + 5, ':');
            if (cwd && idx >= 0 && idx < NUM_TABS) {
                copy_str(g_app.tabs[idx].panes[1].cwd, PATH_MAX, cwd + 1);
                g_app.tabs[idx].split_mode = 1;
            }
        } else if (strncmp(line, "BM:", 3) == 0) {
            add_bookmark(line + 3);
        }
    }
    fclose(f);
}

/* =========================================================================
 * ФАЙЛОВАЯ СИСТЕМА
 * ========================================================================= */

static int cmp_entry(const void *a, const void *b) {
    const Entry *ea = a, *eb = b;
    if (ea->is_parent != eb->is_parent) return eb->is_parent - ea->is_parent;
    if (ea->is_dir != eb->is_dir) return eb->is_dir - ea->is_dir;

    int res = 0;
    SortField field = CURRENT_TAB.panes[0].sort_field;
    int desc = CURRENT_TAB.panes[0].sort_desc;
    
    if (field == SORT_NAME) {
        res = strcmp(ea->name, eb->name);
    } else if (field == SORT_TYPE) {
        const char *ta = type_str(ea->name, ea->is_dir, ea->mode);
        const char *tb = type_str(eb->name, eb->is_dir, eb->mode);
        res = strcmp(ta, tb);
        if (res == 0) res = strcmp(ea->name, eb->name);
    } else if (field == SORT_DATE) {
        if (ea->mtime < eb->mtime) res = -1;
        else if (ea->mtime > eb->mtime) res = 1;
        else res = strcmp(ea->name, eb->name);
    } else if (field == SORT_SIZE) {
        if (ea->size < eb->size) res = -1;
        else if (ea->size > eb->size) res = 1;
        else res = strcmp(ea->name, eb->name);
    }

    if (desc) res = -res;
    return res;
}

static void do_sort(FilePane *p) {
    char keep_name[MAX_NAME] = {0};
    if (p->list.count > 1 && p->file_sel > 0 && p->file_sel < (int)p->list.count) {
        copy_str(keep_name, sizeof keep_name, p->list.items[p->file_sel].name);
    }
    
    CURRENT_TAB.panes[0].sort_field = p->sort_field;
    CURRENT_TAB.panes[0].sort_desc = p->sort_desc;

    if (p->list.count > 1) {
        qsort(p->list.items + 1, p->list.count - 1, sizeof(*p->list.items), cmp_entry);
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
    
    CURRENT_TAB.panes[0].sort_field = p->sort_field;
    CURRENT_TAB.panes[0].sort_desc = p->sort_desc;
    if (n > 1) qsort(items + 1, n - 1, sizeof(*items), cmp_entry);
    
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

static off_t calc_size(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    if (!S_ISDIR(st.st_mode)) return st.st_size;
    DIR *d = opendir(path);
    if (!d) return 0;
    struct dirent *de;
    off_t total = 0;
    while ((de = readdir(d))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        char child[PATH_MAX];
        snprintf(child, sizeof(child), "%s/%s", path, de->d_name);
        total += calc_size(child);
    }
    closedir(d);
    return total;
}

static void do_copy_file(const char *src, const char *dst, CopyProgress *cp) {
    int in = open(src, O_RDONLY);
    if (in < 0) return;
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) { close(in); return; }
    char buf[65536];
    ssize_t n;
    while ((n = read(in, buf, sizeof(buf))) > 0) {
        if (write(out, buf, n) != n) break;
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
    close(in);
    close(out);
}

static void do_copy_recursive(const char *src, const char *dst, CopyProgress *cp) {
    struct stat st;
    if (stat(src, &st) != 0) return;
    if (S_ISDIR(st.st_mode)) {
        mkdir(dst, 0755);
        DIR *d = opendir(src);
        if (!d) return;
        struct dirent *de;
        while ((de = readdir(d))) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
            char s_child[PATH_MAX], d_child[PATH_MAX];
            snprintf(s_child, sizeof(s_child), "%s/%s", src, de->d_name);
            snprintf(d_child, sizeof(d_child), "%s/%s", dst, de->d_name);
            do_copy_recursive(s_child, d_child, cp);
        }
        closedir(d);
    } else {
        do_copy_file(src, dst, cp);
    }
}

static void fs_delete(const char *target) {
    char qtarget[PATH_MAX * 2 + 8];
    shell_quote(qtarget, sizeof qtarget, target);
    char cmd[PATH_MAX * 3 + 32];
    snprintf(cmd, sizeof cmd, "rm -rf -- %s", qtarget);
    def_prog_mode(); endwin(); system(cmd); reset_prog_mode(); refresh();
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
        init_pair(PAIR_SEL_ACTIVE,   COLOR_WHITE, COLOR_BLUE);
        init_pair(PAIR_SEL_INACTIVE, COLOR_BLACK, COLOR_CYAN);
        init_pair(PAIR_HEADER,       COLOR_WHITE, COLOR_BLACK);
        init_pair(PAIR_TITLE,        COLOR_WHITE, COLOR_BLUE);
        init_pair(PAIR_TAB_ACTIVE,   COLOR_WHITE, COLOR_BLUE);
        init_pair(PAIR_TAB_INACTIVE, COLOR_WHITE, COLOR_BLACK);
        init_pair(PAIR_QUIT,         COLOR_WHITE, COLOR_RED);
        init_pair(PAIR_QUIT_SEL,     COLOR_YELLOW, COLOR_RED);
        init_pair(PAIR_GRAY_BG,      COLOR_BLACK, COLOR_WHITE);
        init_pair(PAIR_MARKED,       COLOR_BLACK, COLOR_YELLOW);
        init_pair(PAIR_BG,           COLOR_BLACK, COLOR_WHITE);
        init_pair(PAIR_DIALOG,       COLOR_BLACK, COLOR_WHITE);
        init_pair(PAIR_SHADOW,       COLOR_WHITE, COLOR_BLACK);

        if (COLORS >= 256) {
            init_pair(PAIR_ROW_EVEN, COLOR_BLACK, 255); // Bright white
            init_pair(PAIR_ROW_ODD,  COLOR_BLACK, 254); // Light gray
            init_pair(PAIR_DIR_EVEN, COLOR_BLUE,  255);
            init_pair(PAIR_DIR_ODD,  COLOR_BLUE,  254);
            init_pair(PAIR_EXEC_EVEN,COLOR_GREEN, 255);
            init_pair(PAIR_EXEC_ODD, COLOR_GREEN, 254);
        } else {
            init_pair(PAIR_ROW_EVEN, COLOR_BLACK, COLOR_WHITE);
            init_pair(PAIR_ROW_ODD,  COLOR_WHITE, COLOR_BLACK);
            init_pair(PAIR_DIR_EVEN, COLOR_CYAN,  COLOR_WHITE);
            init_pair(PAIR_DIR_ODD,  COLOR_CYAN,  COLOR_BLACK);
            init_pair(PAIR_EXEC_EVEN,COLOR_GREEN, COLOR_WHITE);
            init_pair(PAIR_EXEC_ODD, COLOR_GREEN, COLOR_BLACK);
        }

        A_PANEL        = A_NORMAL;
        A_SEL_ACTIVE   = COLOR_PAIR(PAIR_SEL_ACTIVE) | A_BOLD;
        A_SEL_INACTIVE = COLOR_PAIR(PAIR_SEL_INACTIVE);
        A_DIR          = A_BOLD;
        A_EXEC         = A_BOLD;
        A_HEADER_GRAY  = COLOR_PAIR(PAIR_GRAY_BG) | A_BOLD;
        A_TITLE        = COLOR_PAIR(PAIR_TITLE) | A_BOLD;
        A_TAB_ACTIVE   = COLOR_PAIR(PAIR_TAB_ACTIVE) | A_BOLD;
        A_TAB_INACTIVE = COLOR_PAIR(PAIR_TAB_INACTIVE);
        A_MARKED       = COLOR_PAIR(PAIR_MARKED) | A_BOLD;
        A_BUTTON       = A_NORMAL;
        A_BUTTON_SEL   = A_REVERSE | A_BOLD;
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
            }
        }
        return 0;
    }
    return ch;
}

static void message_box(const char *title, const char *msg) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int w = cols - 10;
    if (w > 60) w = 60;
    if (w < 40) w = 40;
    int h = 9;
    int y = (rows - h) / 2, x = (cols - w) / 2;
    
    apply_shadow(y, x, h, w);
    WINDOW *win = newwin(h, w, y, x);
    wbkgd(win, COLOR_PAIR(PAIR_DIALOG));
    keypad(win, TRUE);
    box(win, 0, 0);
    mvwaddnstr(win, 0, 2, title, w - 4);
    
    int line = 2;
    const char *p = msg;
    while (*p && line < h - 2) {
        int len = 0;
        while (p[len] && p[len] != '\n' && len < w - 4) len++;
        mvwaddnstr(win, line, 2, p, len);
        p += len;
        if (*p == '\n') p++;
        line++;
    }
    mvwaddnstr(win, h - 2, 2, "Нажмите любую клавишу...", w - 4);
    wrefresh(win);
    wgetch(win);
    delwin(win);
    touchwin(stdscr);
    refresh();
}

static void show_help(void) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int w = 70, h = 24;
    if (w > cols - 4) w = cols - 4;
    if (h > rows - 4) h = rows - 4;
    int y = (rows - h) / 2, x = (cols - w) / 2;

    apply_shadow(y, x, h, w);
    WINDOW *win = newwin(h, w, y, x);
    wbkgd(win, COLOR_PAIR(PAIR_DIALOG));
    keypad(win, TRUE);
    box(win, 0, 0);
    mvwaddnstr(win, 0, 2, " Справка ", w - 4);

    int cy = 2;
    mvwprintw(win, cy++, 2, "Навигация:");
    mvwprintw(win, cy++, 4, "j/↓, k/↑     - Вверх / Вниз");
    mvwprintw(win, cy++, 4, "h/←          - Вверх по каталогам");
    mvwprintw(win, cy++, 4, "l/→/Enter    - Войти в каталог / Открыть файл");
    mvwprintw(win, cy++, 4, "g / G        - В начало / В конец списка");
    mvwprintw(win, cy++, 4, "Tab          - Переключение панелей (в режиме сплита)");
    mvwprintw(win, cy++, 4, "Space        - Выделить / Снять выделение файла");

    cy++;
    mvwprintw(win, cy++, 2, "Действия и вкладки:");
    mvwprintw(win, cy++, 4, "a            - Открыть меню действий");
    mvwprintw(win, cy++, 4, "p            - Открыть избранное");
    mvwprintw(win, cy++, 4, "m            - Вкл/выкл режим сплита");
    mvwprintw(win, cy++, 4, "v            - Вкл/выкл режим предпросмотра");
    mvwprintw(win, cy++, 4, "s / S        - Сортировка по колонкам / Реверс");
    mvwprintw(win, cy++, 4, "/            - Поиск по имени");
    mvwprintw(win, cy++, 4, "F1..F4       - Переключение вкладок");
    mvwprintw(win, cy++, 4, "r            - Обновить список");
    mvwprintw(win, cy++, 4, "q            - Выход");
    mvwprintw(win, cy++, 4, "i            - Эта справка");

    mvwaddnstr(win, h - 2, 2, "Нажмите любую клавишу для выхода...", w - 4);
    wrefresh(win);
    wgetch(win);
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
        if (ch == 'y' || ch == 'Y' || ch == 0xD0 || ch == 0xD1) {
            if (ch == 'y' || ch == 'Y') { res = 1; running = 0; }
            else if (ch == 0xD0) { int c2 = wgetch(win); if (c2 == 0xBD || c2 == 0x9D) { res = 1; running = 0; } }
            else if (ch == 0xD1) { int c2 = wgetch(win); if (c2 == 0x82 || c2 == 0xA2) { res = 0; running = 0; } }
        }
        else if (ch == 'n' || ch == 'N' || ch == 27) { res = 0; running = 0; }
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

static void draw_pane_header(FilePane *p, int x, int w, int show_type, int show_date, int show_size) {
    int name_w = w - 6 - 1;
    if (show_size) name_w -= (SIZE_W + 1);
    if (show_date) name_w -= (DATE_W + 1);
    if (show_type) name_w -= (TYPE_W + 1);
    if (name_w < 4) name_w = 4;

    int x_icon = x + 2;
    int x_name = x + 6;
    int x_type = show_type ? x_name + name_w + 1 : -1;
    int x_date = show_date ? (show_type ? x_type + TYPE_W + 1 : x_name + name_w + 1) : -1;
    int x_size = show_size ? x + w - SIZE_W : -1;

    attron(A_HEADER_GRAY);
    mvhline(1, x, ' ', w);
    
    char h_name[64], h_type[64], h_date[64], h_size[64];
    const char *arrow = p->sort_desc ? "↓" : "↑";
    snprintf(h_name, sizeof h_name, "Имя %s", (p->sort_field == SORT_NAME) ? arrow : " ");
    snprintf(h_type, sizeof h_type, "Тип %s", (p->sort_field == SORT_TYPE) ? arrow : " ");
    snprintf(h_date, sizeof h_date, "Изменён %s", (p->sort_field == SORT_DATE) ? arrow : " ");
    snprintf(h_size, sizeof h_size, "Размер %s", (p->sort_field == SORT_SIZE) ? arrow : " ");

    mvaddstr(1, x_icon, "[X]");
    mvaddnstr(1, x_name, h_name, name_w);
    if (show_type) mvaddnstr(1, x_type, h_type, TYPE_W);
    if (show_date) mvaddnstr(1, x_date, h_date, DATE_W);
    if (show_size) mvaddnstr(1, x_size, h_size, SIZE_W);
    attroff(A_HEADER_GRAY);
}

static void draw_pane_row(FilePane *p, int x, int y, int w, int idx, int is_active, int show_type, int show_date, int show_size) {
    Entry *e = &p->list.items[idx];
    
    int name_w = w - 6 - 1;
    if (show_size) name_w -= (SIZE_W + 1);
    if (show_date) name_w -= (DATE_W + 1);
    if (show_type) name_w -= (TYPE_W + 1);
    if (name_w < 4) name_w = 4;

    int attr;
    if (idx == p->file_sel && is_active)     attr = A_SEL_ACTIVE;
    else if (idx == p->file_sel && !is_active) attr = A_SEL_INACTIVE;
    else if (e->selected)                    attr = A_MARKED;
    else {
        int base_pair = (idx % 2 == 0) ? PAIR_ROW_EVEN : PAIR_ROW_ODD;
        if (e->is_dir || e->is_parent)
            base_pair = (idx % 2 == 0) ? PAIR_DIR_EVEN : PAIR_DIR_ODD;
        else if (e->mode & (S_IXUSR | S_IXGRP | S_IXOTH))
            base_pair = (idx % 2 == 0) ? PAIR_EXEC_EVEN : PAIR_EXEC_ODD;
        
        attr = COLOR_PAIR(base_pair);
        if (e->is_dir || (e->mode & (S_IXUSR | S_IXGRP | S_IXOTH))) attr |= A_BOLD;
    }

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
    build_padded(name_padded, sizeof name_padded, name_disp,  name_w,  0);
    build_padded(type_padded, sizeof type_padded, tname,       TYPE_W,  0);
    build_padded(date_padded, sizeof date_padded, date_str,    DATE_W,  0);
    build_padded(size_padded, sizeof size_padded, size_str,    SIZE_W,  1);

    char mark_char = (idx == p->file_sel) ? '>' : (e->selected ? '*' : ' ');
    char row[2048];
    int pos = snprintf(row, sizeof row, "%c [%c] %s", mark_char, icon, name_padded);
    if (show_type) pos += snprintf(row + pos, sizeof row - pos, " %s", type_padded);
    if (show_date) pos += snprintf(row + pos, sizeof row - pos, " %s", date_padded);
    if (show_size) pos += snprintf(row + pos, sizeof row - pos, " %s", size_padded);

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

static void draw_pane(FilePane *p, int x, int w, int is_active) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    (void)cols;

    int show_type = (w >= 40);
    int show_date = (w >= 55);
    int show_size = (w >= 28);

    draw_pane_header(p, x, w, show_type, show_date, show_size);

    int items_y = 2;
    int vis = rows - 3;
    if (vis < 1) vis = 1;

    if (p->file_top < 0) p->file_top = 0;
    if (p->file_sel >= (int)p->list.count) p->file_sel = p->list.count ? (int)p->list.count - 1 : 0;
    if (p->file_sel < 0) p->file_sel = 0;
    if (p->file_sel < p->file_top) p->file_top = p->file_sel;
    if (p->file_sel >= p->file_top + vis) p->file_top = p->file_sel - vis + 1;

    for (int i = 0; i < vis; i++) {
        int idx = p->file_top + i;
        if (idx >= (int)p->list.count) break;
        draw_pane_row(p, x, items_y + i, w, idx, is_active, show_type, show_date, show_size);
    }
}

static void draw_preview(int x, int w, int rows) {
    FilePane *src = &CURRENT_TAB.panes[0];
    int idx = src->file_sel;
    
    attron(A_HEADER_GRAY);
    mvhline(1, x, ' ', w);
    mvaddstr(1, x + 2, "Предпросмотр");
    attroff(A_HEADER_GRAY);

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
    int max_lines = rows - 3;
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

static void draw(void) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    erase();

    attron(COLOR_PAIR(PAIR_TITLE) | A_BOLD);
    mvhline(0, 0, ' ', cols);
    mvprintw(0, 1, " fm: %s | i: справка", ACTIVE_PANE.cwd);
    mvprintw(0, cols - 4, "[X]");
    attroff(COLOR_PAIR(PAIR_TITLE) | A_BOLD);

    if (CURRENT_TAB.split_mode) {
        int half = cols / 2;
        draw_pane(&CURRENT_TAB.panes[0], 0, half, CURRENT_TAB.active_pane == 0);
        if (CURRENT_TAB.preview_mode) {
            draw_preview(half + 1, cols - half - 1, rows);
        } else {
            draw_pane(&CURRENT_TAB.panes[1], half + 1, cols - half - 1, CURRENT_TAB.active_pane == 1);
        }
        attron(COLOR_PAIR(PAIR_TITLE) | A_BOLD);
        for (int y = 1; y < rows - 1; y++) mvaddch(y, half, ACS_VLINE);
        attroff(COLOR_PAIR(PAIR_TITLE) | A_BOLD);
    } else {
        draw_pane(&ACTIVE_PANE, 0, cols, 1);
    }

    draw_tabs(rows, cols);
    refresh();
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

    char qname[PATH_MAX * 2 + 8];
    shell_quote(qname, sizeof qname, e->name);
    char cmd[PATH_MAX * 3 + 64];
    snprintf(cmd, sizeof cmd, "xdg-open %s", qname);
    def_prog_mode(); endwin(); system(cmd); reset_prog_mode(); refresh();
}

static void action_open_as(FilePane *p) {
    if (p->list.count == 0 || p->file_sel == 0) { message_box("Внимание", "Ничего не выбрано"); return; }
    Entry *e = &p->list.items[p->file_sel];
    if (e->is_parent) { message_box("Ошибка", "Нельзя открыть .."); return; }
    
    char cmd_buf[256];
    snprintf(cmd_buf, sizeof cmd_buf, "xdg-open");
    if (dialog_input("Открыть как", "Команда:", cmd_buf, sizeof cmd_buf)) {
        char target[PATH_MAX];
        if (make_path(target, sizeof target, p->cwd, e->name) == 0) {
            char qname[PATH_MAX * 2 + 8];
            shell_quote(qname, sizeof qname, target);
            char cmd[PATH_MAX * 3 + 64];
            snprintf(cmd, sizeof cmd, "%s %s", cmd_buf, qname);
            def_prog_mode(); endwin(); system(cmd); reset_prog_mode(); refresh();
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
            char *dir = strtok(path_copy, ":");
            char full[PATH_MAX];
            while (dir) {
                snprintf(full, sizeof full, "%s/%s", dir, editors[i]);
                if (access(full, X_OK) == 0) { ed = editors[i]; break; }
                dir = strtok(NULL, ":");
            }
            free(path_copy);
            if (ed) break;
        }
    }
    
    if (!ed) { message_box("Ошибка", "Редактор не найден"); return; }

    char qname[PATH_MAX * 2 + 8];
    shell_quote(qname, sizeof qname, e->name);
    char cmd[PATH_MAX * 3 + 64];
    snprintf(cmd, sizeof cmd, "%s %s", ed, qname);
    def_prog_mode(); endwin(); system(cmd); reset_prog_mode(); refresh();
    
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
            if (mkdir(target, 0755) == 0) { message_box("Успех", "Папка создана"); load_dir(p); }
            else { message_box("Ошибка", "Не удалось создать папку"); }
        }
    }
}

static void action_touch(FilePane *p) {
    char name[MAX_NAME] = "Новый файл.txt";
    if (dialog_input("Создать файл", "Имя файла:", name, sizeof name)) {
        char target[PATH_MAX];
        if (make_path(target, sizeof target, p->cwd, name) == 0) {
            FILE *f = fopen(target, "w");
            if (f) { fclose(f); message_box("Успех", "Файл создан"); load_dir(p); }
            else { message_box("Ошибка", "Не удалось создать файл"); }
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
                message_box("Успех", "Файл создан из шаблона");
                load_dir(p);
            } else { message_box("Ошибка", "Не удалось создать"); }
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

    char qarch[PATH_MAX * 2 + 8];
    shell_quote(qarch, sizeof qarch, archname);

    char cmd[PATH_MAX * 5 + 64];
    if (fmt == 0) snprintf(cmd, sizeof cmd, "zip -r %s", qarch);
    else if (fmt == 1) snprintf(cmd, sizeof cmd, "tar -czf %s", qarch);
    else snprintf(cmd, sizeof cmd, "tar -cJf %s", qarch);

    for (int i = 0; i < count; i++) {
        const char *base = strrchr(paths[i], '/');
        char qbase[PATH_MAX * 2 + 8];
        shell_quote(qbase, sizeof qbase, base ? base + 1 : paths[i]);
        snprintf(cmd + strlen(cmd), sizeof cmd - strlen(cmd), " %s", qbase);
    }
    
    int rc = system(cmd);
    if (rc == 0) { load_dir(p); message_box("Успех", "Архив создан"); }
    else { message_box("Ошибка", "Сбой архивации"); }
}

static void action_unarchive(FilePane *p) {
    if (p->list.count == 0 || p->file_sel == 0) { message_box("Внимание", "Ничего не выбрано"); return; }
    Entry *e = &p->list.items[p->file_sel];
    const char *dot = strrchr(e->name, '.');
    if (!dot || e->is_dir) { message_box("Ошибка", "Не архив"); return; }

    char qname[PATH_MAX * 2 + 8];
    shell_quote(qname, sizeof qname, e->name);
    char cmd[PATH_MAX * 3 + 64];
    
    if (!strcasecmp(dot, ".zip")) snprintf(cmd, sizeof cmd, "unzip -o %s", qname);
    else if (!strcasecmp(dot, ".gz")) snprintf(cmd, sizeof cmd, "tar -xzf %s", qname);
    else if (!strcasecmp(dot, ".xz")) snprintf(cmd, sizeof cmd, "tar -xJf %s", qname);
    else if (!strcasecmp(dot, ".bz2")) snprintf(cmd, sizeof cmd, "tar -xjf %s", qname);
    else if (!strcasecmp(dot, ".tar")) snprintf(cmd, sizeof cmd, "tar -xf %s", qname);
    else { message_box("Ошибка", "Неизвестный формат"); return; }

    int rc = system(cmd);
    if (rc == 0) { load_dir(p); message_box("Успех", "Разархивировано"); }
    else { message_box("Ошибка", "Сбой разархивации"); }
}

static void action_copy(FilePane *p) {
    g_app.clip_count = get_selected_or_current(p, g_app.clip_paths, MAX_CLIP);
    if (g_app.clip_count > 0) {
        g_app.clip_is_cut = 0;
        message_box("Буфер обмена", "Скопировано");
    } else message_box("Внимание", "Нечего копировать");
}

static void action_cut(FilePane *p) {
    g_app.clip_count = get_selected_or_current(p, g_app.clip_paths, MAX_CLIP);
    if (g_app.clip_count > 0) {
        g_app.clip_is_cut = 1;
        message_box("Буфер обмена", "Вырезано");
    } else message_box("Внимание", "Нечего вырезать");
}

static void action_paste(FilePane *p) {
    if (g_app.clip_count == 0) { message_box("Внимание", "Буфер пуст"); return; }
    
    for (int i = 0; i < g_app.clip_count; i++) {
        const char *base = strrchr(g_app.clip_paths[i], '/');
        base = base ? base + 1 : g_app.clip_paths[i];
        char dst[PATH_MAX], new_dst[PATH_MAX];
        
        if (make_path(dst, sizeof dst, p->cwd, base) == 0) {
            if (strcmp(g_app.clip_paths[i], dst) == 0 || access(dst, F_OK) == 0) {
                char new_name[MAX_NAME];
                snprintf(new_name, sizeof new_name, "%s_copy", base);
                make_path(new_dst, sizeof new_dst, p->cwd, new_name);
            } else copy_str(new_dst, sizeof new_dst, dst);

            if (g_app.clip_is_cut && rename(g_app.clip_paths[i], new_dst) == 0) continue;

            off_t total = calc_size(g_app.clip_paths[i]);
            CopyProgress cp = {total, 0, progress_init(), base, -1};
            show_progress(0, cp.disp, cp.win);
            do_copy_recursive(g_app.clip_paths[i], new_dst, &cp);
            delwin(cp.win);
            if (g_app.clip_is_cut) fs_delete(g_app.clip_paths[i]);
        }
    }
    if (g_app.clip_is_cut) g_app.clip_count = 0;
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
        load_dir(p);
        message_box("Успех", "Удалено");
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
            if (rename(src, dst) == 0) { load_dir(p); message_box("Успех", "Готово"); }
            else message_box("Ошибка", "Сбой переименования");
        }
    }
}

static void action_pin(FilePane *p) {
    if (p->cwd[0]) {
        add_bookmark(p->cwd);
        save_session();
        message_box("Успех", "Добавлено в избранное");
    }
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
    case ACT_INFO:
        if (p->list.count && p->file_sel < (int)p->list.count) {
            char sizebuf[32], timebuf[64] = "?";
            struct stat st;
            char full[PATH_MAX];
            if (make_path(full, sizeof full, p->cwd, p->list.items[p->file_sel].name) == 0 && stat(full, &st) == 0) {
                format_size(sizebuf, sizeof sizebuf, st.st_size);
                struct tm tmv;
                if (localtime_r(&st.st_mtime, &tmv)) strftime(timebuf, sizeof timebuf, "%Y-%m-%d %H:%M:%S", &tmv);
                char info[1024];
                char name_trunc[64];
                snprintf(name_trunc, sizeof name_trunc, "%.60s", p->list.items[p->file_sel].name);
                snprintf(info, sizeof info, "Имя:   %s\nРазмер: %s\nИзменён: %s", 
                         name_trunc, sizebuf, timebuf);
                message_box("Свойства", info);
            }
        } else message_box("Внимание", "Ничего не выбрано");
        break;
    default: break;
    }
    return 1;
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
            int attr = (i == sel) ? A_SEL_ACTIVE : A_PANEL;
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
    int menu_w = max_len + 6, menu_h = ACT_COUNT + 2;
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int start_x = (cols - menu_w) / 2, start_y = (rows - menu_h) / 2;
    
    apply_shadow(start_y, start_x, menu_h, menu_w);
    WINDOW *win = newwin(menu_h, menu_w, start_y, start_x);
    wbkgd(win, COLOR_PAIR(PAIR_DIALOG));
    keypad(win, TRUE);
    box(win, 0, 0);
    
    int sel = 0, ch, running = 1, res = 1;
    while (running) {
        werase(win);
        box(win, 0, 0);
        
        wattron(win, A_HEADER_GRAY);
        mvwhline(win, 0, 1, ' ', menu_w - 2);
        mvwprintw(win, 0, (menu_w - utf8_strlen("Действия")) / 2, "Действия");
        wattroff(win, A_HEADER_GRAY);
        
        for (int i = 0; i < ACT_COUNT; i++) {
            int attr = (i == sel) ? A_SEL_ACTIVE : A_PANEL;
            wattron(win, attr);
            mvwhline(win, 1 + i, 1, ' ', menu_w - 2);
            mvwprintw(win, 1 + i, 2, "%-*s", menu_w - 4, ACTION_NAMES[i]);
            wattroff(win, attr);
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
                    int clicked = ev.y - start_y - 1;
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
            int attr = (idx == sel) ? A_SEL_ACTIVE : A_PANEL;
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
            enter_dir(g_app.bookmarks[sel], &ACTIVE_PANE);
            running = 0;
        } else if (ch == 'd' || ch == 'D') {
            char msg[PATH_MAX + 64];
            snprintf(msg, sizeof msg, "Удалить закладку %.100s?", g_app.bookmarks[sel]);
            if (dialog_confirm("Удаление", msg)) {
                for (int i = sel; i < g_app.bookmark_count - 1; i++) {
                    copy_str(g_app.bookmarks[i], PATH_MAX, g_app.bookmarks[i+1]);
                }
                g_app.bookmark_count--;
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
                            enter_dir(g_app.bookmarks[sel], &ACTIVE_PANE);
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
    
    if (ACTIVE_PANE.cwd[0] == '\0') {
        const char *home = getenv("HOME");
        copy_str(ACTIVE_PANE.cwd, sizeof ACTIVE_PANE.cwd, home ? home : "/");
        load_dir(&ACTIVE_PANE);
    }
}

static void handle_mouse(MEVENT ev) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    if (ev.y == 0 && ev.x >= cols - 4) {
        g_app.running = 0;
        return;
    }

    if (ev.bstate & BUTTON3_CLICKED) {
        if (!run_menu(&ACTIVE_PANE)) g_app.running = 0;
    } else if (ev.bstate & (BUTTON1_CLICKED | BUTTON1_PRESSED | BUTTON1_DOUBLE_CLICKED)) {
        if (ev.y == rows - 1) {
            int tab_idx = ev.x / (cols / NUM_TABS);
            if (tab_idx >= 0 && tab_idx < NUM_TABS) switch_tab(tab_idx);
        }
        else if (ev.y == 1) {
            int pane_idx = (CURRENT_TAB.split_mode && ev.x >= cols / 2) ? 1 : 0;
            FilePane *p = &CURRENT_TAB.panes[pane_idx];
            int new_field = -1;
            
            if (ev.x >= g_app.g_x_name[pane_idx] && (g_app.g_x_type[pane_idx] == -1 || ev.x < g_app.g_x_type[pane_idx])) new_field = SORT_NAME;
            else if (g_app.g_x_type[pane_idx] != -1 && ev.x >= g_app.g_x_type[pane_idx] && (g_app.g_x_date[pane_idx] == -1 || ev.x < g_app.g_x_date[pane_idx])) new_field = SORT_TYPE;
            else if (g_app.g_x_date[pane_idx] != -1 && ev.x >= g_app.g_x_date[pane_idx] && ev.x < g_app.g_x_size[pane_idx]) new_field = SORT_DATE;
            else if (g_app.g_x_size[pane_idx] != -1 && ev.x >= g_app.g_x_size[pane_idx]) new_field = SORT_SIZE;

            if (new_field != -1) {
                if (p->sort_field == (SortField)new_field) p->sort_desc = !p->sort_desc;
                else { p->sort_field = (SortField)new_field; p->sort_desc = 0; }
                do_sort(p);
            }
        }
        else if (ev.y >= 2 && ev.y < rows - 1) {
            int pane_idx = (CURRENT_TAB.split_mode && ev.x >= cols / 2) ? 1 : 0;
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
    int vis = rows - 3;
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

    switch (ch) {
    case 'q': g_app.running = 0; break;
    case 'a': case 'A': if (!run_menu(&ACTIVE_PANE)) g_app.running = 0; break;
    case 'p': case 'P': run_bookmarks_menu(); break;
    case 'i': case 'I': show_help(); break;
    case KEY_F(1): switch_tab(0); break;
    case KEY_F(2): switch_tab(1); break;
    case KEY_F(3): switch_tab(2); break;
    case KEY_F(4): switch_tab(3); break;
    case '/': g_app.search_active = 1; g_app.search_len = 0; g_app.search_buf[0] = 0; break;
    case 's': ACTIVE_PANE.sort_field = (ACTIVE_PANE.sort_field + 1) % SORT_COUNT; do_sort(&ACTIVE_PANE); break;
    case 'S': ACTIVE_PANE.sort_desc = !ACTIVE_PANE.sort_desc; do_sort(&ACTIVE_PANE); break;
    
    case 'm': case 'M':
        CURRENT_TAB.split_mode = !CURRENT_TAB.split_mode;
        if (!CURRENT_TAB.split_mode) CURRENT_TAB.preview_mode = 0;
        if (CURRENT_TAB.split_mode && CURRENT_TAB.panes[1].cwd[0] == '\0') {
            copy_str(CURRENT_TAB.panes[1].cwd, sizeof CURRENT_TAB.panes[1].cwd, ACTIVE_PANE.cwd);
            load_dir(&CURRENT_TAB.panes[1]);
        }
        break;
        
    case 'v': case 'V':
        CURRENT_TAB.preview_mode = !CURRENT_TAB.preview_mode;
        if (CURRENT_TAB.preview_mode) CURRENT_TAB.split_mode = 1;
        break;

    case ' ':
        if (ACTIVE_PANE.file_sel > 0 && ACTIVE_PANE.file_sel < (int)ACTIVE_PANE.list.count) {
            ACTIVE_PANE.list.items[ACTIVE_PANE.file_sel].selected ^= 1;
            if (ACTIVE_PANE.file_sel + 1 < (int)ACTIVE_PANE.list.count) ACTIVE_PANE.file_sel++;
        }
        break;

    case '\t': if (CURRENT_TAB.split_mode && !CURRENT_TAB.preview_mode) CURRENT_TAB.active_pane ^= 1; break;
    case KEY_LEFT: if (CURRENT_TAB.split_mode) CURRENT_TAB.active_pane = 0; break;
    case KEY_RIGHT: if (CURRENT_TAB.split_mode && !CURRENT_TAB.preview_mode) CURRENT_TAB.active_pane = 1; break;
    case KEY_HOME: ACTIVE_PANE.file_sel = 0; ACTIVE_PANE.file_top = 0; break;
    case KEY_END: ACTIVE_PANE.file_sel = ACTIVE_PANE.list.count ? (int)ACTIVE_PANE.list.count - 1 : 0; break;
    case 'j': case KEY_DOWN: if (ACTIVE_PANE.file_sel + 1 < (int)ACTIVE_PANE.list.count) ACTIVE_PANE.file_sel++; break;
    case 'k': case KEY_UP: if (ACTIVE_PANE.file_sel > 0) ACTIVE_PANE.file_sel--; break;
    case 'g': ACTIVE_PANE.file_sel = 0; ACTIVE_PANE.file_top = 0; break;
    case 'G': ACTIVE_PANE.file_sel = ACTIVE_PANE.list.count ? (int)ACTIVE_PANE.list.count - 1 : 0; break;
    case CTRL('d'): case KEY_NPAGE:
        ACTIVE_PANE.file_sel += vis / 2;
        if (ACTIVE_PANE.file_sel >= (int)ACTIVE_PANE.list.count) ACTIVE_PANE.file_sel = ACTIVE_PANE.list.count ? (int)ACTIVE_PANE.list.count - 1 : 0;
        break;
    case CTRL('u'): case KEY_PPAGE:
        ACTIVE_PANE.file_sel -= vis / 2;
        if (ACTIVE_PANE.file_sel < 0) ACTIVE_PANE.file_sel = 0;
        break;
    case '\n': case KEY_ENTER: case 'l':
        if (!CURRENT_TAB.preview_mode || CURRENT_TAB.active_pane == 0) action_open(&ACTIVE_PANE);
        break;
    case 'h': case 127: case KEY_BACKSPACE:
        if (!CURRENT_TAB.preview_mode || CURRENT_TAB.active_pane == 0) action_open(&ACTIVE_PANE);
        break;
    case 'r': action_refresh(&ACTIVE_PANE); break;
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

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    init_attributes();
    wbkgd(stdscr, COLOR_PAIR(PAIR_BG));
    
    mousemask(BUTTON1_CLICKED | BUTTON1_PRESSED | BUTTON1_DOUBLE_CLICKED |
              BUTTON3_CLICKED | BUTTON3_PRESSED | BUTTON4_PRESSED | BUTTON5_PRESSED, NULL);

    load_session();
    
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
        } else {
            handle_key(ch);
        }
    }

    save_session();
    
    for (int i = 0; i < NUM_TABS; i++) {
        free_list(&g_app.tabs[i].panes[0].list);
        free_list(&g_app.tabs[i].panes[1].list);
    }
    endwin();
    return 0;
}
