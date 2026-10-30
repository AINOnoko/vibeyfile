#include "fm.h"

static const struct { int ru1, ru2, en; } ru_map[] = {
    {0xD0, 0xB9, 'q'}, {0xD0, 0x99, 'Q'},
    {0xD1, 0x86, 'w'}, {0xD0, 0xA6, 'W'},
    {0xD1, 0x83, 'e'}, {0xD0, 0xA3, 'E'},
    {0xD0, 0xBA, 'r'}, {0xD0, 0x9A, 'R'},
    {0xD0, 0xB5, 't'}, {0xD0, 0x95, 'T'},
    {0xD0, 0xBD, 'y'}, {0xD0, 0x9D, 'Y'},
    {0xD0, 0xB3, 'u'}, {0xD0, 0x93, 'U'},
    {0xD1, 0x88, 'i'}, {0xD0, 0xA8, 'I'},
    {0xD1, 0x89, 'o'}, {0xD0, 0xA9, 'O'},
    {0xD0, 0xB7, 'p'}, {0xD0, 0x97, 'P'},
    {0xD1, 0x85, '['}, {0xD0, 0xA5, '{'},
    {0xD1, 0x8A, ']'}, {0xD0, 0xAA, '}'},
    {0xD1, 0x84, 'a'}, {0xD0, 0xA4, 'A'},
    {0xD1, 0x8B, 's'}, {0xD0, 0xAB, 'S'},
    {0xD0, 0xB2, 'd'}, {0xD0, 0x92, 'D'},
    {0xD0, 0xB0, 'f'}, {0xD0, 0x90, 'F'},
    {0xD0, 0xBF, 'g'}, {0xD0, 0x9F, 'G'},
    {0xD1, 0x80, 'h'}, {0xD0, 0xA0, 'H'},
    {0xD0, 0xBE, 'j'}, {0xD0, 0x9E, 'J'},
    {0xD0, 0xBB, 'k'}, {0xD0, 0x9B, 'K'},
    {0xD0, 0xB4, 'l'}, {0xD0, 0x94, 'L'},
    {0xD0, 0xB6, ';'}, {0xD0, 0x96, ':'},
    {0xD1, 0x8D, '\''}, {0xD0, 0xAD, '\"'},
    {0xD1, 0x8F, 'z'}, {0xD0, 0xAF, 'Z'},
    {0xD1, 0x87, 'x'}, {0xD0, 0xA7, 'X'},
    {0xD1, 0x81, 'c'}, {0xD0, 0xA1, 'C'},
    {0xD0, 0xBC, 'v'}, {0xD0, 0x9C, 'V'},
    {0xD0, 0xB8, 'b'}, {0xD0, 0x98, 'B'},
    {0xD1, 0x82, 'n'}, {0xD0, 0xA2, 'N'},
    {0xD1, 0x8C, 'm'}, {0xD0, 0xAC, 'M'},
    {0xD0, 0xB1, ','}, {0xD0, 0x91, '<'},
    {0xD1, 0x8E, '.'}, {0xD0, 0xAE, '>'},
    {0xD1, 0x91, '`'}, {0xD0, 0x81, '~'}
};

int get_mapped_key(WINDOW *win)
{
    int ch = win ? wgetch(win) : getch();
    int ch2;
    size_t i;

    app_poll_signals();
    if (ch == ERR) return ERR;

    if (ch == 0xD0 || ch == 0xD1) {
        timeout(-1); /* Ждем второй байт без таймаута */
        ch2 = win ? wgetch(win) : getch();
        timeout(100); /* Возвращаем таймаут обратно */

        if (ch2 == ERR) return ERR;
        for (i = 0; i < sizeof(ru_map) / sizeof(ru_map[0]); i++) {
            if (ru_map[i].ru1 == ch && ru_map[i].ru2 == ch2)
                return ru_map[i].en;
        }
        return 0;
    }
    return ch;
}

int get_key(int raw_mode)
{
    int ch = getch();
    int ch2;
    size_t i;

    app_poll_signals();
    if (ch == ERR) return ERR;

    if (ch == 0xD0 || ch == 0xD1) {
        ch2 = getch();
        if (ch2 == ERR) return ERR;

        if (raw_mode)
            return ((unsigned char)ch << 8) | (unsigned char)ch2;

        for (i = 0; i < sizeof(ru_map) / sizeof(ru_map[0]); i++) {
            if (ru_map[i].ru1 == ch && ru_map[i].ru2 == ch2)
                return ru_map[i].en;
        }
        return 0;
    }
    return ch;
}

static void normalize_view(void)
{
    if (CURRENT_TAB.preview_mode) {
        CURRENT_TAB.split_mode = 1;
        CURRENT_TAB.active_pane = 0;
    }
    if (CURRENT_TAB.active_pane != 0 && CURRENT_TAB.active_pane != 1)
        CURRENT_TAB.active_pane = 0;
}

static void switch_tab(int idx)
{
    if (idx < 0 || idx >= NUM_TABS || idx == g_app.active_tab) return;
    g_app.active_tab = idx;
    g_app.last_action[0] = '\0';
    normalize_view();
    if (ACTIVE_PANE.cwd[0] == '\0') {
        const char *home = getenv("HOME");
        copy_str(ACTIVE_PANE.cwd, sizeof ACTIVE_PANE.cwd,
                 home ? home : "/");
        if (load_dir(&ACTIVE_PANE) != 0) {
            copy_str(ACTIVE_PANE.cwd, sizeof ACTIVE_PANE.cwd, "/");
            load_dir(&ACTIVE_PANE);
        }
        if (ACTIVE_PANE.list.count > 1)
            ACTIVE_PANE.file_sel = 1;
    }
}

static void handle_mouse_header(MEVENT ev, int rows, int cols)
{
    (void)rows;
    if (ev.y != 0) return;

    /* Левая часть: кнопка закрытия */
    if (ev.x >= g_app.g_btn_x_quit && ev.x < g_app.g_btn_x_quit + g_app.g_btn_w_quit) {
        g_app.running = 0;
        return;
    }

    /* Правая часть */
    if (ev.x >= g_app.g_btn_x_menu && ev.x < g_app.g_btn_x_menu + g_app.g_btn_w_menu) {
        if (!run_menu(&ACTIVE_PANE)) g_app.running = 0;
    } else if (ev.x >= g_app.g_btn_x_help && ev.x < g_app.g_btn_x_help + g_app.g_btn_w_help) {
        show_help();
    } else if (ev.x >= g_app.g_btn_x_set && ev.x < g_app.g_btn_x_set + g_app.g_btn_w_set) {
        run_settings_menu();
    } else if (ev.x >= g_app.g_btn_x_book && ev.x < g_app.g_btn_x_book + g_app.g_btn_w_book) {
        run_bookmarks_menu();
    }
}

static void handle_mouse_header_row(MEVENT ev, int cols)
{
    int pane_idx, new_field = -1;
    FilePane *p;
    if (CURRENT_TAB.preview_mode && ev.x >= cols / 2) return;
    pane_idx = CURRENT_TAB.split_mode ?
               (ev.x >= cols / 2 ? 1 : 0) : CURRENT_TAB.active_pane;
    if (pane_idx != 0 && pane_idx != 1) return;
    p = &CURRENT_TAB.panes[pane_idx];
    if (g_app.g_x_size[pane_idx] != -1 && ev.x >= g_app.g_x_size[pane_idx])
        new_field = SORT_SIZE;
    else if (g_app.g_x_date[pane_idx] != -1
             && ev.x >= g_app.g_x_date[pane_idx])
        new_field = SORT_DATE;
    else if (g_app.g_x_type[pane_idx] != -1
             && ev.x >= g_app.g_x_type[pane_idx])
        new_field = SORT_TYPE;
    else if (g_app.g_x_name[pane_idx] != 0
             && ev.x >= g_app.g_x_name[pane_idx])
        new_field = SORT_NAME;
    if (new_field == -1) return;
    if (p->sort_field == (SortField)new_field) p->sort_desc = !p->sort_desc;
    else { p->sort_field = (SortField)new_field; p->sort_desc = 0; }
    do_sort(p);
}

static void handle_mouse_list(MEVENT ev, int rows, int cols)
{
    int pane_idx, idx;
    FilePane *p;
    (void)rows;
    pane_idx = CURRENT_TAB.split_mode ?
               (ev.x >= cols / 2 ? 1 : 0) : CURRENT_TAB.active_pane;
    if (CURRENT_TAB.preview_mode && pane_idx == 1) return;
    if (pane_idx != 0 && pane_idx != 1) return;
    p = &CURRENT_TAB.panes[pane_idx];
    idx = p->file_top + (ev.y - 2);
    if (idx < 0 || idx >= (int)p->list.count) return;
    CURRENT_TAB.active_pane = pane_idx;
    p->file_sel = idx;
    if (ev.bstate & BUTTON1_DOUBLE_CLICKED) action_open(p);
}

void handle_mouse(MEVENT ev)
{
    int rows, cols;
    normalize_view();
    getmaxyx(stdscr, rows, cols);
    if (ev.y == 0) { handle_mouse_header(ev, rows, cols); return; }
    if (ev.bstate & BUTTON3_CLICKED) {
        if (!run_menu(&ACTIVE_PANE)) g_app.running = 0;
        return;
    }
    if (!(ev.bstate & (BUTTON1_CLICKED | BUTTON1_PRESSED
                       | BUTTON1_DOUBLE_CLICKED))) goto scroll;
    if (ev.y == rows - 1) {
        if (!g_app.search_active) {
            int tab_w = cols / NUM_TABS;
            int tab_idx = tab_w > 0 ? ev.x / tab_w : 0;
            if (tab_idx >= 0 && tab_idx < NUM_TABS) switch_tab(tab_idx);
        }
    } else if (ev.y == 1) {
        handle_mouse_header_row(ev, cols);
    } else if (ev.y >= 2 && ev.y < rows - 2) {
        handle_mouse_list(ev, rows, cols);
    }
scroll:
    if (ev.bstate & BUTTON4_PRESSED) {
        if (ACTIVE_PANE.file_sel > 0) ACTIVE_PANE.file_sel--;
    }
    if (ev.bstate & BUTTON5_PRESSED) {
        if (ACTIVE_PANE.file_sel + 1 < (int)ACTIVE_PANE.list.count)
            ACTIVE_PANE.file_sel++;
    }
}

static void handle_search_key(int ch)
{
    int i;
    /* Toggle: та же клавиша, что включает поиск, его и выключает. */
    int search_key = binding_key_by_name("search");
    if (search_key && ch == search_key) {
        g_app.search_active = 0;
        return;
    }
    /* ФИКС (clang-tidy, bugprone-branch-clone): пять веток с одинаковым
     * телом `g_app.search_active = 0` и разным лишь побочным действием.
     * Сведено к «вычислить действие, затем применить один раз»: так видно,
     * что гашение поиска здесь общее, а не скопировано. */
    {
        int tab = -1;
        if (ch >= KEY_F(1) && ch <= KEY_F(4)) {
            tab = ch - KEY_F(1);
        } else if (ch == 27 || ch == CTRL('c')
                   || ch == '\n' || ch == KEY_ENTER) {
            tab = -2;                    /* только погасить поиск */
        }
        if (tab != -1) {
            g_app.search_active = 0;
            if (tab >= 0) switch_tab(tab);
            return;
        }
    }
    if (ch == 127 || ch == KEY_BACKSPACE) {
        if (g_app.search_len > 0) {
            /* ФИКС (clang-tidy, bugprone-inc-dec-in-conditions): декремент
             * спрятан в подвыражении индекса — читателю не видно, меняется
             * ли search_len вообще, а порядок вычисления тут критичен
             * (сначала декремент, потом чтение). Разнесено на два шага. */
            while (g_app.search_len > 0) {
                g_app.search_len--;
                if (((unsigned char)g_app.search_buf[g_app.search_len]
                     & 0xC0) != 0x80)
                    break;
            }
            g_app.search_buf[g_app.search_len] = '\0';
        }
    } else if (ch >= 32 && ch <= 255) {
        if (g_app.search_len < (int)sizeof g_app.search_buf - 1) {
            g_app.search_buf[g_app.search_len++] = (char)ch;
            g_app.search_buf[g_app.search_len] = '\0';
        }
    } else if ((ch >> 8) == 0xD0 || (ch >> 8) == 0xD1) {
        if (g_app.search_len < (int)sizeof g_app.search_buf - 2) {
            g_app.search_buf[g_app.search_len++] = (char)(ch >> 8);
            g_app.search_buf[g_app.search_len++] = (char)(ch & 0xFF);
            g_app.search_buf[g_app.search_len] = '\0';
        }
    }
    if (!g_app.search_buf[0] || !ACTIVE_PANE.list.items) return;
    for (i = 0; i < (int)ACTIVE_PANE.list.count; i++)
        if (!ACTIVE_PANE.list.items[i].is_parent
            && strcasestr(ACTIVE_PANE.list.items[i].name,
                          g_app.search_buf)) {
            ACTIVE_PANE.file_sel = i;
            break;
        }
}

static void handle_ui_bound(const char *bound)
{
    if (!strcmp(bound, "quit")) { g_app.running = 0; return; }
    if (!strcmp(bound, "menu")) {
        if (!run_menu(&ACTIVE_PANE)) g_app.running = 0;
        return;
    }
    if (!strcmp(bound, "bookmarks")) run_bookmarks_menu();
    else if (!strcmp(bound, "settings")) run_settings_menu();
    else if (!strcmp(bound, "help")) show_help();
    else if (!strcmp(bound, "search")) {
        g_app.search_active = 1;
        g_app.search_len = 0;
        g_app.search_buf[0] = '\0';
    } else if (!strcmp(bound, "refresh")) action_refresh(&ACTIVE_PANE);
    else if (!strcmp(bound, "refresh_full")) {
        action_refresh(&ACTIVE_PANE);
        clearok(stdscr, TRUE);
    }
}

static void handle_sort_bound(const char *bound)
{
    if (!strcmp(bound, "sort_next"))
        ACTIVE_PANE.sort_field = (ACTIVE_PANE.sort_field + 1) % SORT_COUNT;
    else if (!strcmp(bound, "sort_reverse"))
        ACTIVE_PANE.sort_desc = !ACTIVE_PANE.sort_desc;
    else return;
    do_sort(&ACTIVE_PANE);
}

static void handle_layout_bound(const char *bound)
{
    if (!strcmp(bound, "split")) {
        CURRENT_TAB.split_mode = !CURRENT_TAB.split_mode;
        if (!CURRENT_TAB.split_mode) CURRENT_TAB.preview_mode = 0;
        if (!CURRENT_TAB.split_mode || CURRENT_TAB.panes[1].cwd[0]) return;
        copy_str(CURRENT_TAB.panes[1].cwd, sizeof CURRENT_TAB.panes[1].cwd,
                 ACTIVE_PANE.cwd);
        load_dir(&CURRENT_TAB.panes[1]);
        return;
    }
    /* ФИКС (clang-tidy, bugprone-suspicious-string-compare): `if (strcmp(a,b))`
     * читается как «если строки равны», хотя в C истина — это «НЕ равны».
     * Смысл здесь верный, но запись неоднозначная; == 0 убирает вопрос. */
    if (strcmp(bound, "preview") != 0) return;
    CURRENT_TAB.preview_mode = !CURRENT_TAB.preview_mode;
    if (!CURRENT_TAB.preview_mode) return;
    CURRENT_TAB.split_mode = 1;
    CURRENT_TAB.active_pane = 0;
}

static void handle_nav_bound(const char *bound)
{
    if (!strcmp(bound, "up") && ACTIVE_PANE.file_sel > 0)
        ACTIVE_PANE.file_sel--;
    else if (!strcmp(bound, "down")
             && ACTIVE_PANE.file_sel + 1 < (int)ACTIVE_PANE.list.count)
        ACTIVE_PANE.file_sel++;
    else if (!strcmp(bound, "home")) {
        ACTIVE_PANE.file_sel = 0;
        ACTIVE_PANE.file_top = 0;
    } else if (!strcmp(bound, "end")) {
        ACTIVE_PANE.file_sel = ACTIVE_PANE.list.count ?
                               (int)ACTIVE_PANE.list.count - 1 : 0;
    } else if (!strcmp(bound, "parent")) action_parent(&ACTIVE_PANE);
    else if (!strcmp(bound, "open") || !strcmp(bound, "open_l"))
        action_open(&ACTIVE_PANE);
}

static void handle_select_bound(void)
{
    Entry *e;
    if (!ACTIVE_PANE.list.items || ACTIVE_PANE.file_sel <= 0) return;
    if (ACTIVE_PANE.file_sel >= (int)ACTIVE_PANE.list.count) return;
    e = &ACTIVE_PANE.list.items[ACTIVE_PANE.file_sel];
    e->selected ^= 1;
    if (ACTIVE_PANE.file_sel + 1 < (int)ACTIVE_PANE.list.count)
        ACTIVE_PANE.file_sel++;
}

static void handle_bound_action(const char *bound)
{
    if (!bound) return;
    if (!strcmp(bound, "select")) { handle_select_bound(); return; }
    if (!strncmp(bound, "sort_", 5)) { handle_sort_bound(bound); return; }
    if (!strcmp(bound, "split") || !strcmp(bound, "preview")) {
        handle_layout_bound(bound);
        return;
    }
    if (!strcmp(bound, "up") || !strcmp(bound, "down") || !strcmp(bound, "home")
        || !strcmp(bound, "end") || !strcmp(bound, "parent")
        || !strcmp(bound, "open") || !strcmp(bound, "open_l")) {
        handle_nav_bound(bound);
        return;
    }
    handle_ui_bound(bound);
}

static void handle_fallback_key(int ch, int vis)
{
    switch (ch) {
    case KEY_F(1): switch_tab(0); break;
    case KEY_F(2): switch_tab(1); break;
    case KEY_F(3): switch_tab(2); break;
    case KEY_F(4): switch_tab(3); break;
    case '\t':
        if (CURRENT_TAB.split_mode && !CURRENT_TAB.preview_mode)
            CURRENT_TAB.active_pane ^= 1;
        break;
    case KEY_LEFT:
    case 127:
    case KEY_BACKSPACE:
        action_parent(&ACTIVE_PANE);
        break;
    case KEY_RIGHT:
        action_open(&ACTIVE_PANE);
        break;
    case KEY_HOME:
        ACTIVE_PANE.file_sel = 0;
        ACTIVE_PANE.file_top = 0;
        break;
    case KEY_END:
        ACTIVE_PANE.file_sel = ACTIVE_PANE.list.count ?
                               (int)ACTIVE_PANE.list.count - 1 : 0;
        break;
    case KEY_DOWN:
        if (ACTIVE_PANE.file_sel + 1 < (int)ACTIVE_PANE.list.count)
            ACTIVE_PANE.file_sel++;
        break;
    case KEY_UP:
        if (ACTIVE_PANE.file_sel > 0) ACTIVE_PANE.file_sel--;
        break;
    case CTRL('d'): case KEY_NPAGE:
        ACTIVE_PANE.file_sel += vis / 2;
        if (ACTIVE_PANE.file_sel >= (int)ACTIVE_PANE.list.count)
            ACTIVE_PANE.file_sel = ACTIVE_PANE.list.count ?
                (int)ACTIVE_PANE.list.count - 1 : 0;
        break;
    case CTRL('u'): case KEY_PPAGE:
        ACTIVE_PANE.file_sel -= vis / 2;
        if (ACTIVE_PANE.file_sel < 0) ACTIVE_PANE.file_sel = 0;
        break;
    /* ФИКС (clang-tidy, bugprone-switch-missing-default-case): switch по
     * значению keypad, а не по enum — любая необработанная клавиша
     * молча падает в «нет ветки». Это правильно, но должно быть
     * записано явно: иначе невозможно отличить «забыли клавишу» от
     * «намеренно игнорируем». */
    default:
        return;
    }
}

void handle_key(int ch)
{
    int rows, cols, vis, i;
    const char *bound = NULL;
    normalize_view();
    getmaxyx(stdscr, rows, cols);
    (void)cols;
    vis = rows - 4;
    if (vis < 1) vis = 1;
    if (ch == ERR) return;
    if (g_app.search_active) { handle_search_key(ch); return; }
    g_app.last_action[0] = '\0';
    for (i = 0; i < g_app.binding_count; i++)
        if (g_app.bindings[i].key == ch) {
            bound = g_app.bindings[i].name;
            break;
        }
    if (bound) { handle_bound_action(bound); return; }
    handle_fallback_key(ch, vis);
}
