#include "fm.h"

AppState    g_app;
ThemeConfig g_theme;
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
    snprintf(g_theme.tab_active_bg, 8, "#55FF55");
    snprintf(g_theme.tab_active_fg, 8, "#000000");
    snprintf(g_theme.tab_inactive_bg, 8, "#000000");
    snprintf(g_theme.tab_inactive_fg, 8, "#FFFFFF");
    snprintf(g_theme.status_bg, 8, "#FFFFFF");
    snprintf(g_theme.status_fg, 8, "#000000");
    snprintf(g_theme.dialog_bg, 8, "#FFFFFF");
    snprintf(g_theme.dialog_fg, 8, "#000000");
}

int theme_color_ok(const char *hex)
{
    int i;
    if (!hex || hex[0] != '#' || strlen(hex) != 7) return 0;
    for (i = 1; i < 7; i++)
        if (!isxdigit((unsigned char)hex[i])) return 0;
    return 1;
}

static int parse_hex(const char *hex, int *r, int *g, int *b)
{
    unsigned int ur, ug, ub;
    if (!theme_color_ok(hex)) return 0;
    if (sscanf(hex + 1, "%02x%02x%02x", &ur, &ug, &ub) != 3) return 0;
    *r = (int)ur;
    *g = (int)ug;
    *b = (int)ub;
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

static int clamp_color(int c)
{
    if (c < 0) return 0;
    if (COLORS > 0 && c >= COLORS) return COLORS - 1;
    return c;
}

static int theme_color(const char *hex)
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

char *theme_field_by_idx(int idx)
{
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

const char *theme_name_by_idx(int idx)
{
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

static void init_pairs_from_theme(void)
{
    int bg = theme_color(g_theme.bg);
    int fg = theme_color(g_theme.fg);
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
