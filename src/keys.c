#include "fm.h"

int get_key(int raw_mode)
{
    int ch = getch();
    int ch2;
    if (ch == ERR) return ERR;
    if (ch == 0xD0 || ch == 0xD1) {
        ch2 = getch();
        if (ch2 == ERR) return ERR;
        if (raw_mode)
            return ((unsigned char)ch << 8) | (unsigned char)ch2;
        if (ch == 0xD0) {
            switch (ch2) {
            case 0xB9: return 'q'; case 0xBE: return 'j';
            case 0xB4: return 'l'; case 0xBB: return 'k';
            case 0xBF: return 'g'; case 0xBA: return 'r';
            case 0xBC: return 'v'; case 0xBD: return 'y';
            case 0x99: return 'Q'; case 0x9E: return 'J';
            case 0x94: return 'L'; case 0x9B: return 'K';
            case 0x9F: return 'G'; case 0x9A: return 'R';
            case 0x9C: return 'V'; case 0x9D: return 'Y';
            case 0xA0: return 'H'; case 0xB7: return 'p';
            case 0x97: return 'P';
            }
        } else if (ch == 0xD1) {
            switch (ch2) {
            case 0x84: return 'a'; case 0x8B: return 's';
            case 0x8C: return 'm'; case 0x80: return 'h';
            case 0x88: return 'i'; case 0x82: return 'n';
            case 0xA4: return 'A'; case 0xAB: return 'S';
            case 0xAC: return 'M'; case 0xA8: return 'I';
            case 0xA2: return 'N'; case 0x9E: return 'O';
            }
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
    if (ev.x >= cols - 4 && ev.x <= cols - 2) g_app.running = 0;
    else if (ev.x >= cols - 8 && ev.x <= cols - 6) run_settings_menu();
    else if (ev.x >= cols - 12 && ev.x <= cols - 10) run_bookmarks_menu();
    else if (ev.x >= cols - 16 && ev.x <= cols - 14)
        if (!run_menu(&ACTIVE_PANE)) g_app.running = 0;
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
    if (ch == KEY_F(1)) { g_app.search_active = 0; switch_tab(0); }
    else if (ch == KEY_F(2)) { g_app.search_active = 0; switch_tab(1); }
    else if (ch == KEY_F(3)) { g_app.search_active = 0; switch_tab(2); }
    else if (ch == KEY_F(4)) { g_app.search_active = 0; switch_tab(3); }
    else if (ch == 27 || ch == CTRL('c')) g_app.search_active = 0;
    else if (ch == '\n' || ch == KEY_ENTER) g_app.search_active = 0;
    else if (ch == 127 || ch == KEY_BACKSPACE) {
        if (g_app.search_len > 0) {
            while (g_app.search_len > 0
                   && ((unsigned char)g_app.search_buf[--g_app.search_len]
                       & 0xC0) == 0x80)
                {}
            g_app.search_buf[g_app.search_len] = '\0';
        }
    } else if (ch >= 32 && ch <= 255) {
        if (g_app.search_len < (int)sizeof(g_app.search_buf) - 1) {
            g_app.search_buf[g_app.search_len++] = (char)ch;
            g_app.search_buf[g_app.search_len] = '\0';
        }
    } else if ((ch >> 8) == 0xD0 || (ch >> 8) == 0xD1) {
        if (g_app.search_len < (int)sizeof(g_app.search_buf) - 2) {
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

static int handle_bound_action(const char *bound)
{
    if (!strcmp(bound, "quit")) { g_app.running = 0; return 1; }
    if (!strcmp(bound, "menu")) {
        if (!run_menu(&ACTIVE_PANE)) g_app.running = 0;
    } else if (!strcmp(bound, "bookmarks")) run_bookmarks_menu();
    else if (!strcmp(bound, "settings"))   run_settings_menu();
    else if (!strcmp(bound, "help"))       show_help();
    else if (!strcmp(bound, "sort_next")) {
        ACTIVE_PANE.sort_field = (ACTIVE_PANE.sort_field + 1) % SORT_COUNT;
        do_sort(&ACTIVE_PANE);
    } else if (!strcmp(bound, "sort_reverse")) {
        ACTIVE_PANE.sort_desc = !ACTIVE_PANE.sort_desc;
        do_sort(&ACTIVE_PANE);
    } else if (!strcmp(bound, "split")) {
        CURRENT_TAB.split_mode = !CURRENT_TAB.split_mode;
        if (!CURRENT_TAB.split_mode) CURRENT_TAB.preview_mode = 0;
        if (CURRENT_TAB.split_mode && CURRENT_TAB.panes[1].cwd[0] == '\0') {
            copy_str(CURRENT_TAB.panes[1].cwd,
                     sizeof CURRENT_TAB.panes[1].cwd, ACTIVE_PANE.cwd);
            load_dir(&CURRENT_TAB.panes[1]);
        }
    } else if (!strcmp(bound, "preview")) {
        CURRENT_TAB.preview_mode = !CURRENT_TAB.preview_mode;
        if (CURRENT_TAB.preview_mode) {
            CURRENT_TAB.split_mode = 1;
            CURRENT_TAB.active_pane = 0;
        }
    } else if (!strcmp(bound, "select")) {
        if (ACTIVE_PANE.list.items && ACTIVE_PANE.file_sel > 0
            && ACTIVE_PANE.file_sel < (int)ACTIVE_PANE.list.count) {
            ACTIVE_PANE.list.items[ACTIVE_PANE.file_sel].selected ^= 1;
            if (ACTIVE_PANE.file_sel + 1 < (int)ACTIVE_PANE.list.count)
                ACTIVE_PANE.file_sel++;
        }
    } else if (!strcmp(bound, "search")) {
        g_app.search_active = 1;
        g_app.search_len = 0;
        g_app.search_buf[0] = '\0';
    } else if (!strcmp(bound, "refresh")) action_refresh(&ACTIVE_PANE);
    else if (!strcmp(bound, "refresh_full")) {
        action_refresh(&ACTIVE_PANE);
        clearok(stdscr, TRUE);
    } else if (!strcmp(bound, "up")) {
        if (ACTIVE_PANE.file_sel > 0) ACTIVE_PANE.file_sel--;
    } else if (!strcmp(bound, "down")) {
        if (ACTIVE_PANE.file_sel + 1 < (int)ACTIVE_PANE.list.count)
            ACTIVE_PANE.file_sel++;
    } else if (!strcmp(bound, "home")) {
        ACTIVE_PANE.file_sel = 0;
        ACTIVE_PANE.file_top = 0;
    } else if (!strcmp(bound, "end")) {
        ACTIVE_PANE.file_sel = ACTIVE_PANE.list.count ?
                               (int)ACTIVE_PANE.list.count - 1 : 0;
    } else if (!strcmp(bound, "parent")) {
        action_parent(&ACTIVE_PANE);
    } else if (!strcmp(bound, "open") || !strcmp(bound, "open_l")) {
        action_open(&ACTIVE_PANE);
    }
    return 0;
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
