#include "fm.h"

static const StrId ACTION_IDS[ACT_COUNT] = {
    STR_ACT_OPEN, STR_ACT_VIEW, STR_ACT_OPEN_AS, STR_ACT_EDIT,
    STR_ACT_COPY, STR_ACT_CUT, STR_ACT_PASTE, STR_ACT_DELETE,
    STR_ACT_MKDIR, STR_ACT_TOUCH, STR_ACT_TEMPLATE, STR_ACT_ARCHIVE,
    STR_ACT_UNARCHIVE, STR_ACT_RENAME, STR_ACT_INFO, STR_ACT_PIN,
    STR_ACT_REFRESH, STR_ACT_QUIT
};

int run_submenu(const char *title, const char **options, int count, int start_sel)
{
    int max_len, menu_w, menu_h, rows, cols, i;
    if (!title || !options || count <= 0) return -1;
    max_len = utf8_strlen(title);
    int start_x, start_y, sel = start_sel, ch, res = -1, running = 1;
    WINDOW *win = NULL;

    if (sel < 0 || sel >= count) sel = 0;

    for (i = 0; i < count; i++) {
        int len = utf8_strlen(options[i]);
        if (len > max_len) max_len = len;
    }

    getmaxyx(stdscr, rows, cols);
    menu_w = max_len + 6; menu_h = count + 2;
    start_x = (cols - menu_w) / 2; start_y = (rows - menu_h) / 2;

    while (running && g_app.running) {
        if (!win) {
            apply_shadow(start_y, start_x, menu_h, menu_w);
            win = newwin(menu_h, menu_w, start_y, start_x);
            if (!win) return -1;
            wbkgd(win, COLOR_PAIR(PAIR_DIALOG));
            ui_prepare_win(win);
        }
        werase(win);
        box(win, 0, 0);
        wattron(win, A_HEADER_GRAY);
        mvwhline(win, 0, 1, ' ', menu_w - 2);
        mvwprintw(win, 0, (menu_w - utf8_strlen(title)) / 2, "%s", title);
        wattroff(win, A_HEADER_GRAY);
        for (i = 0; i < count; i++) {
            int attr = (i == sel) ? A_MENU_SEL : A_MENU_NORMAL;
            wattron(win, attr);
            mvwhline(win, 1 + i, 1, ' ', menu_w - 2);
            mvwprintw(win, 1 + i, 2, "%-*s", menu_w - 4, options[i]);
            wattroff(win, attr);
        }
        wrefresh(win);
        ch = get_mapped_key(win);
        if (ch == KEY_RESIZE) {
            delwin(win); win = NULL;
            getmaxyx(stdscr, rows, cols);
            start_x = (cols - menu_w) / 2; start_y = (rows - menu_h) / 2;
            continue;
        }
        if (ch == KEY_MOUSE) {
            MEVENT ev;
            if (getmouse(&ev) == OK) {
                if (ev.bstate & BUTTON1_CLICKED) {
                    if (ev.x < start_x || ev.x >= start_x + menu_w || ev.y < start_y || ev.y >= start_y + menu_h) {
                        running = 0;
                    }
                } else if (ev.bstate & BUTTON4_PRESSED) {
                    sel = (sel - 1 + count) % count;
                } else if (ev.bstate & BUTTON5_PRESSED) {
                    sel = (sel + 1) % count;
                }
            }
            continue;
        }
        if (ch == ERR) continue;
        if (ch == 27) running = 0;
        else if (ch == KEY_DOWN || ch == 'j') sel = (sel + 1) % count;
        else if (ch == KEY_UP || ch == 'k') sel = (sel - 1 + count) % count;
        else if (ch == '\n' || ch == KEY_ENTER) { res = sel; running = 0; }
    }
    if (win) delwin(win);
    touchwin(stdscr); refresh();
    return res;
}

static void draw_menu_strip(WINDOW *win, int menu_w, int scroll, int sel,
                            int vis, int needs_scroll, int content_w)
{
    int i;
    wattron(win, A_HEADER_GRAY);
    mvwhline(win, 0, 1, ' ', menu_w - 2);
    mvwprintw(win, 0, (menu_w - utf8_strlen(T(STR_MENU_ACTIONS))) / 2, "%s", T(STR_MENU_ACTIONS));
    wattroff(win, A_HEADER_GRAY);
    for (i = 0; i < vis; i++) {
        int idx = scroll + i;
        int attr;
        if (idx >= ACT_COUNT) break;
        attr = (idx == sel) ? A_MENU_SEL : A_MENU_NORMAL;
        wattron(win, attr);
        mvwhline(win, 1 + i, 1, ' ', content_w);
        mvwprintw(win, 1 + i, 2, "%-*s", content_w - 2, T(ACTION_IDS[idx]));
        wattroff(win, attr);
    }
    if (needs_scroll) {
        int bar_x = menu_w - 2, bar_h = vis, j;
        int thumb_h = (bar_h * vis) / ACT_COUNT;
        int max_scroll = ACT_COUNT - vis;
        int thumb_pos = max_scroll > 0 ? (bar_h - thumb_h) * scroll / max_scroll : 0;
        if (thumb_h < 1) thumb_h = 1;
        if (thumb_pos + thumb_h > bar_h) thumb_pos = bar_h - thumb_h;
        for (j = 0; j < bar_h; j++) {
            if (j >= thumb_pos && j < thumb_pos + thumb_h) {
                wattron(win, A_SCROLL_THUMB);
                mvwaddch(win, 1 + j, bar_x, ' ');
                wattroff(win, A_SCROLL_THUMB);
            } else {
                wattron(win, A_MENU_NORMAL);
                mvwaddch(win, 1 + j, bar_x, ACS_VLINE);
                wattroff(win, A_MENU_NORMAL);
            }
        }
    }
}

int run_menu(FilePane *p)
{
    int max_len = 0, i;
    int vis, needs_scroll, menu_w, menu_h, rows, cols, start_x, start_y;
    int sel = 0, scroll = 0, running = 1, res = 1, content_w, ch;
    int close_key;
    WINDOW *win = NULL;

    for (i = 0; i < ACT_COUNT; i++) {
        int len = utf8_strlen(T(ACTION_IDS[i]));
        if (len > max_len) max_len = len;
    }
    close_key = binding_key_by_name("menu");

    getmaxyx(stdscr, rows, cols);
    {
        int max_h = (rows * 64) / 100;
        if (max_h < 6) max_h = 6;
        vis = max_h - 2;
    }
    if (vis > ACT_COUNT) vis = ACT_COUNT;
    if (vis < 3) vis = 3;
    needs_scroll = (ACT_COUNT > vis);

    menu_w = max_len + 6 + (needs_scroll ? 1 : 0);
    menu_h = vis + 2;
    start_x = (cols - menu_w) / 2; start_y = (rows - menu_h) / 2;
    content_w = menu_w - 2 - (needs_scroll ? 1 : 0);

    while (running && g_app.running) {
        if (!win) {
            apply_shadow(start_y, start_x, menu_h, menu_w);
            win = newwin(menu_h, menu_w, start_y, start_x);
            if (!win) return 1;
            wbkgd(win, COLOR_PAIR(PAIR_DIALOG));
            ui_prepare_win(win);
        }
        werase(win);
        box(win, 0, 0);
        if (sel < scroll) scroll = sel;
        if (sel >= scroll + vis) scroll = sel - vis + 1;
        draw_menu_strip(win, menu_w, scroll, sel, vis, needs_scroll, content_w);
        wrefresh(win);
        ch = get_mapped_key(win);
        if (ch == KEY_RESIZE) {
            delwin(win); win = NULL;
            getmaxyx(stdscr, rows, cols);
            {
                int max_h = (rows * 64) / 100;
                if (max_h < 6) max_h = 6;
                vis = max_h - 2;
            }
            if (vis > ACT_COUNT) vis = ACT_COUNT;
            if (vis < 3) vis = 3;
            needs_scroll = (ACT_COUNT > vis);
            menu_h = vis + 2;
            start_x = (cols - menu_w) / 2; start_y = (rows - menu_h) / 2;
            content_w = menu_w - 2 - (needs_scroll ? 1 : 0);
            continue;
        }
        if (ch == KEY_MOUSE) {
            MEVENT ev;
            if (getmouse(&ev) == OK) {
                if (ev.bstate & BUTTON1_CLICKED) {
                    if (ev.x < start_x || ev.x >= start_x + menu_w || ev.y < start_y || ev.y >= start_y + menu_h) {
                        running = 0;
                    }
                } else if (ev.bstate & BUTTON4_PRESSED) {
                    sel = (sel - 1 + ACT_COUNT) % ACT_COUNT;
                } else if (ev.bstate & BUTTON5_PRESSED) {
                    sel = (sel + 1) % ACT_COUNT;
                }
            }
            continue;
        }
        if (ch == ERR) continue;
        if (ch == 27 || ch == 'q' || (close_key && ch == close_key)) running = 0;
        else if (ch == KEY_DOWN || ch == 'j') sel = (sel + 1) % ACT_COUNT;
        else if (ch == KEY_UP || ch == 'k') sel = (sel - 1 + ACT_COUNT) % ACT_COUNT;
        else if (ch == '\n' || ch == KEY_ENTER) {
            res = execute_action(sel, p);
            running = 0;
        }
    }
    if (win) delwin(win);
    touchwin(stdscr); refresh();
    return res;
}

static void open_bookmark(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            enter_dir(path, &ACTIVE_PANE);
        } else {
            const char *base = strrchr(path, '/');
            base = base ? base + 1 : path;
            launch_path(base, path);
        }
    } else {
        message_box(T(STR_MSG_ERROR), T(STR_BOOKMARKS_OPEN_ERR));
    }
}

static void bookmark_label(char *out, size_t outsz, const char *path)
{
    char shown[FM_PATH_MAX];
    if (!out || outsz == 0) return;
    path_display(shown, sizeof shown, path);
    out[0] = ' ';
    if (outsz == 1) {
        out[0] = '\0';
        return;
    }
    copy_str(out + 1, outsz - 1, shown);
}

static void bookmarks_draw(WINDOW *win, int menu_w, int scroll, int sel, int vis)
{
    int i;
    wattron(win, A_HEADER_GRAY);
    mvwhline(win, 0, 1, ' ', menu_w - 2);
    mvwprintw(win, 0, (menu_w - utf8_strlen(T(STR_BOOKMARKS_TITLE))) / 2, "%s", T(STR_BOOKMARKS_TITLE));
    wattroff(win, A_HEADER_GRAY);
    for (i = 0; i < vis; i++) {
        int idx = scroll + i;
        int attr;
        char disp[FM_PATH_MAX + 2];
        if (idx >= g_app.bookmark_count) break;
        attr = (idx == sel) ? A_MENU_SEL : A_MENU_NORMAL;
        wattron(win, attr);
        mvwhline(win, 1 + i, 1, ' ', menu_w - 2);
        bookmark_label(disp, sizeof disp, g_app.bookmarks[idx]);
        mvwaddnstr(win, 1 + i, 1, disp, (int)utf8_prefix_bytes(disp, menu_w - 2));
        wattroff(win, attr);
    }
}

static int bookmarks_menu_width(void)
{
    int max_len = 10, i, menu_w;
    char shown[FM_PATH_MAX];
    for (i = 0; i < g_app.bookmark_count; i++) {
        int len;
        path_display(shown, sizeof shown, g_app.bookmarks[i]);
        len = utf8_strlen(shown);
        if (len > max_len) max_len = len;
    }
    menu_w = max_len + 6;
    if (menu_w > 60) menu_w = 60;
    return menu_w;
}

static void bookmarks_delete(int sel)
{
    char msg[FM_PATH_MAX + 64];
    char shown[FM_PATH_MAX];
    int i;
    path_display(shown, sizeof shown, g_app.bookmarks[sel]);
    snprintf(msg, sizeof msg, T(STR_BOOKMARKS_DELETE), shown);
    if (!dialog_confirm(T(STR_DELETE_TITLE), msg)) return;
    for (i = sel; i < g_app.bookmark_count - 1; i++)
        copy_str(g_app.bookmarks[i], sizeof g_app.bookmarks[i],
                 g_app.bookmarks[i + 1]);
    g_app.bookmark_count--;
    save_config();
}

void run_bookmarks_menu(void)
{
    int menu_w, menu_h, rows, cols, start_x, start_y;
    int sel = 0, ch, running = 1, scroll = 0, vis;
    int close_key;
    WINDOW *win = NULL;

    if (g_app.bookmark_count == 0) {
        message_box(T(STR_BOOKMARKS_TITLE), T(STR_BOOKMARKS_EMPTY));
        return;
    }
    close_key = binding_key_by_name("bookmarks");
    menu_w = bookmarks_menu_width();
    menu_h = g_app.bookmark_count + 2; if (menu_h > 20) menu_h = 20;

    getmaxyx(stdscr, rows, cols);
    start_x = (cols - menu_w) / 2; start_y = (rows - menu_h) / 2;
    vis = menu_h - 2;

    while (running && g_app.running) {
        if (!win) {
            apply_shadow(start_y, start_x, menu_h, menu_w);
            win = newwin(menu_h, menu_w, start_y, start_x);
            if (!win) return;
            wbkgd(win, COLOR_PAIR(PAIR_DIALOG));
            ui_prepare_win(win);
        }
        werase(win);
        box(win, 0, 0);
        if (sel < scroll) scroll = sel;
        if (sel >= scroll + vis) scroll = sel - vis + 1;
        bookmarks_draw(win, menu_w, scroll, sel, vis);
        wrefresh(win);
        ch = get_mapped_key(win);
        if (ch == KEY_RESIZE) {
            delwin(win); win = NULL;
            getmaxyx(stdscr, rows, cols);
            menu_h = g_app.bookmark_count + 2; if (menu_h > 20) menu_h = 20;
            start_x = (cols - menu_w) / 2; start_y = (rows - menu_h) / 2;
            vis = menu_h - 2;
            continue;
        }
        if (ch == KEY_MOUSE) {
            MEVENT ev;
            if (getmouse(&ev) == OK) {
                if (ev.bstate & BUTTON1_CLICKED) {
                    if (ev.x < start_x || ev.x >= start_x + menu_w || ev.y < start_y || ev.y >= start_y + menu_h) {
                        running = 0;
                    }
                } else if (ev.bstate & BUTTON4_PRESSED) {
                    sel = (sel - 1 + g_app.bookmark_count) % g_app.bookmark_count;
                } else if (ev.bstate & BUTTON5_PRESSED) {
                    sel = (sel + 1) % g_app.bookmark_count;
                }
            }
            continue;
        }
        if (ch == ERR) continue;
        if (ch == 27 || ch == 'q' || (close_key && ch == close_key)) running = 0;
        else if (ch == KEY_DOWN || ch == 'j') sel = (sel + 1) % g_app.bookmark_count;
        else if (ch == KEY_UP || ch == 'k') sel = (sel - 1 + g_app.bookmark_count) % g_app.bookmark_count;
        else if (ch == '\n' || ch == KEY_ENTER) {
            open_bookmark(g_app.bookmarks[sel]); running = 0;
        } else if (ch == 'd' || ch == 'D') {
            bookmarks_delete(sel);
            if (g_app.bookmark_count == 0) running = 0;
            else if (sel >= g_app.bookmark_count) sel = g_app.bookmark_count - 1;
            werase(stdscr); refresh();
        }
    }
    if (win) delwin(win);
    touchwin(stdscr); refresh();
}

/* ==== Settings ==== */

typedef enum { SK_HEADER, SK_EMPTY, SK_RESET, SK_THEME, SK_BIND, SK_ASSOC, SK_ADD, SK_LANG } SettingKind;

typedef struct { int kind; int id; char label[128]; } SettingItem;

static int find_binding_by_key(int key, int exclude_idx)
{
    int i;
    for (i = 0; i < g_app.binding_count; i++)
        if (i != exclude_idx && g_app.bindings[i].key == key)
            return i;
    return -1;
}

static void add_item(SettingItem *it, int *n, int kind, int id, const char *label)
{
    if (*n >= 256) return;
    it[*n].kind = kind;
    it[*n].id = id;
    if (label) snprintf(it[*n].label, sizeof it[*n].label, "%s", label);
    else       it[*n].label[0] = '\0';
    (*n)++;
}

static void build_settings_items(SettingItem *items, int *count_out)
{
    int n = 0, i;
    char lang_item_label[128];

    // Заголовок "Локализация" (или "Localization", если перевода нет)
    const char *loc_header = T(STR_SET_LOCALIZATION);
    if (!loc_header || !loc_header[0]) loc_header = "Localization";

    // Определяем родное название языка
    const char *native = g_app.lang;
    if (strcmp(g_app.lang, "ru") == 0) native = "Русский";
    else if (strcmp(g_app.lang, "en") == 0) native = "English";

    snprintf(lang_item_label, sizeof lang_item_label, "%s [%s]", native, g_app.lang);

    // Добавляем в меню
    add_item(items, &n, SK_HEADER, 0, loc_header);
    add_item(items, &n, SK_LANG, 0, lang_item_label);
    add_item(items, &n, SK_EMPTY, 0, NULL);

    add_item(items, &n, SK_HEADER, 0, T(STR_SET_RESETS));
    add_item(items, &n, SK_RESET, 0, T(STR_SET_RESET_ALL));
    add_item(items, &n, SK_RESET, 1, T(STR_SET_RESET_FAV));
    add_item(items, &n, SK_RESET, 2, T(STR_SET_RESET_THEME));
    add_item(items, &n, SK_RESET, 3, T(STR_SET_RESET_BINDS));
    add_item(items, &n, SK_EMPTY, 0, NULL);

    add_item(items, &n, SK_HEADER, 0, T(STR_SET_THEME));
    for (i = 0; i < THEME_FIELDS; i++)
        add_item(items, &n, SK_THEME, i, theme_name_by_idx(i));
    add_item(items, &n, SK_EMPTY, 0, NULL);

    add_item(items, &n, SK_HEADER, 0, T(STR_SET_BINDS));
    for (i = 0; i < g_app.binding_count; i++)
        add_item(items, &n, SK_BIND, i, g_app.bindings[i].name);
    add_item(items, &n, SK_EMPTY, 0, NULL);

    add_item(items, &n, SK_HEADER, 0, T(STR_SET_ASSOC));
    for (i = 0; i < g_app.assoc_count; i++) {
        char lab[64];
        snprintf(lab, sizeof lab, ".%s", g_app.associations[i].ext);
        add_item(items, &n, SK_ASSOC, i, lab);
    }
    add_item(items, &n, SK_ADD, 0, T(STR_SET_ADD_ASSOC));
    *count_out = n;
}

static void settings_draw_item(WINDOW *win, SettingItem *it, int row, int w, int is_sel)
{
    int attr = is_sel ? A_MENU_SEL : A_MENU_NORMAL;
    if (it->kind == SK_HEADER) {
        wattron(win, A_HEADER_GRAY);
        mvwhline(win, row, 1, ' ', w - 2);
        mvwprintw(win, row, 2, " %s ", it->label);
        wattroff(win, A_HEADER_GRAY);
        return;
    }
    if (it->kind == SK_EMPTY) return;
    wattron(win, attr);
    mvwhline(win, row, 1, ' ', w - 2);
    if (it->kind == SK_RESET)
        mvwprintw(win, row, 2, " %s ", it->label);
    else if (it->kind == SK_THEME) {
        char *val = theme_field_by_idx(it->id);
        mvwprintw(win, row, 2, " %-20s [%s]", it->label, val ? val : "");
    } else if (it->kind == SK_LANG) {
        mvwprintw(win, row, 2, " %-20s", it->label);
    } else if (it->kind == SK_BIND) {
        char kbuf[16];
        key_display_name(g_app.bindings[it->id].key, kbuf, sizeof kbuf);
        mvwprintw(win, row, 2, " %-20s [%s]", it->label, kbuf);
    } else if (it->kind == SK_ASSOC)
        mvwprintw(win, row, 2, " %-20s [%s]", it->label, g_app.associations[it->id].cmd);
    else if (it->kind == SK_ADD)
        mvwprintw(win, row, 2, " [ %s ]", it->label);
    wattroff(win, attr);
}

static void settings_draw(WINDOW *win, SettingItem *items, int count,
                          int scroll, int sel, int vis, int h, int w)
{
    int i;
    werase(win);
    box(win, 0, 0);
    mvwaddnstr(win, 0, 2, T(STR_SET_TITLE), w - 4);
    for (i = 0; i < vis; i++) {
        int idx = scroll + i;
        if (idx >= count) break;
        settings_draw_item(win, &items[idx], 1 + i, w, idx == sel);
    }
    if (count > vis) {
        int bar_x = w - 2, bar_h = h - 3, j;
        int thumb_h = (bar_h * vis) / count;
        int max_scroll = count - vis;
        int thumb_pos = max_scroll > 0 ? (bar_h - thumb_h) * scroll / max_scroll : 0;
        if (thumb_h < 1) thumb_h = 1;
        if (thumb_pos + thumb_h > bar_h) thumb_pos = bar_h - thumb_h;
        for (j = 0; j < bar_h; j++)
            if (j >= thumb_pos && j < thumb_pos + thumb_h) {
                wattron(win, A_SCROLL_THUMB);
                mvwaddch(win, 1 + j, bar_x, ' ');
                wattroff(win, A_SCROLL_THUMB);
            } else {
                wattron(win, A_MENU_NORMAL);
                mvwaddch(win, 1 + j, bar_x, ACS_VLINE);
                wattroff(win, A_MENU_NORMAL);
            }
    }
    mvwprintw(win, h - 2, 2, "%s", T(STR_SET_FOOTER));
    wrefresh(win);
}

static void perform_reset(int id)
{
    if (id == 0) {
        if (!dialog_confirm(T(STR_SET_RESET_ALL), T(STR_SET_RESET_ALL_CONF))) return;
        default_config(); default_bindings();
        g_app.assoc_count = 0;
        g_theme_raw[0] = '\0'; g_new_theme_seen = 0;
        init_attributes(); save_config();
        clearok(stdscr, TRUE);
        wbkgd(stdscr, COLOR_PAIR(PAIR_BG));
        refresh();
    } else if (id == 1) {
        if (!dialog_confirm(T(STR_SET_RESET_FAV), T(STR_SET_RESET_FAV_CONF))) return;
        g_app.bookmark_count = 0;
        save_config();
    } else if (id == 2) {
        if (!dialog_confirm(T(STR_SET_RESET_THEME), T(STR_SET_RESET_THEME_CONF))) return;
        default_config();
        g_theme_raw[0] = '\0'; g_new_theme_seen = 0;
        init_attributes(); save_config();
        clearok(stdscr, TRUE);
        wbkgd(stdscr, COLOR_PAIR(PAIR_BG));
        refresh();
    } else if (id == 3) {
        if (!dialog_confirm(T(STR_SET_RESET_BINDS), T(STR_SET_RESET_BINDS_CONF))) return;
        default_bindings(); save_config();
    }
}

static void edit_theme(int idx)
{
    char *val = theme_field_by_idx(idx);
    char buf[16];
    if (!val) return;
    copy_str(buf, sizeof buf, val);
    if (!dialog_input(T(STR_SET_COLOR_TITLE), T(STR_SET_COLOR_PROMPT), buf, sizeof buf)) return;
    if (!theme_color_ok(buf)) {
        message_box(T(STR_MSG_ERROR), T(STR_SET_COLOR_ERR));
        return;
    }
    copy_str(val, 8, buf);
    g_theme_raw[0] = '\0';
    init_attributes(); save_config();
    clearok(stdscr, TRUE);
    wbkgd(stdscr, COLOR_PAIR(PAIR_BG));
    refresh();
}

static int capture_binding_key(int *out)
{
    WINDOW *win;
    int rows, cols, w, h, y, x, ch;
    getmaxyx(stdscr, rows, cols);
    w = cols > 52 ? 46 : (cols > 8 ? cols - 4 : cols);
    h = 5;
    y = (rows - h) / 2;
    x = (cols - w) / 2;
    if (y < 0) y = 0;
    if (x < 0) x = 0;
    win = newwin(h, w, y, x);
    if (!win) return 0;
    wbkgd(win, COLOR_PAIR(PAIR_DIALOG));
    ui_prepare_win(win);
    box(win, 0, 0);
    mvwaddn_utf8(win, 1, 2, T(STR_SET_BIND_PROMPT), w - 4);
    mvwaddn_utf8(win, 2, 2, T(STR_SET_BIND_ESC), w - 4);
    wrefresh(win);
    ch = get_mapped_key(win);
    delwin(win);
    touchwin(stdscr);
    refresh();
    if (!g_app.running || ch == ERR || ch == 27) return 0;
    *out = ch;
    return 1;
}

static void edit_binding(int idx)
{
    char msg[256];
    char kname[16];
    int nk, conflict;
    if (!capture_binding_key(&nk)) return;
    if (nk == g_app.bindings[idx].key) return;
    conflict = find_binding_by_key(nk, idx);
    if (conflict >= 0) {
        key_display_name(nk, kname, sizeof kname);
        snprintf(msg, sizeof msg, T(STR_SET_BIND_CONFLICT_MSG), kname, g_app.bindings[conflict].name);
        message_box(T(STR_SET_BIND_CONFLICT), msg);
        return;
    }
    g_app.bindings[idx].key = nk;
    save_config();
}

static void edit_assoc(int idx)
{
    char buf[FM_PATH_MAX];
    copy_str(buf, sizeof buf, g_app.associations[idx].cmd);
    if (!dialog_input(T(STR_SET_ASSOC_CMD_TITLE), T(STR_SET_ASSOC_CMD_PROMPT), buf, sizeof buf)) return;
    if (!buf[0]) return;
    copy_str(g_app.associations[idx].cmd, sizeof g_app.associations[idx].cmd, buf);
    save_config();
}

static void add_assoc(void)
{
    char ext[64] = "", cmd[FM_PATH_MAX] = "xdg-open";
    char *p;
    if (g_app.assoc_count >= MAX_ASSOC) return;
    if (!dialog_input(T(STR_SET_ASSOC_ADD_TITLE), T(STR_SET_ASSOC_EXT_PROMPT), ext, sizeof ext)) return;
    p = ext;
    while (*p == '.') p++;
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) return;
    if (!dialog_input(T(STR_SET_ASSOC_ADD_TITLE), T(STR_SET_ASSOC_CMD_PROMPT), cmd, sizeof cmd)) return;
    if (!cmd[0] || !name_ok(p)) return;
    copy_str(g_app.associations[g_app.assoc_count].ext,
             sizeof g_app.associations[0].ext, p);
    copy_str(g_app.associations[g_app.assoc_count].cmd,
             sizeof g_app.associations[0].cmd, cmd);
    g_app.assoc_count++;
    save_config();
}

static void change_language(void)
{
    DIR *d = opendir(get_lang_dir());
    struct dirent *de;
    char options[16][64];
    const char *ptrs[16];
    int count = 0;
    int current_sel = 0;

    if (!d) return;

    while ((de = readdir(d)) && count < 16) {
        char *ext = strstr(de->d_name, ".txt");
        if (ext && strlen(ext) == 4 && ext != de->d_name) {
            size_t len = ext - de->d_name;
            if (len < 64) {
                memcpy(options[count], de->d_name, len);
                options[count][len] = '\0';
                ptrs[count] = options[count];
                if (strcmp(options[count], g_app.lang) == 0) {
                    current_sel = count;
                }
                count++;
            }
        }
    }
    closedir(d);

    if (count == 0) {
        message_box(T(STR_MSG_ERROR), T(STR_MSG_NO_LANG));
        return;
    }

    int sel = run_submenu(T(STR_SET_LANG_SELECT), ptrs, count, current_sel);
    if (sel >= 0) {
        copy_str(g_app.lang, sizeof g_app.lang, ptrs[sel]);
        load_locale_file(g_app.lang);
        save_config();

        clearok(stdscr, TRUE);
        wbkgd(stdscr, COLOR_PAIR(PAIR_BG));
        refresh();
    }
}

static void handle_setting_enter(SettingItem *it)
{
    if (it->kind == SK_RESET)       perform_reset(it->id);
    else if (it->kind == SK_THEME)  edit_theme(it->id);
    else if (it->kind == SK_BIND)   edit_binding(it->id);
    else if (it->kind == SK_ASSOC)  edit_assoc(it->id);
    else if (it->kind == SK_ADD)    add_assoc();
    else if (it->kind == SK_LANG)   change_language();
}

static void delete_assoc(SettingItem *it)
{
    char msg[160];
    int i;
    if (it->kind != SK_ASSOC) return;
    snprintf(msg, sizeof msg, T(STR_SET_ASSOC_DEL), it->label);
    if (!dialog_confirm(T(STR_DELETE_TITLE), msg)) return;
    for (i = it->id; i < g_app.assoc_count - 1; i++)
        g_app.associations[i] = g_app.associations[i + 1];
    g_app.assoc_count--;
    save_config();
}

static int is_selectable(int kind)
{
    return kind != SK_HEADER && kind != SK_EMPTY;
}

static int move_sel(int sel, int delta, SettingItem *items, int count)
{
    int next = sel + delta;
    while (next >= 0 && next < count && !is_selectable(items[next].kind))
        next += delta;
    if (next < 0 || next >= count) return sel;
    return next;
}

void run_settings_menu(void)
{
    int rows, cols, h, w, y, x, sel = 0, scroll = 0, running = 1, vis;
    int i, count;
    int close_key;
    SettingItem items[256];
    WINDOW *win = NULL;

    getmaxyx(stdscr, rows, cols);
    h = (rows * 64) / 100; if (h > rows - 4) h = rows - 4; if (h < 12) h = 12;
    w = 76; if (w > cols * 80 / 100) w = cols * 80 / 100;
    if (w > cols - 4) w = cols - 4;
    if (w < 50) w = 50;
    y = (rows - h) / 2; x = (cols - w) / 2;
    vis = h - 4 > 0 ? h - 4 : 1;
    close_key = binding_key_by_name("settings");

    while (running && g_app.running) {
        if (!win) {
            apply_shadow(y, x, h, w);
            win = newwin(h, w, y, x);
            if (!win) return;
            wbkgd(win, COLOR_PAIR(PAIR_DIALOG));
            ui_prepare_win(win);
        }
        build_settings_items(items, &count);
        if (sel >= count) sel = count - 1;
        while (sel < count && !is_selectable(items[sel].kind)) sel++;
        if (sel >= count) for (i = count - 1; i >= 0; i--)
            if (is_selectable(items[i].kind)) { sel = i; break; }
        if (sel < scroll) scroll = sel;
        if (sel >= scroll + vis) scroll = sel - vis + 1;
        if (scroll < 0) scroll = 0;
        settings_draw(win, items, count, scroll, sel, vis, h, w);
        int ch = get_mapped_key(win);
        if (ch == KEY_RESIZE) {
            delwin(win); win = NULL;
            getmaxyx(stdscr, rows, cols);
            h = (rows * 64) / 100; if (h > rows - 4) h = rows - 4; if (h < 12) h = 12;
            w = 76; if (w > cols * 80 / 100) w = cols * 80 / 100;
            if (w > cols - 4) w = cols - 4;
            if (w < 50) w = 50;
            y = (rows - h) / 2; x = (cols - w) / 2;
            vis = h - 4 > 0 ? h - 4 : 1;
            continue;
        }
        if (ch == KEY_MOUSE) {
            MEVENT ev;
            if (getmouse(&ev) == OK) {
                if (ev.bstate & BUTTON1_CLICKED) {
                    if (ev.x < x || ev.x >= x + w || ev.y < y || ev.y >= y + h) {
                        running = 0;
                    }
                } else if (ev.bstate & BUTTON4_PRESSED) {
                    sel = move_sel(sel, -1, items, count);
                } else if (ev.bstate & BUTTON5_PRESSED) {
                    sel = move_sel(sel, 1, items, count);
                }
            }
            continue;
        }
        if (ch == ERR) continue;
        if (ch == 27 || ch == 'q' || (close_key && ch == close_key)) running = 0;
        else if (ch == KEY_DOWN || ch == 'j') sel = move_sel(sel, 1, items, count);
        else if (ch == KEY_UP || ch == 'k') sel = move_sel(sel, -1, items, count);
        else if (ch == '\n' || ch == KEY_ENTER) handle_setting_enter(&items[sel]);
        else if (ch == 'd' || ch == 'D') delete_assoc(&items[sel]);
    }
    if (win) delwin(win);
    touchwin(stdscr); refresh();
}
