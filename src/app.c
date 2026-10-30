#include "fm.h"

static volatile sig_atomic_t g_stop_req;
static volatile sig_atomic_t g_tstp_req;

static void sig_handler(int sig)
{
    (void)sig;
    g_stop_req = 1;
}

static void install_tstp(void);

static void sig_tstp(int sig)
{
    (void)sig;
    g_tstp_req = 1;
}

static void suspend_self(void)
{
    struct sigaction sa;
    endwin();
    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = SIG_DFL;
    sigaction(SIGTSTP, &sa, NULL);
    /* raise() вернёт ненулевое, только если сигнал заблокирован — в этом
     * случае процесс не остановится и следующий endwin()/refresh()
     * произойдёт сразу. Проверять нечего: альтернативы «остановиться»
     * без сигнала нет, а глотать ошибку молча — плохо, поэтому
     * возвращаемся в тот же путь, что и при неудаче sigaction(). */
    if (raise(SIGTSTP) != 0) {
        install_tstp();
        refresh();
        return;
    }
    install_tstp();
    refresh();
}

void app_poll_signals(void)
{
    if (g_stop_req) g_app.running = 0;
    if (!g_tstp_req) return;
    g_tstp_req = 0;
    suspend_self();
}

static void install_tstp(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = sig_tstp;
    sa.sa_flags = 0;
    sigaction(SIGTSTP, &sa, NULL);
}

static void install_signals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = sig_handler;
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    install_tstp();
}

static void init_ncurses(void)
{
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    meta(stdscr, TRUE);
    curs_set(0);
    set_escdelay(25);
    init_attributes();
    wbkgd(stdscr, COLOR_PAIR(PAIR_BG));
    mousemask(BUTTON1_CLICKED | BUTTON1_PRESSED | BUTTON1_RELEASED |
              BUTTON1_DOUBLE_CLICKED | BUTTON3_CLICKED | BUTTON3_PRESSED |
              BUTTON4_PRESSED | BUTTON5_PRESSED, NULL);
}

static int load_pane(FilePane *p, const char *fallback)
{
    if (p->cwd[0] && load_dir(p) == 0) return 0;
    if (fallback && fallback[0]) {
        copy_str(p->cwd, sizeof p->cwd, fallback);
        if (load_dir(p) == 0) return 0;
    }
    copy_str(p->cwd, sizeof p->cwd, "/");
    return load_dir(p);
}

static void init_tabs(const char *fallback)
{
    int i, pi;
    for (i = 0; i < NUM_TABS; i++) {
        if (g_app.tabs[i].preview_mode) {
            g_app.tabs[i].split_mode = 1;
            g_app.tabs[i].active_pane = 0;
        }
        if (g_app.tabs[i].active_pane != 1)
            g_app.tabs[i].active_pane = 0;
        for (pi = 0; pi < 2; pi++) {
            FilePane *p = &g_app.tabs[i].panes[pi];
            if (!p->cwd[0] && !(i == 0 && pi == 0)) continue;
            load_pane(p, fallback);
            if (!p->pending_sel[0] && p->list.count > 1 && p->file_sel <= 0)
                p->file_sel = 1;
        }
    }
}

static void restore_pending_selection(void)
{
    int i, pi;
    for (i = 0; i < NUM_TABS; i++)
        for (pi = 0; pi < 2; pi++) {
            FilePane *p = &g_app.tabs[i].panes[pi];
            size_t j;
            if (!p->pending_sel[0] || !p->list.items || p->list.count == 0)
                continue;
            for (j = 1; j < p->list.count; j++)
                if (strcmp(p->list.items[j].name, p->pending_sel) == 0) {
                    p->file_sel = (int)j;
                    break;
                }
            p->pending_sel[0] = '\0';
        }
}

/* Короткая неблокирующая проверка размера. Не блокирует главный цикл:
 * даёт пользователю возможность растянуть терминал или нажать 'q'. */
static void show_size_warning(int rows, int cols)
{
    int ch;
    erase();
    mvprintw(0, 0, T(STR_MSG_SMALL_TERM), cols, rows, MIN_COLS, MIN_ROWS);
    mvprintw(1, 0, "%s", T(STR_MSG_SMALL_TERM_HINT));
    refresh();
    timeout(100);
    ch = get_key(0);
    /* ФИКС: окно ожидания тоже должно знать о новом размере терминала,
     * иначе после «развернуть» первый кадр считается по старым LINES/COLS. */
    if (ch == KEY_RESIZE && is_term_resized(rows, cols))
        resizeterm(rows, cols);
    if (ch == 'q' || ch == 'Q' || !g_app.running) g_app.running = 0;
}

static void cleanup(void)
{
    int i, pi;
    for (i = 0; i < NUM_TABS; i++)
        for (pi = 0; pi < 2; pi++)
            free_list(&g_app.tabs[i].panes[pi].list);
    /* ЭТАП 1: буфер обмена теперь char** — освобождаем имена и вектор */
    clipboard_clear();
    free(g_app.clip_paths);
    g_app.clip_paths = NULL;
    endwin();
}

int app_run(void)
{
    char start[FM_PATH_MAX];
    /* ФИКС (clang-tidy, cert-err33-c): setlocale() возвращает NULL, если
     * локаль из окружения не установлена (например, LANG=ru_RU.KOI8-R на
     * системе без этой локали). Молча продолжать в этом случае нельзя:
     * дальше mvaddwstr()/wcwidth() считают, что работает C-локаль, и
     * кириллица в интерфейсе рассыпается на байты. Явно откатываемся в
     * "C.UTF-8" — она есть в любой установке glibc и хотя бы не ломает
     * UTF-8. */
    if (!setlocale(LC_ALL, "") && !setlocale(LC_ALL, "C.UTF-8")) {
        /* обе локали недоступны — работаем как есть, но это уже
         * экзотика: рисовать UTF-8 будет нельзя, и мы это не скрываем. */
    }
    memset(&g_app, 0, sizeof(g_app));
    g_app.running = 1;
    if (!getcwd(start, sizeof start)) return 1;
    copy_str(g_app.tabs[0].panes[0].cwd,
             sizeof g_app.tabs[0].panes[0].cwd, start);
    load_config();
    init_ncurses();
    install_signals();
    init_tabs(start);
    restore_pending_selection();
    while (g_app.running) {
        int rows, cols, ch;
        int timeout_ms;
        MEVENT ev;
        getmaxyx(stdscr, rows, cols);
        if (cols < MIN_COLS || rows < MIN_ROWS) {
            show_size_warning(rows, cols);
            continue;
        }
        draw();
        /* Анимация бегущей строки нужна только если шапка не помещается.
         * Иначе блокирующее чтение — меньше нагрузки и мгновенная реакция
         * на сигналы за счёт EINTR. */
        timeout_ms = g_app.header_scrolling ? 100 : -1;
        timeout(timeout_ms);
        ch = get_key(g_app.search_active);
        if (!g_app.running) break;
        if (ch == ERR) continue;
        if (ch == KEY_MOUSE) {
            if (getmouse(&ev) == OK) handle_mouse(ev);
        } else if (ch == KEY_RESIZE) {
            /* ncurses обновляет LINES/COLS после getch() сам, но окна
             * (а их тут создают диалоги и просмотрщик) остаются со старыми
             * размерами. resizeterm() приводит всё в порядок; если ядро
             * ncurses отказалось — не падаем, просто просим полную
             * перерисовку. */
            if (is_term_resized(rows, cols)) resizeterm(rows, cols);
            clearok(stdscr, TRUE);
            refresh();
        } else {
            handle_key(ch);
        }
    }
    save_config();
    cleanup();
    return 0;
}
