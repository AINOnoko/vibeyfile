#include "fm.h"

#define HELP_MAX_LINES 64
#define HELP_LINE_MAX  512

static int binary_sample(int fd)
{
    unsigned char buf[512];
    size_t n = 0, i, bad = 0;
    while (n < sizeof buf) {
        ssize_t got = read(fd, buf + n, sizeof buf - n);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        n += (size_t)got;
    }
    for (i = 0; i < n; i++) {
        unsigned char c = buf[i];
        if (c == 0) return 1;
        if (c < 9 || (c > 13 && c < 32) || c == 127) bad++;
    }
    return n > 0 && bad * 5 > n;
}

int is_binary_content(const char *path)
{
    struct stat st;
    int fd, bin;
    if (!path) return 1;
    fd = open(path, O_RDONLY);
    if (fd < 0) return 1;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        return 1;
    }
    if (st.st_size == 0) { close(fd); return 0; }
    bin = binary_sample(fd);
    close(fd);
    return bin;
}

typedef struct { char **lines; int count; int cap; int max_width; } FileLines;

static int push_line(FileLines *fl, const char *line)
{
    char *dup;
    if (fl->count == fl->cap) {
        int ncap = fl->cap ? fl->cap * 2 : 256;
        char **tmp = realloc(fl->lines, ncap * sizeof(char *));
        if (!tmp) return -1;
        fl->lines = tmp;
        fl->cap = ncap;
    }
    dup = strdup(line);
    if (!dup) return -1;
    fl->lines[fl->count++] = dup;

    int len = utf8_strlen(line);
    if (len > fl->max_width) fl->max_width = len;

    return 0;
}

static void free_lines(FileLines *fl)
{
    int i;
    for (i = 0; i < fl->count; i++) free(fl->lines[i]);
    free(fl->lines);
    fl->lines = NULL;
    fl->count = 0;
    fl->cap = 0;
    fl->max_width = 0;
}

static int read_all_lines(const char *path, FileLines *fl)
{
    FILE *f = fopen(path, "r");
    char *line = NULL;
    size_t line_len = 0;
    ssize_t n;
    int rc = 0;
    memset(fl, 0, sizeof(*fl));
    if (!f) return -1;
    while ((n = getline(&line, &line_len, f)) != -1) {
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';
        if (push_line(fl, line) != 0) { rc = -1; goto cleanup; }
    }
    if (ferror(f)) rc = -1;
cleanup:
    free(line);
    fclose(f);
    if (rc != 0) free_lines(fl);
    return rc;
}

static void view_draw_title(WINDOW *win, int cols, const char *base)
{
    char title[PATH_MAX + 32];
    wattron(win, A_TITLE);
    mvwhline(win, 0, 0, ' ', cols);
    snprintf(title, sizeof title, " %s ", base ? base : "");
    mvwaddn_utf8(win, 0, 1, title, cols > 4 ? cols - 4 : 1);
    wattroff(win, A_TITLE);
}

static void view_draw_lines(WINDOW *win, FileLines *fl, int scroll,
                            int h_scroll, int vis, int view_w)
{
    int i;
    for (i = 0; i < vis && scroll + i < fl->count; i++) {
        const char *ln = fl->lines[scroll + i];
        size_t skip = utf8_prefix_bytes(ln, h_scroll);
        const char *disp = ln + skip;
        mvwaddnstr(win, 1 + i, 1, disp, (int)utf8_prefix_bytes(disp, view_w));
    }
}

static void view_draw_vbar(WINDOW *win, int count, int scroll, int vis, int cols)
{
    int i, bar_h = vis, thumb_h, max_scroll, thumb_pos;
    if (count <= vis) return;
    thumb_h = (bar_h * vis) / count;
    if (thumb_h < 1) thumb_h = 1;
    max_scroll = count - vis;
    thumb_pos = max_scroll > 0 ? (bar_h - thumb_h) * scroll / max_scroll : 0;
    if (thumb_pos + thumb_h > bar_h) thumb_pos = bar_h - thumb_h;
    for (i = 0; i < bar_h; i++) {
        int on = i >= thumb_pos && i < thumb_pos + thumb_h;
        if (on) wattron(win, A_REVERSE);
        mvwaddch(win, 1 + i, cols - 1, on ? ' ' : ACS_VLINE);
        if (on) wattroff(win, A_REVERSE);
    }
}

static void view_draw_hbar(WINDOW *win, FileLines *fl, int h_scroll,
                            int rows, int cols, int view_w)
{
    int i, bar_w, thumb_w, max_h, thumb_pos;
    max_h = fl->max_width > view_w ? fl->max_width - view_w : 0;
    if (max_h <= 0 || fl->max_width <= 0) return;
    bar_w = cols - 2;
    if (bar_w < 1) return;
    thumb_w = (bar_w * view_w) / fl->max_width;
    if (thumb_w < 1) thumb_w = 1;
    thumb_pos = (bar_w - thumb_w) * h_scroll / max_h;
    if (thumb_pos + thumb_w > bar_w) thumb_pos = bar_w - thumb_w;
    for (i = 0; i < bar_w; i++) {
        int on = i >= thumb_pos && i < thumb_pos + thumb_w;
        if (on) wattron(win, A_REVERSE);
        mvwaddch(win, rows - 2, 1 + i, on ? ' ' : ACS_HLINE);
        if (on) wattroff(win, A_REVERSE);
    }
}

static void view_draw_status(WINDOW *win, const char *base, int scroll,
                              int count, int h_scroll, int rows, int cols, int max_h)
{
    char status[512];
    wattron(win, COLOR_PAIR(PAIR_STATUS));
    mvwhline(win, rows - 1, 0, ' ', cols);
    if (max_h > 0)
        snprintf(status, sizeof status,
                 " %s | %d/%d | x:%d | h/l, ←/→ - сдвиг, q/Esc - выход",
                 base, scroll + 1, count, h_scroll);
    else
        snprintf(status, sizeof status, " %s | %d/%d | q/Esc - выход",
                 base, scroll + 1, count);
    mvwaddn_utf8(win, rows - 1, 1, status, cols > 2 ? cols - 2 : 1);
    wattroff(win, COLOR_PAIR(PAIR_STATUS));
}

static void view_draw(WINDOW *win, FileLines *fl, int scroll, int h_scroll, int vis,
                      int rows, int cols, const char *base)
{
    int view_w = cols > 2 ? cols - 2 : 1;
    int max_h = fl->max_width > view_w ? fl->max_width - view_w : 0;
    if (h_scroll > max_h) h_scroll = max_h;
    werase(win);
    view_draw_title(win, cols, base);
    view_draw_lines(win, fl, scroll, h_scroll, vis, view_w);
    view_draw_vbar(win, fl->count, scroll, vis, cols);
    view_draw_hbar(win, fl, h_scroll, rows, cols, view_w);
    view_draw_status(win, base, scroll, fl->count, h_scroll, rows, cols, max_h);
    wrefresh(win);
}

static int view_handle_key(int ch, int *scroll, int *h_scroll, int vis, int total, int max_width, int cols)
{
    int view_w = cols - 2;
    int max_h_scroll = max_width > view_w ? max_width - view_w : 0;

    if (ch == 'q' || ch == 'Q' || ch == 27) return 0;

    if (ch == 'j' || ch == KEY_DOWN) { if (*scroll < total - 1) (*scroll)++; }
    else if (ch == 'k' || ch == KEY_UP) { if (*scroll > 0) (*scroll)--; }
    else if (ch == KEY_NPAGE || ch == CTRL('d')) {
        *scroll += vis;
        if (*scroll > total - vis) *scroll = total - vis;
        if (*scroll < 0) *scroll = 0;
    } else if (ch == KEY_PPAGE || ch == CTRL('u')) {
        *scroll -= vis;
        if (*scroll < 0) *scroll = 0;
    } else if (ch == 'g' || ch == KEY_HOME) *scroll = 0;
    else if (ch == 'G' || ch == KEY_END) {
        *scroll = total - vis;
        if (*scroll < 0) *scroll = 0;
    }
    else if (ch == 'h' || ch == KEY_LEFT) { if (*h_scroll > 0) (*h_scroll)--; }
    else if (ch == 'l' || ch == KEY_RIGHT) { if (*h_scroll < max_h_scroll) (*h_scroll)++; }
    else if (ch == '0') *h_scroll = 0;
    else if (ch == '$') *h_scroll = max_h_scroll;

    return 1;
}

static int view_gate(const char *path, struct stat *st)
{
    int fd;
    if (!path) return -1;
    fd = open(path, O_RDONLY);
    if (fd < 0 || fstat(fd, st) != 0) {
        if (fd >= 0) close(fd);
        message_box("Ошибка", "Не удалось открыть файл");
        return -1;
    }
    close(fd);
    if (!S_ISREG(st->st_mode)) {
        message_box("Просмотр", "Это не обычный файл");
        return -1;
    }
    if (st->st_size > VIEW_MAX_SIZE) {
        message_box("Слишком большой файл",
                    "Файл больше 5 МБ. Используйте внешний просмотрщик.");
        return -1;
    }
    if (is_binary_content(path)) {
        message_box("Просмотр", "Двоичный файл. Откройте внешней программой.");
        return -1;
    }
    return 0;
}

static WINDOW *view_open_win(int *rows, int *cols, int *vis)
{
    WINDOW *win;
    getmaxyx(stdscr, *rows, *cols);
    win = newwin(*rows, *cols, 0, 0);
    if (!win) return NULL;
    wbkgd(win, COLOR_PAIR(PAIR_BG));
    ui_prepare_win(win);
    *vis = *rows - 3;
    if (*vis < 1) *vis = 1;
    return win;
}

static void view_on_mouse(int *scroll, int total)
{
    MEVENT ev;
    if (getmouse(&ev) != OK) return;
    if ((ev.bstate & BUTTON4_PRESSED) && *scroll > 0) (*scroll)--;
    if ((ev.bstate & BUTTON5_PRESSED) && *scroll < total - 1) (*scroll)++;
}

static int view_step(WINDOW **win, FileLines *fl, int *scroll, int *h_scroll,
                     int *rows, int *cols, int *vis)
{
    int ch;
    if (!*win) return 0;
    ch = get_mapped_key(*win);
    if (!g_app.running) return 0;
    if (ch == KEY_RESIZE) {
        delwin(*win);
        *win = view_open_win(rows, cols, vis);
        return *win != NULL;
    }
    if (ch == KEY_MOUSE) { view_on_mouse(scroll, fl->count); return 1; }
    if (ch == ERR) return 1;
    return view_handle_key(ch, scroll, h_scroll, *vis, fl->count,
                           fl->max_width, *cols);
}

void view_file(const char *path)
{
    struct stat st;
    FileLines fl;
    int rows, cols, scroll = 0, h_scroll = 0, vis, running = 1;
    WINDOW *win;
    const char *base;
    if (view_gate(path, &st) != 0) return;
    if (read_all_lines(path, &fl) != 0) {
        message_box("Ошибка", "Недостаточно памяти для просмотра файла");
        return;
    }
    if (fl.count == 0) {
        free_lines(&fl);
        message_box("Просмотр", "Файл пуст");
        return;
    }
    win = view_open_win(&rows, &cols, &vis);
    if (!win) { free_lines(&fl); return; }
    base = strrchr(path, '/');
    base = base ? base + 1 : path;
    while (running && win) {
        view_draw(win, &fl, scroll, h_scroll, vis, rows, cols, base);
        running = view_step(&win, &fl, &scroll, &h_scroll, &rows, &cols, &vis);
    }
    free_lines(&fl);
    if (win) delwin(win);
    touchwin(stdscr);
    refresh();
}

static void help_draw_lines(WINDOW *win, const char **lines, int total,
                            int scroll, int vis, int w)
{
    int i;
    for (i = 0; i < vis && scroll + i < total; i++) {
        size_t b = utf8_prefix_bytes(lines[scroll + i], w - 4);
        mvwaddnstr(win, 1 + i, 2, lines[scroll + i], (int)b);
    }
}

static void help_draw_bar(WINDOW *win, int total, int scroll, int vis, int h, int w)
{
    int j, bar_h, thumb_h, max_scroll, thumb_pos;
    if (total <= vis) return;
    bar_h = h - 3;
    if (bar_h < 1) return;
    thumb_h = (bar_h * vis) / total;
    if (thumb_h < 1) thumb_h = 1;
    max_scroll = total - vis;
    thumb_pos = max_scroll > 0 ? (bar_h - thumb_h) * scroll / max_scroll : 0;
    if (thumb_pos + thumb_h > bar_h) thumb_pos = bar_h - thumb_h;
    for (j = 0; j < bar_h; j++) {
        if (j < thumb_pos || j >= thumb_pos + thumb_h) continue;
        wattron(win, A_REVERSE);
        mvwaddch(win, 1 + j, w - 2, ' ');
        wattroff(win, A_REVERSE);
    }
}

static void help_draw_footer(WINDOW *win, int total, int scroll, int vis, int h, int w)
{
    char footer[128];
    if (total > vis)
        snprintf(footer, sizeof footer, "j/k, PgUp/PgDn | q/Esc [%d/%d]",
                 scroll + 1, total - vis + 1);
    else
        snprintf(footer, sizeof footer, "q/Esc - выход");
    mvwaddn_utf8(win, h - 2, 2, footer, w > 4 ? w - 4 : 1);
}

static void help_draw(WINDOW *win, const char **lines, int total,
                      int scroll, int vis, int h, int w)
{
    werase(win);
    box(win, 0, 0);
    mvwaddnstr(win, 0, 2, " Справка ", w - 4);
    help_draw_lines(win, lines, total, scroll, vis, w);
    help_draw_bar(win, total, scroll, vis, h, w);
    help_draw_footer(win, total, scroll, vis, h, w);
    wrefresh(win);
}

typedef struct {
    char up[32], down[32], parent[32], open[32], open_l[32];
    char home[32], end[32], select_[32], menu[32], book[32];
    char sett[32], split[32], prev[32], sortn[32], sortr[32];
    char search[32], ref[32], full[32], quit[32], help[32];
    int help_key;
} HelpKeys;

static void help_one_key(char *dst, size_t n, const char *name)
{
    key_display_name(binding_key_by_name(name), dst, n);
}

static void help_load_keys(HelpKeys *k)
{
    help_one_key(k->up, sizeof k->up, "up");
    help_one_key(k->down, sizeof k->down, "down");
    help_one_key(k->parent, sizeof k->parent, "parent");
    help_one_key(k->open, sizeof k->open, "open");
    help_one_key(k->open_l, sizeof k->open_l, "open_l");
    help_one_key(k->home, sizeof k->home, "home");
    help_one_key(k->end, sizeof k->end, "end");
    help_one_key(k->select_, sizeof k->select_, "select");
    help_one_key(k->menu, sizeof k->menu, "menu");
    help_one_key(k->book, sizeof k->book, "bookmarks");
    help_one_key(k->sett, sizeof k->sett, "settings");
    help_one_key(k->split, sizeof k->split, "split");
    help_one_key(k->prev, sizeof k->prev, "preview");
    help_one_key(k->sortn, sizeof k->sortn, "sort_next");
    help_one_key(k->sortr, sizeof k->sortr, "sort_reverse");
    help_one_key(k->search, sizeof k->search, "search");
    help_one_key(k->ref, sizeof k->ref, "refresh");
    help_one_key(k->full, sizeof k->full, "refresh_full");
    help_one_key(k->quit, sizeof k->quit, "quit");
    help_one_key(k->help, sizeof k->help, "help");
    k->help_key = binding_key_by_name("help");
}

static int help_push(char store[][HELP_LINE_MAX], const char **lines, int n, const char *s)
{
    if (n >= HELP_MAX_LINES) return n;
    copy_str(store[n], HELP_LINE_MAX, s ? s : "");
    lines[n] = store[n];
    return n + 1;
}

static int help_pushf(char store[][HELP_LINE_MAX], const char **lines, int n,
                      const char *fmt, const char *a, const char *b)
{
    char buf[HELP_LINE_MAX];
    snprintf(buf, sizeof buf, fmt, a ? a : "", b ? b : "");
    return help_push(store, lines, n, buf);
}

static int help_fill_banner(char store[][HELP_LINE_MAX], const char **lines, int n)
{
    n = help_push(store, lines, n, "========================================================================");
    n = help_push(store, lines, n, "__     __  ___   ____   _____  __   __  _____   ___   _      _____ ");
    n = help_push(store, lines, n, "\\\\ \\\\   / / |_ _| | __ ) | ____| \\\\ \\\\ / / |  ___| |_ _| | |    | ____|");
    n = help_push(store, lines, n, " \\\\ \\\\ / /   | |  |  _ \\\\ |  _|    \\\\ V /  | |_     | |  | |    |  _|  ");
    n = help_push(store, lines, n, "  \\\\ V /    | |  | |_) || |___    | |   |  _|    | |  | |___ | |___ ");
    n = help_push(store, lines, n, "   \\\\_/    |___| |____/ |_____|   |_|   |_|     |___| |_____||_____|");
    n = help_push(store, lines, n, "");
    return help_push(store, lines, n, "========================================================================");
}

static int help_fill_nav(char store[][HELP_LINE_MAX], const char **lines, int n, const HelpKeys *k)
{
    n = help_push(store, lines, n, "");
    n = help_push(store, lines, n, "Навигация:");
    n = help_pushf(store, lines, n, "  %s/↓, %s/↑ - Вверх / Вниз", k->down, k->up);
    n = help_pushf(store, lines, n, "  %s/←/Backspace - Вверх по каталогам", k->parent, "");
    n = help_pushf(store, lines, n, "  %s/%s/→/Enter - Войти / Открыть", k->open_l, k->open);
    n = help_pushf(store, lines, n, "  %s / %s - В начало / В конец списка", k->home, k->end);
    n = help_push(store, lines, n, "  Tab - Переключение панелей (в сплите)");
    n = help_pushf(store, lines, n, "  %s - Выделить / Снять выделение", k->select_, "");
    return help_push(store, lines, n, "");
}

static int help_fill_acts(char store[][HELP_LINE_MAX], const char **lines, int n, const HelpKeys *k)
{
    n = help_push(store, lines, n, "Действия и вкладки:");
    n = help_pushf(store, lines, n, "  %s - Меню действий", k->menu, "");
    n = help_pushf(store, lines, n, "  %s / [=] - Избранное", k->book, "");
    n = help_push(store, lines, n,
                  "  Избранное: папка, если ничего не отмечено; иначе только отмеченное");
    n = help_push(store, lines, n,
                  "  Документы, Загрузки, Видео и т.п. — если избранное ещё пустое");
    n = help_pushf(store, lines, n, "  %s / [O] - Настройки", k->sett, "");
    n = help_pushf(store, lines, n, "  %s - Вкл/выкл сплит", k->split, "");
    n = help_pushf(store, lines, n, "  %s - Вкл/выкл предпросмотр", k->prev, "");
    n = help_pushf(store, lines, n, "  %s / %s - Сортировка / Реверс", k->sortn, k->sortr);
    n = help_pushf(store, lines, n, "  %s - Поиск по имени", k->search, "");
    n = help_pushf(store, lines, n, "  %s - Обновить список", k->ref, "");
    n = help_pushf(store, lines, n, "  %s - Перерисовать экран", k->full, "");
    n = help_push(store, lines, n, "  F1..F4 - Переключение вкладок");
    n = help_pushf(store, lines, n, "  %s / [X] - Выход", k->quit, "");
    n = help_pushf(store, lines, n, "  %s - Эта справка", k->help, "");
    n = help_push(store, lines, n, "");
    n = help_pushf(store, lines, n, "Клавиши можно переназначить в Настройках (%s).", k->sett, "");
    n = help_push(store, lines, n, "");
    n = help_push(store, lines, n, "Разработано AINOnoko с помощью ИИ.");
    return help_push(store, lines, n, "VibeyFile распространяется под лицензией GPLv3.");
}

static int help_widest(const char **lines, int total)
{
    int i, max_len = 55;
    for (i = 0; i < total; i++) {
        int len = utf8_strlen(lines[i]);
        if (len > max_len) max_len = len;
    }
    return max_len;
}

typedef struct { int w, h, y, x, vis, max_scroll; } HelpGeom;

static void help_geom(HelpGeom *g, int max_len, int total)
{
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    g->w = max_len + 6;
    if (g->w > cols - 4) g->w = cols - 4;
    if (g->w < 40) g->w = 40;
    g->h = (rows * 64) / 100;
    if (g->h > rows - 4) g->h = rows - 4;
    if (g->h < 10) g->h = 10;
    g->y = (rows - g->h) / 2;
    g->x = (cols - g->w) / 2;
    g->vis = g->h - 4 > 0 ? g->h - 4 : 1;
    g->max_scroll = total - g->vis > 0 ? total - g->vis : 0;
}

static WINDOW *help_open(const HelpGeom *g)
{
    WINDOW *win;
    apply_shadow(g->y, g->x, g->h, g->w);
    win = newwin(g->h, g->w, g->y, g->x);
    if (!win) return NULL;
    wbkgd(win, COLOR_PAIR(PAIR_DIALOG));
    ui_prepare_win(win);
    return win;
}

static int help_outside(const MEVENT *ev, const HelpGeom *g)
{
    return ev->x < g->x || ev->x >= g->x + g->w
        || ev->y < g->y || ev->y >= g->y + g->h;
}

static int help_on_mouse(int *scroll, const HelpGeom *g)
{
    MEVENT ev;
    if (getmouse(&ev) != OK) return 1;
    if ((ev.bstate & BUTTON1_CLICKED) && help_outside(&ev, g)) return 0;
    if ((ev.bstate & BUTTON4_PRESSED) && *scroll > 0) (*scroll)--;
    if ((ev.bstate & BUTTON5_PRESSED) && *scroll < g->max_scroll) (*scroll)++;
    return 1;
}

static int help_on_key(int ch, int *scroll, const HelpGeom *g, int help_key)
{
    if (ch == 'q' || ch == 'Q' || ch == 27 || ch == '\n' || ch == KEY_ENTER)
        return 0;
    if (help_key && ch == help_key) return 0;
    if (ch == KEY_DOWN || ch == 'j') {
        if (*scroll < g->max_scroll) (*scroll)++;
    } else if (ch == KEY_UP || ch == 'k') {
        if (*scroll > 0) (*scroll)--;
    } else if (ch == KEY_NPAGE || ch == CTRL('d')) {
        *scroll += g->vis;
        if (*scroll > g->max_scroll) *scroll = g->max_scroll;
    } else if (ch == KEY_PPAGE || ch == CTRL('u')) {
        *scroll -= g->vis;
        if (*scroll < 0) *scroll = 0;
    } else if (ch == KEY_HOME || ch == 'g') *scroll = 0;
    else if (ch == KEY_END || ch == 'G') *scroll = g->max_scroll;
    return 1;
}

static int help_step(WINDOW **win, HelpGeom *g, const char **lines, int total,
                     int *scroll, int help_key, int max_len)
{
    int ch;
    help_draw(*win, lines, total, *scroll, g->vis, g->h, g->w);
    ch = get_mapped_key(*win);
    if (ch == KEY_RESIZE) {
        delwin(*win);
        help_geom(g, max_len, total);
        *win = help_open(g);
        return *win != NULL;
    }
    if (ch == KEY_MOUSE) return help_on_mouse(scroll, g);
    if (ch == ERR) return 1;
    return help_on_key(ch, scroll, g, help_key);
}

void show_help(void)
{
    char store[HELP_MAX_LINES][HELP_LINE_MAX];
    const char *lines[HELP_MAX_LINES];
    HelpKeys keys;
    HelpGeom geom;
    int total, scroll = 0, running = 1, max_len;
    WINDOW *win;
    help_load_keys(&keys);
    total = help_fill_banner(store, lines, 0);
    total = help_fill_nav(store, lines, total, &keys);
    total = help_fill_acts(store, lines, total, &keys);
    max_len = help_widest(lines, total);
    help_geom(&geom, max_len, total);
    win = help_open(&geom);
    if (!win) return;
    while (running && g_app.running && win)
        running = help_step(&win, &geom, lines, total, &scroll, keys.help_key, max_len);
    if (win) delwin(win);
    touchwin(stdscr);
    refresh();
}
