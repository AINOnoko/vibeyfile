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
            /* C: void* → char** преобразуется неявно и корректно, приведение
             * только скрыло бы ошибку уровня указателя. clang-tidy ругается
             * по правилам C++ (там так нельзя) — здесь это ложное. */
            char **tmp = realloc(lines, (size_t)ncap * sizeof(char *));
            if (!tmp) { free(lines); *out = NULL; *count = -1; goto cleanup; }
            lines = tmp;
            line_cap = ncap;
        }
        lines[line_count++] = line;
        line = strtok_r(NULL, "\n", &save);
    }
    *out = lines;
    *count = line_count;
cleanup:
    return;
}

/* Рисует одну строку в окне. Если строка не помещается по ширине —
 * крутит её как бегущую строку (по тому же принципу, что в шапке). */
static void draw_scroll_line(WINDOW *win, int y, int x, int w, const char *s)
{
    /* 8.3 КиБ BSS. Статики безопасны: dialog.c не реентрантен по дизайну
     * (одно окно за раз) и не вызывается из signal handler'ов.
     * ВАЖНО: это единственный «дорогой» BSS в проекте — вынести в кучу
     * смысла нет, размер фиксирован. */
    static char padded[8192];
    static char out[8300];
    int len, i, speed, gap, total, offset;
    long long ms;
    struct timespec now;
    size_t bytes, slen, pos, cap, start_byte, b1, b2;
    const char *start_ptr;
    int chars_in_first;

    if (!win || w < 1 || !s) return;
    len = utf8_strlen(s);
    if (len <= w) {
        bytes = utf8_prefix_bytes(s, w);
        mvwaddnstr(win, y, x, s, (int)bytes);
        return;
    }

    clock_gettime(CLOCK_MONOTONIC, &now);
    ms = (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
    speed = 150;       /* мс на один символ */
    gap = 10;          /* пробелов после конца строки */
    total = len + gap;
    offset = (int)((ms / speed) % total);

    cap = sizeof padded - 1;
    slen = strlen(s);
    if (slen > cap) slen = cap;
    memcpy(padded, s, slen);
    pos = slen;
    for (i = 0; i < gap && pos < cap; i++) padded[pos++] = ' ';
    padded[pos] = '\0';

    start_byte = utf8_prefix_bytes(padded, offset);
    start_ptr = padded + start_byte;
    chars_in_first = utf8_strlen(start_ptr);
    if (chars_in_first >= w) {
        bytes = utf8_prefix_bytes(start_ptr, w);
        mvwaddnstr(win, y, x, start_ptr, (int)bytes);
    } else {
        b1 = strlen(start_ptr);
        if (b1 >= sizeof out) b1 = sizeof out - 1;
        memcpy(out, start_ptr, b1);
        b2 = utf8_prefix_bytes(padded, w - chars_in_first);
        if (b1 + b2 >= sizeof out) b2 = sizeof out - 1 - b1;
        memcpy(out + b1, padded, b2);
        out[b1 + b2] = '\0';
        mvwaddnstr(win, y, x, out, (int)(b1 + b2));
    }
}

static void draw_msg_body(WINDOW *win, char **lines, int line_count,
                          int vis, int scroll, int w)
{
    int i;
    int avail = w - 4;
    if (!lines || avail < 1) return;
    for (i = 0; i < vis && scroll + i < line_count; i++)
        draw_scroll_line(win, 1 + i, 2, avail, lines[scroll + i]);
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

/* ФИКС (clang-tidy, bugprone-branch-clone): пять одинаковых пар
 * wtimeout(-1)+timeout(-1) выглядели как копипаста, но это ОДНА операция —
 * «снять таймаут подсказки и отдать клавиатуру вызывающему». Вынесено в
 * функцию: если завтра снимать таймаут придётся иначе (например, ещё и
 * flushing), правится одно место. Поведение не изменилось. */
static void dialog_release_keyboard(WINDOW *win)
{
    wtimeout(win, -1);
    timeout(-1);
}

static int msg_loop(WINDOW *win, const char *title, char **lines,
                    int line_count, int h, int w, int max_scroll, int vis,
                    int x, int y)
{
    int scroll = 0;
    /* Win-таймаут: нужен для анимации бегущей строки.
     * stdscr-таймаут — на случай, если сюда попадут «голые» getch() */
    wtimeout(win, 100);
    timeout(100);
    while (g_app.running) {
        int ch;
        werase(win);
        box(win, 0, 0);
        if (title && title[0])
            mvwaddn_utf8(win, 0, 2, title, w - 4);
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
            mvwaddn_utf8(win, h - 2, 2, footer, w - 4);
        } else {
            mvwaddn_utf8(win, h - 2, 2, T(STR_MSG_ANY_KEY), w - 4);
        }
        wrefresh(win);

        ch = get_mapped_key(win);

        if (ch == KEY_RESIZE) {
            dialog_release_keyboard(win);
            return 1;
        }
        if (ch == KEY_MOUSE) {
            MEVENT ev;
            if (getmouse(&ev) == OK) {
                if (ev.bstate & BUTTON1_CLICKED) {
                    if (ev.x < x || ev.x >= x + w || ev.y < y || ev.y >= y + h) {
                        dialog_release_keyboard(win);
                        return 0;
                    }
                } else if (ev.bstate & BUTTON4_PRESSED) {
                    if (scroll > 0) scroll--;
                } else if (ev.bstate & BUTTON5_PRESSED) {
                    if (scroll < max_scroll) scroll++;
                }
            }
            continue;
        }
        if (ch == ERR) continue;
        /* ФИКС (clang-tidy, bugprone-branch-clone): «закрыть по клавише
         * выхода» и «закрыть, раз прокручивать нечего» имели одинаковое
         * тело — сведены в одно условие. Пробел добавлен намеренно: он
         * РАНЬШЕ доходил до ветки max_scroll == 0 и закрывал окно, а
         * теперь эта ветка стала явной, и без перечисления пробела
         * подсказка «нажмите любую клавишу» перестала бы срабатывать на
         * самой ожидаемой клавише. Найдено при перечитывании правки,
         * не тестом — поэтому и вынесено в комментарий.
         *
         * СНАЧАЛА прокрутка — она забирает j/k/PgUp/PgDn и стрелки даже
         * при max_scroll == 0 (там они просто ничего не меняют). Если
         * поставить max_scroll == 0 в условия выхода, эти клавиши начнут
         * закрывать окно вместо того, чтобы быть проглоченными, — то есть
         * "нажмите любую клавишу" перестанет срабатывать на стрелках. */
        if (msg_scroll_key(ch, &scroll, max_scroll, vis))
            continue;
        if (ch == 'q' || ch == 'Q' || ch == 27 || ch == '\n'
            || ch == KEY_ENTER || ch == ' ' || max_scroll == 0) {
            dialog_release_keyboard(win);
            return 0;
        }
    }
    dialog_release_keyboard(win);
    return 0;
}

void message_box(const char *title, const char *msg)
{
    int rows, cols, w, h, y, x, vis, max_scroll, line_count;
    char *buf;
    char **lines = NULL;
    int max_chars;
    int i;
    WINDOW *win = NULL;
    if (!msg) msg = "";
    if (!title) title = "";

    getmaxyx(stdscr, rows, cols);
    buf = strdup(msg);
    if (!buf) return;
    collect_msg_lines(buf, &lines, &line_count);
    if (line_count < 0) { free(lines); free(buf); return; }   /* free(NULL) — норма */

    max_chars = utf8_strlen(title);
    for (i = 0; i < line_count; i++) {
        int l = utf8_strlen(lines[i]);
        if (l > max_chars) max_chars = l;
    }

    while (g_app.running) {
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
        if (!win) break;
        wbkgd(win, COLOR_PAIR(PAIR_DIALOG));
        ui_prepare_win(win);

        int resized = msg_loop(win, title, lines, line_count, h, w, max_scroll, vis, x, y);

        delwin(win);
        win = NULL;
        clearok(stdscr, TRUE);   /* ФИКС: артефакты рамки — см. actions.c */
        touchwin(stdscr);
        refresh();

        if (!resized) break;

        getmaxyx(stdscr, rows, cols);
    }

    free(lines);
    free(buf);
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

typedef struct { int w, h, y, x; } BoxGeom;

static void input_geom(BoxGeom *g)
{
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    g->w = cols - 20;
    if (g->w > 76) g->w = 76;
    if (g->w < 40) g->w = 40;
    if (g->w > cols - 2 && cols > 2) g->w = cols - 2;
    g->h = 7;
    g->y = (rows - g->h) / 2;
    g->x = (cols - g->w) / 2;
    if (g->y < 0) g->y = 0;
    if (g->x < 0) g->x = 0;
}

static WINDOW *input_open(const BoxGeom *g)
{
    WINDOW *win;
    apply_shadow(g->y, g->x, g->h, g->w);
    win = newwin(g->h, g->w, g->y, g->x);
    if (!win) return NULL;
    wbkgd(win, COLOR_PAIR(PAIR_DIALOG));
    ui_prepare_win(win);
    return win;
}

static int input_click_out(const BoxGeom *g)
{
    MEVENT ev;
    if (getmouse(&ev) != OK) return 0;
    if (!(ev.bstate & BUTTON1_CLICKED)) return 0;
    return ev.x < g->x || ev.x >= g->x + g->w
        || ev.y < g->y || ev.y >= g->y + g->h;
}

static void input_backspace(char *buf, int *len)
{
    /* ФИКС (clang-tidy, bugprone-inc-dec-in-conditions): тот же случай,
     * что в keys.c — декремент внутри индекса. Поведение сохранено
     * дословно: снимаем байты-продолжения UTF-8, начиная с последнего. */
    while (*len > 0) {
        (*len)--;
        if (((unsigned char)buf[*len] & 0xC0) != 0x80) break;
    }
    buf[*len] = '\0';
}

/* cap — реальная ёмкость buf. Раньше стоял литерал PATH_MAX - 1, из-за
 * чего при буфере меньше PATH_MAX запись шла за границу (переполнение
 * стека при вводе длинного имени). */
static int input_key(int ch, char *buf, int *len, size_t cap)
{
    if (ch == '\n' || ch == KEY_ENTER) return 1;
    if (ch == 27) { *len = -1; return 1; }
    if (ch == 127 || ch == KEY_BACKSPACE) {
        input_backspace(buf, len);
        return 0;
    }
    if (ch < 32 || ch > 255 || (size_t)*len + 1 >= cap) return 0;
    buf[(*len)++] = (char)ch;
    buf[*len] = '\0';
    return 0;
}

static void input_paint(WINDOW *win, const BoxGeom *g,
                        const char *title, const char *prompt, const char *buf)
{
    werase(win);
    box(win, 0, 0);
    mvwaddn_utf8(win, 0, 2, title ? title : "", g->w - 4);
    mvwaddn_utf8(win, 2, 2, prompt ? prompt : "", g->w - 4);
    draw_input_line(win, g->w, buf);
    wrefresh(win);
}

int dialog_input(const char *title, const char *prompt,
                 char *out, size_t outsz)
{
    BoxGeom g;
    WINDOW *win;
    /* ЭТАП 1: FM_PATH_MAX (1024) вместо PATH_MAX (4096) на стеке */
    char buf[FM_PATH_MAX];
    int len, ch, done = 0;
    if (!out || outsz == 0) return 0;
    input_geom(&g);
    win = input_open(&g);
    if (!win) return 0;
    copy_str(buf, sizeof buf, out);
    len = (int)strlen(buf);
    curs_set(1);
    timeout(100);
    while (!stopped() && !done) {
        /* getch()/wgetch() прерываются сигналами (EINTR), но не
         * выставляют running сами — опрашиваем каждый цикл. */
        app_poll_signals();
        if (stopped()) break;
        input_paint(win, &g, title, prompt, buf);
        ch = wgetch(win);
        if (ch == KEY_RESIZE) {
            delwin(win);
            input_geom(&g);
            win = input_open(&g);
            if (!win) { timeout(-1); curs_set(0); return 0; }
            continue;
        }
        if (ch == KEY_MOUSE) { if (input_click_out(&g)) { len = -1; done = 1; } continue; }
        if (ch == ERR) continue;
        done = input_key(ch, buf, &len, sizeof buf);
    }
    timeout(-1);
    curs_set(0);
    delwin(win);
    clearok(stdscr, TRUE);       /* ФИКС: артефакты рамки */
    touchwin(stdscr);
    refresh();
    if (len < 0 || stopped()) return 0;
    copy_str(out, outsz, buf);
    return 1;
}

static int confirm_read_key(WINDOW *win, int *btn_sel, int y_yes, int y_no,
                            int win_y, int win_x, int btn_x, int btn_w,
                            int w, int h)
{
    int ch = get_mapped_key(win);
    if (ch == KEY_RESIZE) return KEY_RESIZE;
    if (ch == KEY_MOUSE) {
        MEVENT ev;
        if (getmouse(&ev) == OK) {
            if (ev.bstate & BUTTON1_CLICKED) {
                if (ev.x < win_x || ev.x >= win_x + w || ev.y < win_y || ev.y >= win_y + h) {
                    return -1;
                }
                int wy = ev.y - win_y;
                int wx = ev.x - win_x;
                if (wx >= btn_x && wx < btn_x + btn_w) {
                    if (wy == y_yes) return 1;
                    if (wy == y_no)  return -1;
                }
            } else if (ev.bstate & BUTTON4_PRESSED) {
                *btn_sel = 0;
            } else if (ev.bstate & BUTTON5_PRESSED) {
                *btn_sel = 1;
            }
        }
        return 0;
    }
    if (ch == ERR) return stopped() ? -1 : 0;

    if (ch == 'y' || ch == 'Y') return 1;
    if (ch == 'n' || ch == 'N' || ch == 27) return -1;

    if (ch == KEY_UP || ch == KEY_DOWN) { *btn_sel = 1 - *btn_sel; return 0; }
    if (ch == '\n' || ch == KEY_ENTER) return (*btn_sel == 0) ? 1 : -1;

    return 0;
}

static int button_width(void)
{
    int yw = utf8_strlen(T(STR_BTN_YES));
    int nw = utf8_strlen(T(STR_BTN_NO));
    int w = (yw > nw ? yw : nw) + 4;
    if (w < 10) w = 10;
    return w;
}

int dialog_confirm(const char *title, const char *prompt)
{
    int rows, cols, w, h, y, x, ch, running = 1, res = 0;
    int btn_sel = 0, btn_w, btn_x, y_yes = 4, y_no = 6;
    int yes_w, no_w;
    WINDOW *win = NULL;

    btn_w = button_width();
    yes_w = utf8_strlen(T(STR_BTN_YES));
    no_w  = utf8_strlen(T(STR_BTN_NO));

    getmaxyx(stdscr, rows, cols);
    w = cols - 20;
    if (w > 76) w = 76;
    if (w < 40) w = 40;
    if (w > cols - 2 && cols > 2) w = cols - 2;
    if (btn_w > w - 4) btn_w = w - 4;   /* защита от переполнения */
    if (btn_w < 4) btn_w = 4;
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

    timeout(100);
    while (running && !stopped()) {
        int x_yes = btn_x + (btn_w - yes_w) / 2;
        int x_no  = btn_x + (btn_w - no_w) / 2;
        werase(win);
        box(win, 0, 0);
        mvwaddn_utf8(win, 0, 2, title ? title : "", w - 4);
        mvwaddn_utf8(win, 2, 2, prompt ? prompt : "", w - 4);
        wattron(win, btn_sel == 0 ? A_BUTTON_SEL : A_BUTTON);
        mvwaddn_utf8(win, y_yes, x_yes, T(STR_BTN_YES), yes_w);
        wattroff(win, btn_sel == 0 ? A_BUTTON_SEL : A_BUTTON);
        wattron(win, btn_sel == 1 ? A_BUTTON_SEL : A_BUTTON);
        mvwaddn_utf8(win, y_no, x_no, T(STR_BTN_NO), no_w);
        wattroff(win, btn_sel == 1 ? A_BUTTON_SEL : A_BUTTON);
        wrefresh(win);

        ch = confirm_read_key(win, &btn_sel, y_yes, y_no, y, x, btn_x, btn_w, w, h);

        if (ch == KEY_RESIZE) {
            delwin(win);
            getmaxyx(stdscr, rows, cols);
            w = cols - 20; if (w > 76) w = 76; if (w < 40) w = 40;
            if (w > cols - 2 && cols > 2) w = cols - 2;
            if (btn_w > w - 4) btn_w = w - 4;
            if (btn_w < 4) btn_w = 4;
            h = 9;
            y = (rows - h) / 2;
            x = (cols - w) / 2;
            if (y < 0) y = 0;
            if (x < 0) x = 0;
            apply_shadow(y, x, h, w);
            win = newwin(h, w, y, x);
            if (!win) { timeout(-1); return 0; }
            wbkgd(win, COLOR_PAIR(PAIR_DIALOG));
            ui_prepare_win(win);
            btn_x = (w - btn_w) / 2;
            continue;
        }
        if (ch == ERR) continue;

        if (ch == 1)  { res = 1; running = 0; }
        if (ch == -1) { res = 0; running = 0; }
    }
    timeout(-1);
    delwin(win);
    touchwin(stdscr);
    refresh();
    return res;
}

void show_progress(int pct, const char *filename, WINDOW *win)
{
    int w, fill, i;
    if (!win) return;
    if (pct < -1) pct = -1;
    if (pct > 100) pct = 100;
    if (!filename) filename = "";
    werase(win);
    box(win, 0, 0);
    mvwaddnstr(win, 0, 2, T(STR_MSG_PROGRESS_TITLE), 20);
    mvwaddn_utf8(win, 2, 2, filename, getmaxx(win) - 4);

    if (pct == -1) {
        mvwprintw(win, 4, 4, "%s", T(STR_MSG_PROGRESS_CALC));
    } else {
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
    }
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
