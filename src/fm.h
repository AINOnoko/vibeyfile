/* _GNU_SOURCE объявлен ДО include guard и до первого системного заголовка:
 * glibc раскрывает список видимых функций в момент включения <features.h>,
 * поэтому макрос, объявленный после него, ни на что не влияет (и часть
 * объявлений просто не попадает в наш контекст).
 *
 * Про clang-tidy, bugprone-reserved-identifier: предупреждение формально
 * верное — имя с подчёркиванием зарезервировано за реализацией. Но это не
 * «своё» имя, а публичный feature-макрос glibc: писать _GNU_SOURCE обязан
 * именно пользователь (libc этого не делает), и подчёркивание здесь часть
 * контракта системы. Подавлять нечем — нет ни .clang-tidy, ни NOLINT. */
#define _GNU_SOURCE

#ifndef FM_H
#define FM_H

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

/* ══════════════════════════════════════════════════════════════════════════
 *  ЭТАП 1: FM_PATH_MAX вместо PATH_MAX
 *
 *  PATH_MAX (4096) — лимит ЯДРА, а не реальность: NAME_MAX на любой ФС = 255,
 *  и 99.9% путей короче 1024 байт. В статических массивах AppState 4096 — это
 *  чистый налог на BSS:
 *      clip_paths[256][4096]  = 1 048 576 B
 *      bookmarks[64][4096]    =   262 144 B
 *      associations[64].cmd   =   262 144 B
 *      cwd в 8 панелях        =    32 768 B
 *  Итого ~1.6 МиБ на ОДНУ копию AppState.
 * ═════════════════════════════════════════════════════════════════════════ */
/* Язык по умолчанию: один макрос вместо литерала "ru" по файлам. */
#define DEFAULT_LANG    "ru"

#define FM_PATH_MAX     1024

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
/* 5 МиБ. Скобка (off_t) не для красоты: на платформе с 16-битным int
 * (i386-совместимые ДСП, часть embedded) `5 * 1024 * 1024` переполнило бы
 * int на этапе трансляции и дало отрицательный лимит — «файл слишком
 * большой» на любом файле. Приведение к off_t уводит умножение в 64 бита. */
#define VIEW_MAX_SIZE ((off_t)5 * 1024 * 1024)
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

/* === ЛОКАЛИЗАЦИЯ === */
#define STRINGS(X) \
    X(APP_NAME) X(STATUS_HELP) X(STATUS_SEARCH) X(INFO_PARENT) X(INFO_DIR) \
    X(PREVIEW_TITLE) X(PREVIEW_PARENT) X(PREVIEW_BROKEN) X(PREVIEW_NO_ACCESS) \
    X(PREVIEW_FOLDER) X(PREVIEW_SPECIAL) X(PREVIEW_TOO_BIG) X(PREVIEW_BINARY) \
    X(MSG_ATTENTION) X(MSG_ERROR) X(MSG_NOTHING_SELECTED) X(MSG_CANT_VIEW_PARENT) X(MSG_PATH_TOO_LONG) \
    X(MSG_OPEN_FAILED) X(MSG_OPENED_ASSOC) X(MSG_OPENED) X(MSG_VIEWED) \
    X(OPEN_AS_TITLE) X(OPEN_AS_PROMPT) X(MSG_BAD_COMMAND) X(MSG_NOT_REGULAR) \
    X(MSG_NO_EDITOR) X(MSG_BAD_EDITOR) X(MSG_DIR_CREATED) X(MSG_ERROR_STR) \
    X(MSG_EDITOR_FAILED) X(MSG_EDITOR_KILLED) X(MSG_SPAWN_FAILED) \
    X(MKDIR_TITLE) X(MKDIR_PROMPT) X(MSG_BAD_NAME) X(TOUCH_TITLE) X(TOUCH_PROMPT) \
    X(MSG_FILE_CREATED) X(MSG_FILE_EXISTS) X(TEMPLATE_TITLE) X(MSG_FROM_TEMPLATE) \
    X(TEMPLATE_EMPTY) X(TEMPLATE_PY) X(TEMPLATE_C) X(TEMPLATE_SH) \
    X(TEMPLATE_DEFAULT_TXT) X(TEMPLATE_DEFAULT_PY) X(TEMPLATE_DEFAULT_C) X(TEMPLATE_DEFAULT_SH) \
    X(FORMAT_TITLE) X(FORMAT_ZIP) X(FORMAT_TGZ) X(FORMAT_TXZ) \
    X(ARCHIVE_TITLE) X(ARCHIVE_PROMPT) X(ARCHIVE_CONFIRM) X(ARCHIVE_CREATED) X(ARCHIVE_ERROR) \
    X(UNARCHIVE_UNKNOWN) X(UNARCHIVE_DONE) X(UNARCHIVE_ERROR) \
    X(MSG_COPIED_FILES) X(MSG_NOTHING_TO_COPY) X(MSG_CUT_FILES) X(MSG_NOTHING_TO_CUT) \
    X(MSG_SELF_COPY) X(MSG_PASTED_FILES) X(MSG_COPY_FAILED) X(MSG_CLIPBOARD_EMPTY) \
    X(DELETE_CONFIRM_FILE) X(DELETE_CONFIRM_MANY) X(DELETE_TITLE) X(MSG_DELETED_OK) X(MSG_DELETED_FAILED) \
    X(RENAME_TITLE) X(RENAME_PROMPT) X(RENAME_CONFIRM) X(MSG_RENAMED) X(MSG_RENAME_FAILED) \
    X(PIN_OK) X(INFO_TITLE) X(INFO_TYPE_REG) X(INFO_TYPE_DIR) X(INFO_TYPE_LNK) \
    X(INFO_TYPE_BLK) X(INFO_TYPE_CHR) X(INFO_TYPE_FIFO) X(INFO_TYPE_SOCK) X(INFO_TYPE_UNKNOWN) \
    X(INFO_NAME) X(INFO_PATH) X(INFO_TYPE) X(INFO_MIME) X(INFO_LINK) X(INFO_SIZE) \
    X(INFO_BYTES) X(INFO_PERMS) X(INFO_OWNER) X(INFO_GROUP) X(INFO_LINKS) \
    X(INFO_INODE) X(INFO_DEV) X(INFO_MTIME) X(INFO_ATIME) X(INFO_CTIME) \
    X(ACT_OPEN) X(ACT_VIEW) X(ACT_OPEN_AS) X(ACT_EDIT) X(ACT_COPY) X(ACT_CUT) \
    X(ACT_PASTE) X(ACT_DELETE) X(ACT_MKDIR) X(ACT_TOUCH) X(ACT_TEMPLATE) \
    X(ACT_ARCHIVE) X(ACT_UNARCHIVE) X(ACT_RENAME) X(ACT_INFO) X(ACT_PIN) X(ACT_REFRESH) X(ACT_QUIT) \
    X(MENU_ACTIONS) X(BOOKMARKS_TITLE) X(BOOKMARKS_EMPTY) X(BOOKMARKS_DELETE) X(BOOKMARKS_OPEN_ERR) \
    X(SET_TITLE) X(SET_RESETS) X(SET_RESET_ALL) X(SET_RESET_FAV) X(SET_RESET_THEME) \
    X(SET_RESET_BINDS) X(SET_THEME) X(SET_LANG) X(SET_LOCALIZATION) X(SET_LANG_SELECT) X(MSG_NO_LANG) \
    X(SET_BINDS) X(SET_ASSOC) X(SET_ADD_ASSOC) X(SET_RESET_ALL_CONF) X(SET_RESET_FAV_CONF) \
    X(SET_RESET_THEME_CONF) X(SET_RESET_BINDS_CONF) X(SET_COLOR_TITLE) X(SET_COLOR_PROMPT) \
    X(SET_COLOR_ERR) X(SET_BIND_TITLE) X(SET_BIND_PROMPT) X(SET_BIND_ESC) \
    X(SET_BIND_CONFLICT) X(SET_BIND_CONFLICT_MSG) X(SET_ASSOC_CMD_TITLE) X(SET_ASSOC_CMD_PROMPT) \
    X(SET_ASSOC_ADD_TITLE) X(SET_ASSOC_EXT_PROMPT) X(SET_ASSOC_DEL) X(SET_FOOTER) \
    X(VIEW_TITLE) X(VIEW_HINT) X(VIEW_HINT_H) X(VIEW_ERR_OPEN) X(VIEW_ERR_BIG) \
    X(VIEW_ERR_MEM) X(VIEW_ERR_EMPTY) X(VIEW_HELP_TITLE) X(VIEW_HELP_FOOTER) X(VIEW_HELP_FOOTER_SHORT) \
    X(MSG_ANY_KEY) X(MSG_SMALL_TERM) X(MSG_SMALL_TERM_HINT) X(MSG_PROGRESS_TITLE) X(MSG_PROGRESS_CALC) X(BTN_YES) X(BTN_NO) \
    X(PANE_NAME) X(PANE_TYPE) X(PANE_DATE) X(PANE_SIZE) X(ARROW_UP) X(ARROW_DOWN) \
    X(HELP_BANNER_1) X(HELP_BANNER_2) X(HELP_BANNER_3) X(HELP_BANNER_4) X(HELP_BANNER_5) X(HELP_BANNER_6) \
    X(HELP_NAV_TITLE) X(HELP_NAV_DOWN) X(HELP_NAV_PARENT) X(HELP_NAV_OPEN) X(HELP_NAV_HOME) \
    X(HELP_NAV_TAB) X(HELP_NAV_SELECT) X(HELP_ACT_TITLE) X(HELP_ACT_MENU) X(HELP_ACT_BOOK) \
    X(HELP_ACT_BOOK_INFO) X(HELP_ACT_BOOK_INFO2) X(HELP_ACT_SETT) X(HELP_ACT_SPLIT) X(HELP_ACT_PREV) \
    X(HELP_ACT_SORT) X(HELP_ACT_SEARCH) X(HELP_ACT_REFRESH) X(HELP_ACT_FULL) X(HELP_ACT_TABS) \
    X(HELP_ACT_QUIT) X(HELP_ACT_HELP) X(HELP_KEYBIND_HINT) X(HELP_CREDITS) X(HELP_LICENSE) \
    X(KEY_UP) X(KEY_DOWN) X(KEY_LEFT) X(KEY_RIGHT) X(KEY_HOME) X(KEY_END) \
    X(KEY_PGUP) X(KEY_PGDN) X(KEY_BS) X(KEY_DEL) X(KEY_ESC) X(KEY_ENTER) \
    X(KEY_TAB) X(KEY_SPACE) \
    X(TYPE_DIR) X(TYPE_TEXT) X(TYPE_CODE) X(TYPE_CONF) X(TYPE_SCRIPT) X(TYPE_WEB) \
    X(TYPE_IMG) X(TYPE_AUDIO) X(TYPE_VIDEO) X(TYPE_ARCHIVE) X(TYPE_PDF) X(TYPE_EXEC) X(TYPE_LINK) X(TYPE_FILE)

typedef enum {
#define X(id) STR_##id,
    STRINGS(X)
#undef X
    STR_COUNT
} StrId;

/* === БЕЗОПАСНОСТЬ ФОРМАТА ===
 * Часть строк локализации подставляется в snprintf()/printw() КАК ФОРМАТ
 * (в них есть %s / %d). Файл локализации правит пользователь, поэтому
 * «%» в переводе — это не текст, а спецификатор: строка «100% готово»
 * заставляет snprintf прочитать несуществующий аргумент со стека.
 * FMT_STRINGS перечисляет ровно те ключи, чьи значения уходят в формат;
 * validate_fmt_strings() при загрузке отбрасывает негодные значения. */
#define FMT_STRINGS(X) \
    X(STATUS_HELP, 1) \
    X(STATUS_SEARCH, 1) \
    X(MSG_OPEN_FAILED, 1) \
    X(MSG_OPENED_ASSOC, 1) \
    X(MSG_OPENED, 1) \
    X(MSG_VIEWED, 1) \
    X(MSG_DIR_CREATED, 1) \
    X(MSG_ERROR_STR, 1) \
    X(MSG_FILE_CREATED, 1) \
    X(MSG_EDITOR_FAILED, 1) \
    X(MSG_EDITOR_KILLED, 1) \
    X(MSG_SPAWN_FAILED, 2) \
    X(MSG_FROM_TEMPLATE, 1) \
    X(ARCHIVE_CREATED, 1) \
    X(UNARCHIVE_DONE, 1) \
    X(MSG_COPIED_FILES, 1) \
    X(MSG_CUT_FILES, 1) \
    X(MSG_PASTED_FILES, 1) \
    X(DELETE_CONFIRM_FILE, 1) \
    X(DELETE_CONFIRM_MANY, 1) \
    X(MSG_DELETED_OK, 1) \
    X(MSG_DELETED_FAILED, 2) \
    X(MSG_RENAMED, 2) \
    X(MSG_RENAME_FAILED, 1) \
    X(SET_ASSOC_DEL, 1) \
    X(BOOKMARKS_DELETE, 1) \
    X(SET_BIND_CONFLICT_MSG, 2) \
    X(SET_ASSOC_CMD_TITLE, 0) \
    X(SET_ASSOC_CMD_PROMPT, 0) \
    X(SET_ASSOC_ADD_TITLE, 0) \
    X(SET_ASSOC_EXT_PROMPT, 0) \
    X(VIEW_TITLE, 1) \
    X(VIEW_HINT, 3) \
    X(VIEW_HINT_H, 4) \
    X(VIEW_HELP_FOOTER, 2) \
    X(MSG_SMALL_TERM, 4) \
    X(PANE_NAME, 1) \
    X(PANE_TYPE, 1) \
    X(PANE_DATE, 1) \
    X(PANE_SIZE, 1)

/* Предел числа подстановок в одной строке локализации. */
#define FMT_ARGS_MAX 6

/* Второй параметр X(k, n) — СКОЛЬКО аргументов код передаёт в этом вызове.
 * Без этого «переводчик дописал лишний %s» остаётся незамеченным: синтаксис
 * верный, а snprintf читает аргумент, которого нет. */

/* Ключ входит в FMT_STRINGS? Проверка синтаксиса формата при загрузке. */
int  fmt_key_is_checked(StrId id);
/* Сколько аргументов код передаёт для этого ключа, или -1 (не формат). */
int  fmt_key_arity(StrId id);
/* Отбрасывает значение, негодное как printf-формат. Возвращает
 * 1, если значение ОТБРОШЕНО (тогда вызывающий не должен его применять). */
int  fmt_value_rejected(StrId id, const char *val, char *why, size_t whysz);

extern const char *string_keys[STR_COUNT];

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

/* ══════════════════════════════════════════════════════════════════════════
 *  ЭТАП 2: struct Entry
 *  было: char name[256] + 4 отдельных int + ...  = 312 B (x86-64) / 304 (i386)
 *  стало: char *name (ровно strlen+1 в куче) + битовые поля
 *                                            =  56 B (x86-64) /  48 (i386)
 *  Флаги упакованы в одно слово: 5 бит вместо 4*4 байт.
 * ═════════════════════════════════════════════════════════════════════════ */
typedef struct {
    char   *name;              /* ЭТАП 2: владелец — EntryList, free в free_list() */
    mode_t  mode;
    off_t   size;
    time_t  mtime;
    unsigned is_dir    : 1;
    unsigned is_parent : 1;
    unsigned is_link   : 1;
    unsigned selected  : 1;
    unsigned stat_done : 1;    /* ЭТАП 3: ленивый stat уже выполнен */
    char    icon;
    /* ФИКС: было type_name[16]. Локализованный тип — это UTF-8, где кириллица
     * занимает 2 байта на символ: «Изображение» = 22 байта, «Исполняемый» = 22.
     * 16 байт обрезали их ПОСЕРЕДИНЕ символа → на экране битые байты,
     * а strcmp в cmp_entry_ctx сравнивал обрезанные строки.
     * 48 байт вмещают любой тип в разумном переводе (проверено тестом
     * test_audit_type_name_capacity). Цена на 10 000 файлов: +312 КиБ
     * к прежним 156 КиБ — Entry остаётся 88 байт. */
    char    type_name[48];
} Entry;

typedef struct { Entry *items; size_t count; } EntryList;

typedef struct {
    char      cwd[FM_PATH_MAX];        /* ЭТАП 1: было PATH_MAX */
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

typedef struct { char ext[32]; char cmd[FM_PATH_MAX]; } Association;  /* ЭТАП 1 */
typedef struct { char name[24]; int key; } Binding;

typedef struct {
    TabState tabs[NUM_TABS];
    int active_tab;
    /* ЭТАП 1: 1 048 576 B -> 256*sizeof(char*) = 2 048 B + strdup'ы */
    char **clip_paths;
    int  clip_count;
    int  clip_is_cut;
    int  search_active;
    char search_buf[256];
    int  search_len;
    char bookmarks[MAX_BOOKMARKS][FM_PATH_MAX];   /* ЭТАП 1 */
    int  bookmark_count;
    Association associations[MAX_ASSOC];
    int         assoc_count;
    Binding bindings[MAX_BINDINGS];
    int     binding_count;

    char strings[STR_COUNT][256];
    char lang[16];

    int g_x_name[2], g_x_type[2], g_x_date[2], g_x_size[2];

    /* Для координат кнопок в шапке */
    int g_btn_x_quit, g_btn_w_quit;
    int g_btn_x_menu, g_btn_w_menu;
    int g_btn_x_help, g_btn_w_help;
    int g_btn_x_set,  g_btn_w_set;
    int g_btn_x_book, g_btn_w_book;

    int running;
    int header_scrolling;
    char last_action[FM_PATH_MAX + 128];         /* ЭТАП 1 */
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
/* Таблица «индекс темы → поле». Биндится один раз через theme_bind_fields()
 * сразу после загрузки/сброса темы; theme_field_by_idx() — единственный
 * способ достать поле по индексу (раньше был switch на 33 ветки). */
extern char  *g_theme_field[THEME_FIELDS];
void          theme_bind_fields(void);
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
size_t utf8_truncate(char *dst, size_t dstsz, const char *src);
size_t utf8_prefix_bytes(const char *s, int max_chars);
void   mvwaddn_utf8(WINDOW *win, int y, int x, const char *s, int max_chars);
void   build_padded(char *out, size_t outsz, const char *in,
                    int width, int right);
void   format_size(char *out, size_t outsz, off_t size);
void   format_mode(char *out, size_t outsz, mode_t mode);
int    make_path(char *out, size_t outsz, const char *dir,
                 const char *name);
/* === name_ok(): можно ли использовать строку как имя в текущем каталоге ==
 * Отвергает: пустую строку, "." и "..", длину >= MAX_NAME и > 255,
 * '/', '\\', управляющие символы (<32, 127), а также символы, которые
 * ext4 примет, а FAT/NTFS — нет: : * ? " < > | и точку/пробел в конце.
 *
 * ВАЖНО: это проверка ТОЛЬКО для создания/переименования. Существующие
 * файлы с «непереносимыми» именами остаются видимыми и открываются —
 * иначе менеджер перестал бы показывать чужие каталоги.
 * ================================================================= */
int    name_ok(const char *name);
int    path_parent(char *out, size_t outsz, const char *path);
int    path_is_inside(const char *parent, const char *child);
void   path_display(char *out, size_t outsz, const char *path);
char   file_icon(const char *name, int is_dir, int is_link, mode_t mode);
const char *type_str(const char *name, int is_dir, int is_link, mode_t mode);
int    binding_key_by_name(const char *name);
void   key_display_name(int key, char *out, size_t outsz);
int    binding_str_to_key(const char *s);
void   binding_key_to_str(int key, char *out, size_t outsz);
/* ЭТАП 1: вектор указателей вместо calloc(n, PATH_MAX) */
char **alloc_path_vec(int n);
void   free_path_vec(char **v, int n);
/* === T(): локализованная строка ===================================
 * Возвращает:
 *   - значение из g_app.strings[id], если оно загружено;
 *   - встроенный текст для служебных ключей (имена шаблонов, .zip, Yes/No);
 *   - ИМЯ КЛЮЧА ЦЕЛИКОМ (например "STATUS_HELP") для остальных.
 *
 * Контракт:
 *  1) НИКОГДА не возвращает NULL и не возвращает "" для валидного id.
 *     id вне [0, STR_COUNT) → "".
 *  2) Указатель валиден до следующего load_config()/load_locale_file():
 *     он смотрит в g_app.strings[], а не в выделенную память. Копируйте,
 *     если нужно пережить перезагрузку локализации.
 *  3) Если ключ объявлен в FMT_STRINGS(X, n), значение — printf-формат
 *     ровно с n подстановками. Это проверяется при загрузке
 *     (fmt_value_rejected), поэтому подставлять можно без опаски.
 *  4) Не потокобезопасно и не должно быть: g_app — глобальное состояние
 *     однопоточного ncurses-приложения. gettext/threads не используются.
 * ================================================================= */
const char *T(StrId id);

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
const char *get_lang_dir(void);
void load_locale_file(const char *lang);

/* fs.c */
void do_sort(FilePane *p);
void free_list(EntryList *l);
int  load_dir(FilePane *p);
int  enter_dir(const char *path, FilePane *p);
off_t calc_size(const char *path, int depth);
int  do_copy_recursive(const char *src, const char *dst, CopyProgress *cp, int overwrite);
/* ЭТАП 1+2: выход — вектор strdup'ов; владелец — вызывающий */
int  get_selected_or_current(FilePane *p, char **paths, int max_paths);
/* ЭТАП 3: ленивое дозаполнение size/mtime/mode/icon/type_name */
void entry_fill_stat(FilePane *p, Entry *e);
int  fs_delete(const char *target);

/* exec.c */
/* === tokenize_cmd(): разбор командной строки ======================
 * Делит cmd по пробелам/табам/переводам строк. Возвращает число
 * аргументов (>= 0).
 *
 * ВЛАДЕНИЕ: каждый argv[i] — отдельный strdup(), освобождать free_argv().
 *   argv[max_args - 1] всегда NULL (место под завершающий элемент).
 *   Сам массив argv принадлежит вызывающему (обычно char *argv[MAX_ARGS]).
 *
 * УСЕЧЕНИЕ: если токенов больше, чем max_args - 1, лишние ОТБРАСЫВАЮТСЯ,
 *   а fm_argv_truncated() возвращает 1 до следующего вызова. Вызывающий
 *   ОБЯЗАН это проверять: запуск усечённой команды — ошибка, а не мелочь.
 *
 * ОШИБКА: 0 при пустой строке или неудаче strdup. argv[0] в этом случае NULL.
 *   Статического состояния, кроме флага усечения, нет — реентерабельно
 *   по данным, но не потокобезопасно (общий флаг).
 * ================================================================= */
int  tokenize_cmd(const char *cmd, char **argv, int max_args);
void free_argv(char **argv, int n);
/* Запуск «в фоне»: двойной fork + setsid, stdout/stderr → /dev/null,
 * лишние дескрипторы закрыты. Не ждёт завершения, кода возврата нет —
 * ошибку запуска узнать нельзя по замыслу (xdg-open). */
void spawn_detached_argv(char *const argv[]);
/* >=0 — код выхода, -1 — не запустился, -2 — убит сигналом. */
void fm_flush_input(void);
int  fm_argv_truncated(void);
/* === spawn_sync(): запуск с ожиданием =============================
 * fork + execvp, ждёт завершения. argv не модифицируется и не
 * освобождается (владение — вызывающего); элементы могут быть
 * строковыми литералами. Закрывает лишние дескрипторы в ребёнке
 * (close_extra_fds) — наследников не остаётся.
 *
 * Возвращает:
 *   0..255 — код выхода процесса;
 *   -1     — сбой fork/waitpid в самом менеджере;
 *   -2     — процесс убит сигналом (Ctrl-C в редакторе — это норма);
 *   127    — execvp не нашёл команду в PATH.
 *
 * restore_tty != 0 — погасить ncurses перед запуском и восстановить после.
 * workdir != NULL и непустой — chdir в ребёнке; NULL = cwd не менять
 * (так делает action_edit: файл передаётся полным путём).
 * ================================================================= */
int  spawn_sync(const char *file, char *const argv[], int restore_tty, const char *workdir);
int  get_mime_type(const char *path, char *out, size_t outsz);
/* ЭТАП 4: сниффер «текст/бинарник» по первым 512 байтам, без fork */
int  fm_content_is_binary(const char *path);

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
void view_file(const char *path);
void show_help(void);

/* actions.c */
void clipboard_clear(void);      /* ЭТАП 1: освобождение буфера обмена */
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
int  run_submenu(const char *title, const char **options, int count, int start_sel);
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
