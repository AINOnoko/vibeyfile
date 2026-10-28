#include "fm.h"

#define HELP_MAX_LINES 64
#define HELP_LINE_MAX  512

int is_text_file(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot || dot == name) return 0;
    dot++;
    if (!strcasecmp(dot, "txt") || !strcasecmp(dot, "log")
        || !strcasecmp(dot, "md") || !strcasecmp(dot, "rst")) return 1;
    if (!strcasecmp(dot, "c") || !strcasecmp(dot, "h")
        || !strcasecmp(dot, "cpp") || !strcasecmp(dot, "cc")
        || !strcasecmp(dot, "hpp") || !strcasecmp(dot, "py")
        || !strcasecmp(dot, "rs") || !strcasecmp(dot, "go")
        || !strcasecmp(dot, "js") || !strcasecmp(dot, "ts")
        || !strcasecmp(dot, "java") || !strcasecmp(dot, "rb")) return 1;
    if (!strcasecmp(dot, "json") || !strcasecmp(dot, "yaml")
        || !strcasecmp(dot, "yml") || !strcasecmp(dot, "toml")
        || !strcasecmp(dot, "ini") || !strcasecmp(dot, "xml")) return 1;
    if (!strcasecmp(dot, "sh") || !strcasecmp(dot, "bash")
        || !strcasecmp(dot, "zsh") || !strcasecmp(dot, "fish")) return 1;
    return 0;
}

int is_binary_content(const char *path)
{
    FILE *f;
    unsigned char buf[512];
    struct stat st;
    size_t n, i, bad = 0;
    if (!path || stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return 1;
    if (st.st_size == 0) return 0;
    f = fopen(path, "rb");
    if (!f) return 1;
    n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    for (i = 0; i < n; i++) {
        unsigned char c = buf[i];
        if (c == 0) return 1;
        if (c < 9 || (c > 13 && c < 32) || c == 127) bad++;
    }
    return n > 0 && bad * 5 > n;
}

typedef struct { char **lines; int count; int cap; } FileLines;

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
        if (push_line(fl, line) != 0) { rc = -1; break; }
    }
    free(line);
    fclose(f);
    if (rc != 0) free_lines(fl);
    return rc;
}

static void view_draw(WINDOW *win, FileLines *fl, int scroll, int vis,
                      int rows, int cols, const char *base)
{
    int i;
    char status[512];
    char title[PATH_MAX + 32];
    werase(win);
    wattron(win, A_TITLE);
    mvwhline(win, 0, 0, ' ', cols);
    snprintf(title, sizeof title, " %s ", base);
    mvwaddnstr(win, 0, 1, title, cols - 4);
    wattroff(win, A_TITLE);
    for (i = 0; i < vis && scroll + i < fl->count; i++) {
        const char *ln = fl->lines[scroll + i];
        size_t bytes = utf8_prefix_bytes(ln, cols - 2);
        mvwaddnstr(win, 1 + i, 1, ln, bytes);
    }
    wattron(win, COLOR_PAIR(PAIR_STATUS));
    mvwhline(win, rows - 1, 0, ' ', cols);
    snprintf(status, sizeof status, " %s | %d/%d | q/Esc - выход",
             base, scroll + 1, fl->count);
    mvwaddnstr(win, rows - 1, 1, status, cols - 2);
    wattroff(win, COLOR_PAIR(PAIR_STATUS));
    wrefresh(win);
}

static int view_handle_key(int ch, int *scroll, int vis, int total)
{
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
    return 1;
}

void view_file(const char *path)
{
    struct stat st;
    FileLines fl;
    int rows, cols, scroll = 0, vis, running = 1;
    WINDOW *win;
    const char *base;
    if (stat(path, &st) != 0) {
        message_box("Ошибка", "Не удалось открыть файл");
        return;
    }
    if (!S_ISREG(st.st_mode)) {
        message_box("Просмотр", "Это не обычный файл");
        return;
    }
    if (is_binary_content(path)) {
        message_box("Просмотр", "Двоичный файл. Откройте внешней программой.");
        return;
    }
    if (st.st_size > VIEW_MAX_SIZE) {
        message_box("Слишком большой файл",
                    "Файл больше 5 МБ. Используйте внешний просмотрщик.");
        return;
    }
    if (read_all_lines(path, &fl) != 0) {
        message_box("Ошибка", "Недостаточно памяти для просмотра файла");
        return;
    }
    if (fl.count == 0) {
        free_lines(&fl);
        message_box("Просмотр", "Файл пуст");
        return;
    }
    getmaxyx(stdscr, rows, cols);
    win = newwin(rows, cols, 0, 0);
    if (!win) { free_lines(&fl); return; }
    wbkgd(win, COLOR_PAIR(PAIR_BG));
    ui_prepare_win(win);
    vis = rows - 2;
    base = strrchr(path, '/');
    base = base ? base + 1 : path;
    while (running) {
        view_draw(win, &fl, scroll, vis, rows, cols, base);
        {
            int ch = wgetch(win);
            if (!g_app.running) break;
            if (ch == ERR) continue;
            running = view_handle_key(ch, &scroll, vis, fl.count);
        }
    }
    free_lines(&fl);
    delwin(win);
    touchwin(stdscr);
    refresh();
}

static void help_draw(WINDOW *win, const char **lines, int total,
                      int scroll, int vis, int h, int w)
{
    int i;
    werase(win);
    box(win, 0, 0);
    mvwaddnstr(win, 0, 2, " Справка ", w - 4);
    for (i = 0; i < vis; i++) {
        int idx = scroll + i;
        if (idx >= total) break;
        {
            size_t b = utf8_prefix_bytes(lines[idx], w - 4);
            mvwaddnstr(win, 1 + i, 2, lines[idx], (int)b);
        }
    }
    if (total > vis) {
        int bar_x = w - 2, bar_h = h - 3, j;
        int thumb_h = (bar_h * vis) / total;
        int max_scroll = total - vis;
        int thumb_pos = max_scroll > 0 ? (bar_h - thumb_h) * scroll
                          / max_scroll : 0;
        if (thumb_h < 1) thumb_h = 1;
        if (thumb_pos + thumb_h > bar_h) thumb_pos = bar_h - thumb_h;
        for (j = 0; j < bar_h; j++)
            if (j >= thumb_pos && j < thumb_pos + thumb_h) {
                wattron(win, A_REVERSE);
                mvwaddch(win, 1 + j, bar_x, ' ');
                wattroff(win, A_REVERSE);
            }
    }
    {
        char footer[128];
        if (total > vis)
            snprintf(footer, sizeof footer,
                     "j/k, PgUp/PgDn | q/Esc [%d/%d]",
                     scroll + 1, total - vis + 1);
        else
            snprintf(footer, sizeof footer, "q/Esc - выход");
        mvwaddnstr(win, h - 2, 2, footer, w - 4);
    }
    wrefresh(win);
}

void show_help(void)
{
    char lines_storage[HELP_MAX_LINES][HELP_LINE_MAX];
    const char *lines[HELP_MAX_LINES];
    int total = 0, help_key, max_len = 0, i;
    int rows, cols, w, h, y, x, vis, max_scroll;
    int scroll = 0, running = 1;
    char kup[32], kdown[32], kparent[32], kopen[32], kopenl[32],
         khome[32], kend[32], kselect[32],
         kmenu[32], kbook[32], ksett[32], ksplit[32], kprev[32],
         ksortn[32], ksortr[32], ksearch[32],
         kref[32], kfull[32], kquit[32], khelp[32];
    WINDOW *win;

    key_display_name(binding_key_by_name("up"),            kup,     sizeof kup);
    key_display_name(binding_key_by_name("down"),          kdown,   sizeof kdown);
    key_display_name(binding_key_by_name("parent"),        kparent, sizeof kparent);
    key_display_name(binding_key_by_name("open"),          kopen,   sizeof kopen);
    key_display_name(binding_key_by_name("open_l"),        kopenl,  sizeof kopenl);
    key_display_name(binding_key_by_name("home"),          khome,   sizeof khome);
    key_display_name(binding_key_by_name("end"),           kend,    sizeof kend);
    key_display_name(binding_key_by_name("select"),        kselect, sizeof kselect);
    key_display_name(binding_key_by_name("menu"),          kmenu,   sizeof kmenu);
    key_display_name(binding_key_by_name("bookmarks"),     kbook,   sizeof kbook);
    key_display_name(binding_key_by_name("settings"),      ksett,   sizeof ksett);
    key_display_name(binding_key_by_name("split"),         ksplit,  sizeof ksplit);
    key_display_name(binding_key_by_name("preview"),       kprev,   sizeof kprev);
    key_display_name(binding_key_by_name("sort_next"),     ksortn,  sizeof ksortn);
    key_display_name(binding_key_by_name("sort_reverse"),  ksortr,  sizeof ksortr);
    key_display_name(binding_key_by_name("search"),        ksearch, sizeof ksearch);
    key_display_name(binding_key_by_name("refresh"),       kref,    sizeof kref);
    key_display_name(binding_key_by_name("refresh_full"),  kfull,   sizeof kfull);
    key_display_name(binding_key_by_name("quit"),          kquit,   sizeof kquit);
    key_display_name(binding_key_by_name("help"),          khelp,   sizeof khelp);
    help_key = binding_key_by_name("help");

#define ADD(...) do { \
    if (total < HELP_MAX_LINES) { \
        snprintf(lines_storage[total], HELP_LINE_MAX, __VA_ARGS__); \
        lines[total] = lines_storage[total]; \
        total++; \
    } \
} while (0)
#define ADD_EMPTY() do { \
    if (total < HELP_MAX_LINES) { \
        lines_storage[total][0] = '\0'; \
        lines[total] = lines_storage[total]; \
        total++; \
    } \
} while (0)

    ADD("========================================================================");
    ADD("__     __  ___   ____   _____  __   __  _____   ___   _      _____ ");
    ADD("\\ \\   / / |_ _| | __ ) | ____| \\ \\ / / |  ___| |_ _| | |    | ____|");
    ADD(" \\ \\ / /   | |  |  _ \\ |  _|    \\ V /  | |_     | |  | |    |  _|  ");
    ADD("  \\ V /    | |  | |_) || |___    | |   |  _|    | |  | |___ | |___ ");
    ADD("   \\_/    |___| |____/ |_____|   |_|   |_|     |___| |_____||_____|");
    ADD_EMPTY();
    ADD("========================================================================");
    ADD_EMPTY();
    ADD("Навигация:");
    ADD("  %s/↓, %s/↑ - Вверх / Вниз",              kdown, kup);
    ADD("  %s/←/Backspace - Вверх по каталогам",     kparent);
    ADD("  %s/%s/→/Enter - Войти / Открыть",          kopenl, kopen);
    ADD("  %s / %s - В начало / В конец списка",     khome, kend);
    ADD("  Tab - Переключение панелей (в сплите)");
    ADD("  %s - Выделить / Снять выделение",        kselect);
    ADD_EMPTY();
    ADD("Действия и вкладки:");
    ADD("  %s / [A] - Меню действий",               kmenu);
    ADD("  %s / [=] - Избранное",                    kbook);
    ADD("  %s / [O] - Настройки",                    ksett);
    ADD("  %s - Вкл/выкл сплит",                     ksplit);
    ADD("  %s - Вкл/выкл предпросмотр",              kprev);
    ADD("  %s / %s - Сортировка / Реверс",           ksortn, ksortr);
    ADD("  %s - Поиск по имени",                     ksearch);
    ADD("  %s - Обновить список",                    kref);
    ADD("  %s - Перерисовать экран",                 kfull);
    ADD("  F1..F4 - Переключение вкладок");
    ADD("  %s / [X] - Выход",                        kquit);
    ADD("  %s - Эта справка",                        khelp);
    ADD_EMPTY();
    ADD("Клавиши можно переназначить в Настройках (%s).", ksett);
    ADD_EMPTY();
    ADD("Разработано AINOnoko с помощью ИИ.");
    ADD("VibeyFile распространяется под лицензией GPLv3.");

#undef ADD
#undef ADD_EMPTY

    for (i = 0; i < total; i++) {
        int len = utf8_strlen(lines[i]);
        if (len > max_len) max_len = len;
    }
    if (max_len < 55) max_len = 55;
    getmaxyx(stdscr, rows, cols);
    w = max_len + 6; if (w > cols - 4) w = cols - 4; if (w < 40) w = 40;
    h = (rows * 64) / 100;
    if (h > rows - 4) h = rows - 4;
    if (h < 10) h = 10;
    y = (rows - h) / 2; x = (cols - w) / 2;
    vis = h - 4 > 0 ? h - 4 : 1;
    max_scroll = total - vis > 0 ? total - vis : 0;
    apply_shadow(y, x, h, w);
    win = newwin(h, w, y, x);
    if (!win) return;
    wbkgd(win, COLOR_PAIR(PAIR_DIALOG));
    ui_prepare_win(win);
    while (running && g_app.running) {
        int ch;
        help_draw(win, lines, total, scroll, vis, h, w);
        ch = wgetch(win);
        if (ch == ERR) continue;
        if (ch == 'q' || ch == 'Q' || ch == 27
            || (help_key && ch == help_key)
            || ch == '\n' || ch == KEY_ENTER) { running = 0; }
        else if (ch == KEY_DOWN || ch == 'j') {
            if (scroll < max_scroll) scroll++;
        } else if (ch == KEY_UP || ch == 'k') {
            if (scroll > 0) scroll--;
        } else if (ch == KEY_NPAGE || ch == CTRL('d')) {
            scroll += vis; if (scroll > max_scroll) scroll = max_scroll;
        } else if (ch == KEY_PPAGE || ch == CTRL('u')) {
            scroll -= vis; if (scroll < 0) scroll = 0;
        } else if (ch == KEY_HOME || ch == 'g') scroll = 0;
        else if (ch == KEY_END || ch == 'G') scroll = max_scroll;
    }
    delwin(win);
    touchwin(stdscr);
    refresh();
}
