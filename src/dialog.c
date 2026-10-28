#include "fm.h"

void ui_prepare_win(WINDOW *win)
{
    if (!win) return;
    keypad(win, TRUE);
    meta(win, TRUE);
}

static int stopped(void)
{
    return !g_app.running;
}

static void collect_msg_lines(char *msg, char ***out, int *count)
{
    char **lines = NULL;
    int line_count = 0, line_cap = 0;
    char *save = NULL;
    char *line;
    if (!msg) { *out = NULL; *count = 0; return; }
    line = strtok_r(msg, "\n", &save);
    while (line) {
        if (line_count == line_cap) {
            int ncap = line_cap ? line_cap * 2 : 16;
            char **tmp = realloc(lines, (size_t)ncap * sizeof(char *));
            if (!tmp) { free(lines); *out = NULL; *count = -1; return; }
            lines = tmp;
            line_cap = ncap;
        }
        lines[line_count++] = line;
        line = strtok_r(NULL, "\n", &save);
    }
    *out = lines;
    *count = line_count;
}

static void draw_msg_body(WINDOW *win, char **lines, int line_count,
                          int vis, int scroll, int w)
{
    int i;
    if (!lines) return;
    for (i = 0; i < vis && scroll + i < line_count; i++) {
        const char *ln = lines[scroll + i];
        size_t lbytes = utf8_prefix_bytes(ln, w - 4);
        mvwaddnstr(win, 1 + i, 2, ln, (int)lbytes);
    }
}

static int msg_scroll_key(int ch, int *scroll, int max_scroll, int vis)
{
    if (ch == KEY_DOWN || ch == 'j') {
        if (*scroll < max_scroll) (*scroll)++;
    } else if (ch == KEY_UP || ch == 'k') {
        if (*scroll > 0) (*scroll)--;
    } else if (ch == KEY_NPAGE || ch == CTRL('d')) {
        *scroll += vis;
        if (*scroll > max_scroll) *scroll = max_scroll;
    } else if (ch == KEY_PPAGE || ch == CTRL('u')) {
        *scroll -= vis;
        if (*scroll < 0) *scroll = 0;
    } else if (ch == KEY_HOME) {
        *scroll = 0;
    } else if (ch == KEY_END) {
        *scroll = max_scroll;
    } else {
        return 0;
    }
    return 1;
}

static void msg_loop(WINDOW *win, const char *title, char **lines,
                     int line_count, int h, int w, int max_scroll, int vis)
{
    int scroll = 0;
    int running = 1;
    while (running && !stopped()) {
        int ch;
        werase(win);
        box(win, 0, 0);
        if (title && title[0])
            mvwaddnstr(win, 0, 2, title, w - 4);
        if (max_scroll > 0) {
            int bar_x = w - 2, bar_h = h - 3, i;
            int thumb_h = line_count > 0 ? (bar_h * vis) / line_count : 1;
            int thumb_pos;
            if (thumb_h < 1) thumb_h = 1;
            if (bar_h < 1) bar_h = 1;
            thumb_pos = (bar_h * scroll) / (max_scroll > 0 ? max_scroll : 1);
            if (thumb_pos + thumb_h > bar_h) thumb_pos = bar_h - thumb_h;
            if (thumb_pos < 0) thumb_pos = 0;
            for (i = 0; i < bar_h; i++) {
                if (i >= thumb_pos && i < thumb_pos + thumb_h) {
                    wattron(win, A_REVERSE);
                    mvwaddch(win, 1 + i, bar_x, ' ');
                    wattroff(win, A_REVERSE);
                }
            }
        }
        draw_msg_body(win, lines, line_count, vis, scroll, w);
        if (max_scroll > 0) {
            char footer[160];
            snprintf(footer, sizeof footer,
                     "j/k, PgUp/PgDn | q/Esc/Enter [%d/%d]",
                     scroll + 1, max_scroll + 1);
            mvwaddnstr(win, h - 2, 2, footer, w - 4);
        } else {
            mvwaddnstr(win, h - 2, 2, "Нажмите любую клавишу...", w - 4);
        }
        wrefresh(win);
        ch = wgetch(win);
        if (ch == ERR) {
            if (stopped()) running = 0;
            continue;
        }
        if (ch == 'q' || ch == 'Q' || ch == 27 || ch == '\n'
            || ch == KEY_ENTER) {
            running = 0;
        } else if (msg_scroll_key(ch, &scroll, max_scroll, vis)) {
            /* handled */
        } else if (max_scroll == 0) {
            running = 0;
        }
    }
}

void message_box(const char *title, const char *msg)
{
    int rows, cols, w, h, y, x, vis, max_scroll, line_count;
    char *buf;
    char **lines = NULL;
    int max_chars;
    int i;
    WINDOW *win;
    if (!msg) msg = "";
    if (!title) title = "";
    getmaxyx(stdscr, rows, cols);
    buf = strdup(msg);
    if (!buf) return;
    collect_msg_lines(buf, &lines, &line_count);
    if (line_count < 0) { free(buf); return; }
    max_chars = utf8_strlen(title);
    for (i = 0; i < line_count; i++) {
        int l = utf8_strlen(lines[i]);
        if (l > max_chars) max_chars = l;
    }
    w = max_chars + 6;
    if (w > cols - 4) w = cols - 4;
    if (w < 40) w = 40;
    if (w > cols - 2 && cols > 2) w = cols - 2;
    if (w < 8) w = 8;
    h = line_count + 4;
    if (h > rows - 4) h = rows - 4;
    if (h < 6) h = 6;
    if (h > rows - 2 && rows > 2) h = rows - 2;
    y = (rows - h) / 2;
    x = (cols - w) / 2;
    if (y < 0) y = 0;
    if (x < 0) x = 0;
    vis = h - 4 > 0 ? h - 4 : 1;
    max_scroll = line_count - vis > 0 ? line_count - vis : 0;
    apply_shadow(y, x, h, w);
    win = newwin(h, w, y, x);
    if (!win) { free(lines); free(buf); return; }
    wbkgd(win, COLOR_PAIR(PAIR_DIALOG));
    ui_prepare_win(win);
    msg_loop(win, title, lines, line_count, h, w, max_scroll, vis);
    delwin(win);
    free(lines);
    free(buf);
    touchwin(stdscr);
    refresh();
}

static void draw_input_line(WINDOW *win, int w, const char *buf)
{
    int maxc = w - 4;
    int chars, skip = 0;
    const char *show;
    size_t bytes;
    if (maxc < 1) maxc = 1;
    chars = utf8_strlen(buf);
    if (chars > maxc) skip = chars - maxc;
    show = buf + utf8_prefix_bytes(buf, skip);
    bytes = utf8_prefix_bytes(show, maxc);
    mvwaddnstr(win, 3, 2, show, (int)bytes);
    wmove(win, 3, 2 + utf8_strlen(show));
}

int dialog_input(const char *title, const char *prompt,
                 char *out, size_t outsz)
{
    int rows, cols, w, h, y, x, len, ch;
    WINDOW *win;
    char buf[PATH_MAX];
    if (!out || outsz == 0) return 0;
    getmaxyx(stdscr, rows, cols);
    w = cols - 20;
    if (w > 76) w = 76;
    if (w < 40) w = 40;
    if (w > cols - 2 && cols > 2) w = cols - 2;
    h = 7;
    y = (rows - h) / 2;
    x = (cols - w) / 2;
    if (y < 0) y = 0;
    if (x < 0) x = 0;
    apply_shadow(y, x, h, w);
    win = newwin(h, w, y, x);
    if (!win) return 0;
    wbkgd(win, COLOR_PAIR(PAIR_DIALOG));
    ui_prepare_win(win);
    copy_str(buf, sizeof buf, out);
    len = (int)strlen(buf);
    curs_set(1);
    while (!stopped()) {
        werase(win);
        box(win, 0, 0);
        mvwaddnstr(win, 0, 2, title ? title : "", w - 4);
        mvwaddnstr(win, 2, 2, prompt ? prompt : "", w - 4);
        draw_input_line(win, w, buf);
        wrefresh(win);
        ch = wgetch(win);
        if (ch == ERR) continue;
        if (ch == '\n' || ch == KEY_ENTER) break;
        if (ch == 27) { len = -1; break; }
        if (ch == 127 || ch == KEY_BACKSPACE) {
            while (len > 0 && ((unsigned char)buf[--len] & 0xC0) == 0x80) {}
            buf[len] = '\0';
        } else if (ch >= 32 && ch <= 255) {
            if (len < (int)sizeof buf - 1) {
                buf[len++] = (char)ch;
                buf[len] = '\0';
            }
        }
    }
    curs_set(0);
    delwin(win);
    touchwin(stdscr);
    refresh();
    if (len < 0 || stopped()) return 0;
    copy_str(out, outsz, buf);
    return 1;
}

static int confirm_read_key(WINDOW *win, int *btn_sel, int y_yes, int y_no,
                            int win_y, int win_x, int btn_x, int btn_w)
{
    int ch = wgetch(win);
    if (ch == ERR) return stopped() ? -1 : 0;
    if (ch == 'y' || ch == 'Y') return 1;
    if (ch == 'n' || ch == 'N' || ch == 27) return -1;
    if (ch == 0xD0) {
        int c2 = wgetch(win);
        if (c2 == ERR) return 0;
        if (c2 == 0xB4 || c2 == 0x94) return 1;
        if (c2 == 0xBD || c2 == 0x9D) return -1;
        return 0;
    }
    if (ch == KEY_UP || ch == KEY_DOWN) { *btn_sel = 1 - *btn_sel; return 0; }
    if (ch == '\n' || ch == KEY_ENTER) return (*btn_sel == 0) ? 1 : -1;
    if (ch == KEY_MOUSE) {
        MEVENT ev;
        if (getmouse(&ev) == OK
            && (ev.bstate & (BUTTON1_CLICKED | BUTTON1_RELEASED))) {
            int wy = ev.y - win_y;
            int wx = ev.x - win_x;
            if (wx >= btn_x && wx < btn_x + btn_w) {
                if (wy == y_yes) return 1;
                if (wy == y_no)  return -1;
            }
        }
    }
    return 0;
}

int dialog_confirm(const char *title, const char *prompt)
{
    int rows, cols, w, h, y, x, ch, running = 1, res = 0;
    int btn_sel = 0, btn_w = 10, btn_x, y_yes = 4, y_no = 6;
    WINDOW *win;
    getmaxyx(stdscr, rows, cols);
    w = cols - 20;
    if (w > 76) w = 76;
    if (w < 40) w = 40;
    if (w > cols - 2 && cols > 2) w = cols - 2;
    h = 9;
    y = (rows - h) / 2;
    x = (cols - w) / 2;
    if (y < 0) y = 0;
    if (x < 0) x = 0;
    apply_shadow(y, x, h, w);
    win = newwin(h, w, y, x);
    if (!win) return 0;
    wbkgd(win, COLOR_PAIR(PAIR_DIALOG));
    ui_prepare_win(win);
    btn_x = (w - btn_w) / 2;
    while (running && !stopped()) {
        werase(win);
        box(win, 0, 0);
        mvwaddnstr(win, 0, 2, title ? title : "", w - 4);
        mvwaddnstr(win, 2, 2, prompt ? prompt : "", w - 4);
        wattron(win, btn_sel == 0 ? A_BUTTON_SEL : A_BUTTON);
        mvwprintw(win, y_yes, btn_x, "[   Да   ]");
        wattroff(win, btn_sel == 0 ? A_BUTTON_SEL : A_BUTTON);
        wattron(win, btn_sel == 1 ? A_BUTTON_SEL : A_BUTTON);
        mvwprintw(win, y_no, btn_x, "[   Нет  ]");
        wattroff(win, btn_sel == 1 ? A_BUTTON_SEL : A_BUTTON);
        wrefresh(win);
        ch = confirm_read_key(win, &btn_sel, y_yes, y_no, y, x, btn_x, btn_w);
        if (ch == 1)  { res = 1; running = 0; }
        if (ch == -1) { res = 0; running = 0; }
    }
    delwin(win);
    touchwin(stdscr);
    refresh();
    return res;
}

void show_progress(int pct, const char *filename, WINDOW *win)
{
    int w, fill, i;
    if (!win) return;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    if (!filename) filename = "";
    werase(win);
    box(win, 0, 0);
    mvwaddnstr(win, 0, 2, " Копирование ", 20);
    mvwaddnstr(win, 2, 2, filename, getmaxx(win) - 4);
    w = getmaxx(win) - 10;
    if (w < 10) w = 10;
    fill = (w * pct) / 100;
    mvwaddch(win, 4, 4, '[');
    for (i = 0; i < w; i++) {
        if (i < fill) wattron(win, A_REVERSE);
        else wattroff(win, A_REVERSE);
        mvwaddch(win, 4, 5 + i, ' ');
    }
    wattroff(win, A_REVERSE);
    mvwaddch(win, 4, 5 + w, ']');
    mvwprintw(win, 6, 4, "%d%%", pct);
    wrefresh(win);
}

WINDOW *progress_init(void)
{
    int rows, cols, w = 60, h = 9, y, x;
    WINDOW *win;
    getmaxyx(stdscr, rows, cols);
    if (w > cols - 4) w = cols - 4;
    if (w < 20) w = cols > 4 ? cols - 4 : cols;
    y = (rows - h) / 2;
    x = (cols - w) / 2;
    if (y < 0) y = 0;
    if (x < 0) x = 0;
    apply_shadow(y, x, h, w);
    win = newwin(h, w, y, x);
    if (!win) return NULL;
    wbkgd(win, COLOR_PAIR(PAIR_DIALOG));
    ui_prepare_win(win);
    return win;
}
