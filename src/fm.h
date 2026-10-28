#ifndef FM_H
#define FM_H

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
#define THEME_FIELDS 32
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
    PAIR_SCROLL,
    PAIR_BTN_QUIT, PAIR_BTN_MENU, PAIR_BTN_HELP, PAIR_BTN_SETTINGS, PAIR_BTN_BOOKMARKS
};

typedef enum {
    SORT_NAME, SORT_TYPE, SORT_DATE, SORT_SIZE, SORT_COUNT
} SortField;

typedef enum {
    ACT_OPEN, ACT_VIEW, ACT_OPEN_AS, ACT_EDIT, ACT_COPY, ACT_CUT, ACT_PASTE,
    ACT_DELETE, ACT_MKDIR, ACT_TOUCH, ACT_TEMPLATE, ACT_ARCHIVE,
    ACT_UNARCHIVE, ACT_RENAME, ACT_INFO, ACT_PIN, ACT_REFRESH,
    ACT_QUIT, ACT_COUNT
} Action;

typedef struct {
    char   name[MAX_NAME];
    int    is_dir;
    int    is_parent;
    int    selected;
    mode_t mode;
    off_t  size;
    time_t mtime;
    char   icon;
    char   type_name[16];
} Entry;

typedef struct { Entry *items; size_t count; } EntryList;

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

typedef struct { char ext[32]; char cmd[PATH_MAX]; } Association;
typedef struct { char name[24]; int key; } Binding;

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

    /* Для координат кнопок в шапке */
    int g_btn_x_quit, g_btn_w_quit;
    int g_btn_x_menu, g_btn_w_menu;
    int g_btn_x_help, g_btn_w_help;
    int g_btn_x_set,  g_btn_w_set;
    int g_btn_x_book, g_btn_w_book;

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

    /* Цвета кнопок */
    char btn_quit_bg[8], btn_quit_fg[8];
    char btn_menu_bg[8], btn_menu_fg[8];
    char btn_help_bg[8], btn_help_fg[8];
    char btn_set_bg[8],  btn_set_fg[8];
    char btn_book_bg[8], btn_book_fg[8];
} ThemeConfig;

typedef struct { SortField field; int desc; } SortCtx;

typedef struct {
    int show_type, show_date, show_size;
    int name_w;
    int x_name, x_type, x_date, x_size;
} PaneGeom;

/* Global state */
extern AppState    g_app;
extern ThemeConfig g_theme;
extern char        g_theme_raw[4096];
extern int         g_new_theme_seen;

extern int A_PANEL, A_SEL_ACTIVE, A_SEL_INACTIVE, A_DIR, A_EXEC;
extern int A_HEADER_GRAY, A_TITLE, A_TAB_ACTIVE, A_TAB_INACTIVE;
extern int A_MARKED, A_BUTTON, A_BUTTON_SEL, A_MENU_SEL, A_MENU_NORMAL;
extern int A_SCROLL_THUMB, A_SCROLL_TRACK;

#define CURRENT_TAB (g_app.tabs[g_app.active_tab])
#define ACTIVE_PANE (CURRENT_TAB.panes[CURRENT_TAB.active_pane])

/* util.c */
void   copy_str(char *dst, size_t dstsz, const char *src);
int    utf8_strlen(const char *s);
size_t utf8_prefix_bytes(const char *s, int max_chars);
void   mvwaddn_utf8(WINDOW *win, int y, int x, const char *s, int max_chars);
void   build_padded(char *out, size_t outsz, const char *in,
                    int width, int right);
void   format_size(char *out, size_t outsz, off_t size);
void   format_mode(char *out, size_t outsz, mode_t mode);
int    make_path(char *out, size_t outsz, const char *dir,
                 const char *name);
int    name_ok(const char *name);
int    path_parent(char *out, size_t outsz, const char *path);
int    path_is_inside(const char *parent, const char *child);
void   path_display(char *out, size_t outsz, const char *path);
char   file_icon(const char *name, int is_dir, mode_t mode);
const char *type_str(const char *name, int is_dir, mode_t mode);
int    binding_key_by_name(const char *name);
void   key_display_name(int key, char *out, size_t outsz);
int    binding_str_to_key(const char *s);
void   binding_key_to_str(int key, char *out, size_t outsz);
char (*alloc_paths(int n))[PATH_MAX];
void   free_paths(char (*p)[PATH_MAX]);

/* theme.c */
void        default_config(void);
int         color_to_256(const char *hex);
int         theme_color_ok(const char *hex);
char       *theme_field_by_idx(int idx);
const char *theme_name_by_idx(int idx);
void        init_attributes(void);
void        apply_shadow(int y, int x, int h, int w);

/* config.c */
void default_bindings(void);
void load_config(void);
void save_config(void);
void add_bookmark(const char *path);
void add_home_places(const char *home);

/* fs.c */
void do_sort(FilePane *p);
void free_list(EntryList *l);
int  load_dir(FilePane *p);
int  enter_dir(const char *path, FilePane *p);
off_t calc_size(const char *path, int depth);
int  do_copy_recursive(const char *src, const char *dst, CopyProgress *cp, int overwrite);
int  get_selected_or_current(FilePane *p, char paths[][PATH_MAX],
                             int max_paths);
int  fs_delete(const char *target);

/* exec.c */
int  tokenize_cmd(const char *cmd, char **argv, int max_args);
void free_argv(char **argv, int n);
void spawn_detached_argv(char *const argv[]);
/* >=0 — код выхода, -1 — не запустился, -2 — убит сигналом. */
int  spawn_sync(const char *file, char *const argv[], int restore_tty, const char *workdir);
int  get_mime_type(const char *path, char *out, size_t outsz);

/* dialog.c */
void    ui_prepare_win(WINDOW *win);
void    message_box(const char *title, const char *msg);
int     dialog_input(const char *title, const char *prompt,
                     char *out, size_t outsz);
int     dialog_confirm(const char *title, const char *prompt);
void    show_progress(int pct, const char *filename, WINDOW *win);
WINDOW *progress_init(void);

/* pane.c */
void compute_geometry(int x, int w, int show_type, int show_date,
                      int show_size, PaneGeom *g);
void draw_pane_header(FilePane *p, int pane_idx, int x, int w,
                      int show_type, int show_date, int show_size);
void draw_pane(FilePane *p, int pane_idx, int x, int w, int is_active,
               int show_type, int show_date, int show_size,
               const struct tm *now_tm);

/* view.c */
int  is_text_file(const char *name);
int  is_binary_content(const char *path);
void view_file(const char *path);
void show_help(void);

/* actions.c */
void launch_path(const char *name, const char *full);
void action_open(FilePane *p);
void action_view(FilePane *p);
void action_parent(FilePane *p);
void action_open_as(FilePane *p);
void action_edit(FilePane *p);
void action_refresh(FilePane *p);
void action_mkdir(FilePane *p);
void action_touch(FilePane *p);
void action_template(FilePane *p);
void action_archive(FilePane *p);
void action_unarchive(FilePane *p);
void action_copy(FilePane *p);
void action_cut(FilePane *p);
void action_paste(FilePane *p);
void action_delete(FilePane *p);
void action_rename(FilePane *p);
void action_pin(FilePane *p);
void action_info(FilePane *p);
int  execute_action(int act, FilePane *p);

/* menus.c */
int  run_menu(FilePane *p);
int  run_submenu(const char *title, const char **options, int count);
void run_bookmarks_menu(void);
void run_settings_menu(void);

/* draw.c */
void draw(void);

/* keys.c */
int  get_key(int raw_mode);
int  get_mapped_key(WINDOW *win);
void handle_key(int ch);
void handle_mouse(MEVENT ev);

/* app.c */
int  app_run(void);
void app_poll_signals(void);

#endif
