#include "fm.h"

AppState    g_app;
ThemeConfig g_theme;
char *g_theme_field[THEME_FIELDS];
char        g_theme_raw[4096] = "";
int         g_new_theme_seen = 0;

int A_PANEL        = 0;
int A_SEL_ACTIVE   = A_REVERSE | A_BOLD;
int A_SEL_INACTIVE = A_REVERSE;
int A_DIR          = A_BOLD;
int A_EXEC         = A_BOLD;
int A_HEADER_GRAY  = A_REVERSE | A_BOLD;
int A_TITLE        = A_REVERSE | A_BOLD;
int A_TAB_ACTIVE   = A_REVERSE | A_BOLD;
int A_TAB_INACTIVE = A_REVERSE;
int A_MARKED       = A_REVERSE | A_BOLD;
int A_BUTTON       = A_NORMAL;
int A_BUTTON_SEL   = A_REVERSE | A_BOLD;
int A_MENU_SEL     = 0;
int A_MENU_NORMAL  = 0;
int A_SCROLL_THUMB = A_REVERSE;
int A_SCROLL_TRACK = 0;

void default_config(void)
{
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

    /* Активная вкладка: бирюзовый фон, БЕЛЫЙ текст */
    snprintf(g_theme.tab_active_bg, 8, "#00AAAA");
    snprintf(g_theme.tab_active_fg, 8, "#FFFFFF");

    snprintf(g_theme.tab_inactive_bg, 8, "#000000");
    snprintf(g_theme.tab_inactive_fg, 8, "#FFFFFF");
    snprintf(g_theme.status_bg, 8, "#FFFFFF");
    snprintf(g_theme.status_fg, 8, "#000000");
    snprintf(g_theme.dialog_bg, 8, "#FFFFFF");
    snprintf(g_theme.dialog_fg, 8, "#000000");

    /* Дефолтные цвета кнопок (фон темно-цветной, текст белый) */
    snprintf(g_theme.btn_quit_bg, 8, "#AA0000"); /* Темно-красный */
    snprintf(g_theme.btn_quit_fg, 8, "#FFFFFF");

    snprintf(g_theme.btn_menu_bg, 8, "#AA5500"); /* Темно-оранжевый */
    snprintf(g_theme.btn_menu_fg, 8, "#FFFFFF");

    snprintf(g_theme.btn_help_bg, 8, "#00AA00"); /* Темно-зеленый */
    snprintf(g_theme.btn_help_fg, 8, "#FFFFFF");

    snprintf(g_theme.btn_set_bg, 8, "#AA00AA");  /* Темно-пурпурный */
    snprintf(g_theme.btn_set_fg, 8, "#FFFFFF");

    snprintf(g_theme.btn_book_bg, 8, "#00AAAA"); /* Темно-бирюзовый */
    snprintf(g_theme.btn_book_fg, 8, "#FFFFFF");
    /* Таблица «индекс → поле» должна быть готова ДО разбора
     * конфига: parse_theme_line() достаёт поля через
     * theme_field_by_idx(). Раньше вызов стоял только в
     * init_attributes(), то есть уже после load_config(). */
    theme_bind_fields();
}

int theme_color_ok(const char *hex)
{
    int i;
    if (!hex || hex[0] != '#' || strlen(hex) != 7) return 0;
    for (i = 1; i < 7; i++)
        if (!isxdigit((unsigned char)hex[i])) return 0;
    return 1;
}

/* ФИКС (clang-tidy, cert-err34-c): "на вид hex" и "число" — разные вещи.
 * theme_color_ok() проверяет только символы, а sscanf("%02x%02x%02x")
 * вернёт 3 и для «#12345g», если бы такая строка прошла проверку выше, —
 * то есть корректность обеспечивалась ДВУМЯ разными проверками в двух
 * местах. Разбор цифр теперь тут же и вручную: каждой цифре соответствует
 * один nibble, результат не зависит от локали и не может «прочитаться
 * частично». */
static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse_hex(const char *hex, int *r, int *g, int *b)
{
    int v[3] = {0, 0, 0}, i;
    if (!theme_color_ok(hex)) return 0;
    for (i = 0; i < 3; i++) {
        int hi = hex_nibble(hex[1 + i * 2]);
        int lo = hex_nibble(hex[2 + i * 2]);
        if (hi < 0 || lo < 0) return 0;   /* недостижимо: theme_color_ok */
        v[i] = hi * 16 + lo;
    }
    *r = v[0];
    *g = v[1];
    *b = v[2];
    return 1;
}

int color_to_256(const char *hex)
{
    int r = 0, g = 0, b = 0, cr, cg, cb;
    if (!parse_hex(hex, &r, &g, &b)) return COLOR_WHITE;
    cr = (r * 5 + 127) / 255;
    cg = (g * 5 + 127) / 255;
    cb = (b * 5 + 127) / 255;
    return 16 + (36 * cr) + (6 * cg) + cb;
}

/* ФИКС (clang-tidy, bugprone-narrowing-conversions): init_pair() принимает
 * short, поэтому весь путь «hex → номер пары» теперь short, а не int.
 * Раньше сужение происходило неявно на каждом из 20 вызовов init_pair и
 * формально было implementation-defined. Значение всегда в [0, COLORS-1],
 * так что приведение безопасно и теперь явное в одной точке. */
static short clamp_color(int c)
{
    if (c < 0) return 0;
    if (COLORS > 0 && c >= COLORS) return (short)(COLORS - 1);
    return (short)c;
}

static short theme_color(const char *hex)
{
    static const int pal[16][3] = {
        {0, 0, 0},       {170, 0, 0},     {0, 170, 0},     {170, 85, 0},
        {0, 0, 170},     {170, 0, 170},   {0, 170, 170},   {170, 170, 170},
        {85, 85, 85},    {255, 85, 85},   {85, 255, 85},   {255, 255, 85},
        {85, 85, 255},   {255, 85, 255},  {85, 255, 255},  {255, 255, 255}
    };
    int r = 0, g = 0, b = 0, i, n, best = 7, best_d = 1 << 30;
    if (COLORS >= 256) return clamp_color(color_to_256(hex));
    if (!parse_hex(hex, &r, &g, &b))
        return clamp_color(COLOR_WHITE);
    n = COLORS >= 16 ? 16 : COLORS;
    if (n < 1) n = 1;
    if (n > 16) n = 16;
    for (i = 0; i < n; i++) {
        int dr = r - pal[i][0], dg = g - pal[i][1], db = b - pal[i][2];
        int d = dr * dr + dg * dg + db * db;
        if (d < best_d) { best_d = d; best = i; }
    }
    return clamp_color(best);
}

/* ФИКС (clang-tidy, bugprone-branch-clone): 32 почти одинаковых `case`
 * заменены таблицей. Раньше это был switch на 33 ветки — компилятор
 * разворачивал его в сравнения, а любая правка (добавили поле в
 * ThemeConfig — забыли ветку) давала NULL в рантайме. Теперь соответствие
 * «индекс ↔ поле» задаётся один раз списком, и добавление поля в конец
 * массива видно ровно в одном месте. */
char *theme_field_by_idx(int idx)
{
    if (idx < 0 || idx >= THEME_FIELDS) return NULL;
    return g_theme_field[idx];
}

/* Заполняет таблицу адресами полей ThemeConfig. Вызывается один раз после
 * смены темы: адреса постоянны (поля — массивы внутри g_theme), но
 * инициализация в рантайме, а не статической константой, потому что
 * g_theme — не constexpr-объект. */
void theme_bind_fields(void)
{
    int i = 0;
    g_theme_field[i++] = g_theme.bg;
    g_theme_field[i++] = g_theme.fg;
    g_theme_field[i++] = g_theme.sel_bg;
    g_theme_field[i++] = g_theme.sel_fg;
    g_theme_field[i++] = g_theme.sel_inactive_bg;
    g_theme_field[i++] = g_theme.sel_inactive_fg;
    g_theme_field[i++] = g_theme.marked_bg;
    g_theme_field[i++] = g_theme.marked_fg;
    g_theme_field[i++] = g_theme.dir;
    g_theme_field[i++] = g_theme.exec;
    g_theme_field[i++] = g_theme.header_bg;
    g_theme_field[i++] = g_theme.header_fg;
    g_theme_field[i++] = g_theme.title_bg;
    g_theme_field[i++] = g_theme.title_fg;
    g_theme_field[i++] = g_theme.tab_active_bg;
    g_theme_field[i++] = g_theme.tab_active_fg;
    g_theme_field[i++] = g_theme.tab_inactive_bg;
    g_theme_field[i++] = g_theme.tab_inactive_fg;
    g_theme_field[i++] = g_theme.status_bg;
    g_theme_field[i++] = g_theme.status_fg;
    g_theme_field[i++] = g_theme.dialog_bg;
    g_theme_field[i++] = g_theme.dialog_fg;
    g_theme_field[i++] = g_theme.btn_quit_bg;
    g_theme_field[i++] = g_theme.btn_quit_fg;
    g_theme_field[i++] = g_theme.btn_menu_bg;
    g_theme_field[i++] = g_theme.btn_menu_fg;
    g_theme_field[i++] = g_theme.btn_help_bg;
    g_theme_field[i++] = g_theme.btn_help_fg;
    g_theme_field[i++] = g_theme.btn_set_bg;
    g_theme_field[i++] = g_theme.btn_set_fg;
    g_theme_field[i++] = g_theme.btn_book_bg;
    g_theme_field[i++] = g_theme.btn_book_fg;
    while (i < THEME_FIELDS) g_theme_field[i++] = NULL;
}

const char *theme_name_by_idx(int idx)
{
    static const char *names[THEME_FIELDS] = {
        "bg", "fg", "sel_bg", "sel_fg", "sel_inactive_bg", "sel_inactive_fg",
        "marked_bg", "marked_fg", "dir", "exec",
        "header_bg", "header_fg", "title_bg", "title_fg",
        "tab_active_bg", "tab_active_fg", "tab_inactive_bg", "tab_inactive_fg",
        "status_bg", "status_fg", "dialog_bg", "dialog_fg",
        "btn_quit_bg", "btn_quit_fg",
        "btn_menu_bg", "btn_menu_fg",
        "btn_help_bg", "btn_help_fg",
        "btn_set_bg", "btn_set_fg",
        "btn_book_bg", "btn_book_fg"
    };
    if (idx < 0 || idx >= THEME_FIELDS) return "";
    return names[idx];
}

static void init_pairs_from_theme(void)
{
    short bg = theme_color(g_theme.bg);
    short fg = theme_color(g_theme.fg);
    init_pair(PAIR_BG,           fg, bg);
    init_pair(PAIR_SEL_ACTIVE,
              theme_color(g_theme.sel_fg),
              theme_color(g_theme.sel_bg));
    init_pair(PAIR_SEL_INACTIVE,
              theme_color(g_theme.sel_inactive_fg),
              theme_color(g_theme.sel_inactive_bg));
    init_pair(PAIR_DIR, theme_color(g_theme.dir), bg);
    init_pair(PAIR_EXEC, theme_color(g_theme.exec), bg);
    init_pair(PAIR_HEADER,
              theme_color(g_theme.header_fg),
              theme_color(g_theme.header_bg));
    init_pair(PAIR_TITLE,
              theme_color(g_theme.title_fg),
              theme_color(g_theme.title_bg));
    init_pair(PAIR_TAB_ACTIVE,
              theme_color(g_theme.tab_active_fg),
              theme_color(g_theme.tab_active_bg));
    init_pair(PAIR_TAB_INACTIVE,
              theme_color(g_theme.tab_inactive_fg),
              theme_color(g_theme.tab_inactive_bg));
    init_pair(PAIR_STATUS,
              theme_color(g_theme.status_fg),
              theme_color(g_theme.status_bg));
    init_pair(PAIR_DIALOG,
              theme_color(g_theme.dialog_fg),
              theme_color(g_theme.dialog_bg));
    init_pair(PAIR_SHADOW,       clamp_color(COLOR_BLACK),
              clamp_color(COLOR_BLACK));
    init_pair(PAIR_MARKED,
              theme_color(g_theme.marked_fg),
              theme_color(g_theme.marked_bg));
    init_pair(PAIR_MENU_SEL,
              theme_color(g_theme.sel_fg),
              theme_color(g_theme.sel_bg));
    init_pair(PAIR_SCROLL,
              theme_color(g_theme.sel_fg),
              theme_color(g_theme.sel_bg));

    init_pair(PAIR_BTN_QUIT, theme_color(g_theme.btn_quit_fg), theme_color(g_theme.btn_quit_bg));
    init_pair(PAIR_BTN_MENU, theme_color(g_theme.btn_menu_fg), theme_color(g_theme.btn_menu_bg));
    init_pair(PAIR_BTN_HELP, theme_color(g_theme.btn_help_fg), theme_color(g_theme.btn_help_bg));
    init_pair(PAIR_BTN_SETTINGS, theme_color(g_theme.btn_set_fg), theme_color(g_theme.btn_set_bg));
    init_pair(PAIR_BTN_BOOKMARKS, theme_color(g_theme.btn_book_fg), theme_color(g_theme.btn_book_bg));
}

static void set_attrs_from_pairs(void)
{
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
}

static void set_attrs_fallback(void)
{
    A_MENU_SEL = A_REVERSE | A_BOLD;
    A_MENU_NORMAL = A_NORMAL;
    A_SCROLL_THUMB = A_REVERSE;
    A_SCROLL_TRACK = A_NORMAL;
}

void init_attributes(void)
{
    theme_bind_fields();
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pairs_from_theme();
        set_attrs_from_pairs();
    } else {
        set_attrs_fallback();
    }
}

void apply_shadow(int y, int x, int h, int w)
{
    int i;
    attron(COLOR_PAIR(PAIR_SHADOW));
    for (i = 1; i <= h; i++) mvhline(y + i, x + w, ' ', 2);
    for (i = 1; i <= w; i++) mvvline(y + h, x + i, ' ', 1);
    attroff(COLOR_PAIR(PAIR_SHADOW));
    refresh();
}
