#include "fm.h"

static const char *ACTION_NAMES[ACT_COUNT] = {
    "Открыть", "Посмотреть", "Открыть как", "Правка",
    "Копировать", "Вырезать", "Вставить", "Удалить",
    "Создать папку", "Создать файл", "Создать из шаблона",
    "Архивировать", "Разархивировать",
    "Переименовать", "Свойства", "В избранное", "Обновить", "Выход"
};

int run_submenu(const char *title, const char **options, int count)
{
    int max_len, menu_w, menu_h, rows, cols, i;
    if (!title || !options || count <= 0) return -1;
    max_len = utf8_strlen(title);
    int start_x, start_y, sel = 0, ch, res = -1, running = 1;
    WINDOW *win;
    for (i = 0; i < count; i++) {
        int len = utf8_strlen(options[i]);
        if (len > max_len) max_len = len;
    }
    menu_w = max_len + 6; menu_h = count + 2;
    getmaxyx(stdscr, rows, cols);
    start_x = (cols - menu_w) / 2; start_y = (rows - menu_h) / 2;
    apply_shadow(start_y, start_x, menu_h, menu_w);
    win = newwin(menu_h, menu_w, start_y, start_x);
    if (!win) return -1;
    wbkgd(win, COLOR_PAIR(PAIR_DIALOG));
    ui_prepare_win(win);
    box(win, 0, 0);
    while (running && g_app.running) {
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
        ch = get_key(0);
        if (ch == ERR) continue;
        if (ch == 27) running = 0;
        else if (ch == KEY_DOWN || ch == 'j') sel = (sel + 1) % count;
        else if (ch == KEY_UP || ch == 'k') sel = (sel - 1 + count) % count;
        else if (ch == '\n' || ch == KEY_ENTER) { res = sel; running = 0; }
    }
    delwin(win); touchwin(stdscr); refresh();
    return res;
}

static void draw_menu_strip(WINDOW *win, int menu_w, int scroll, int sel,
                            int vis, int needs_scroll, int content_w)
{
    int i;
    wattron(win, A_HEADER_GRAY);
    mvwhline(win, 0, 1, ' ', menu_w - 2);
    mvwprintw(win, 0, (menu_w - utf8_strlen("Действия")) / 2, "Действия");
    wattroff(win, A_HEADER_GRAY);
    for (i = 0; i < vis; i++) {
        int idx = scroll + i;
        int attr;
        if (idx >= ACT_COUNT) break;
        attr = (idx == sel) ? A_MENU_SEL : A_MENU_NORMAL;
        wattron(win, attr);
        mvwhline(win, 1 + i, 1, ' ', content_w);
        mvwprintw(win, 1 + i, 2, "%-*s", content_w - 2, ACTION_NAMES[idx]);
        wattroff(win, attr);
    }
    if (needs_scroll) {
        int bar_x = menu_w - 2, bar_h = vis, j;
        int thumb_h = (bar_h * vis) / ACT_COUNT;
        int max_scroll = ACT_COUNT - vis;
        int thumb_pos = max_scroll > 0 ? (bar_h - thumb_h) * scroll
                          / max_scroll : 0;
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
    WINDOW *win;
    for (i = 0; i < ACT_COUNT; i++) {
        int len = utf8_strlen(ACTION_NAMES[i]);
        if (len > max_len) max_len = len;
    }
    close_key = binding_key_by_name("menu");

    getmaxyx(stdscr, rows, cols);
    /* Меню занимает до 64% высоты экрана (но не больше числа пунктов). */
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
    apply_shadow(start_y, start_x, menu_h, menu_w);
    win = newwin(menu_h, menu_w, start_y, start_x);
    if (!win) return 1;
    wbkgd(win, COLOR_PAIR(PAIR_DIALOG));
    ui_prepare_win(win);
    box(win, 0, 0);
    content_w = menu_w - 2 - (needs_scroll ? 1 : 0);
    while (running && g_app.running) {
        werase(win);
        box(win, 0, 0);
        if (sel < scroll) scroll = sel;
        if (sel >= scroll + vis) scroll = sel - vis + 1;
        draw_menu_strip(win, menu_w, scroll, sel, vis, needs_scroll,
                        content_w);
        wrefresh(win);
        ch = get_key(0);
        if (ch == ERR) continue;
        if (ch == 27 || ch == 'q' || (close_key && ch == close_key))
            running = 0;
        else if (ch == KEY_DOWN || ch == 'j') sel = (sel + 1) % ACT_COUNT;
        else if (ch == KEY_UP || ch == 'k')
            sel = (sel - 1 + ACT_COUNT) % ACT_COUNT;
        else if (ch == '\n' || ch == KEY_ENTER) {
            res = execute_action(sel, p);
            running = 0;
        }
    }
    delwin(win); touchwin(stdscr); refresh();
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
        message_box("Ошибка", "Не удалось открыть закладку");
    }
}

static void bookmarks_draw(WINDOW *win, int menu_w, int scroll, int sel,
                           int vis)
{
    int i;
    wattron(win, A_HEADER_GRAY);
    mvwhline(win, 0, 1, ' ', menu_w - 2);
    mvwprintw(win, 0, (menu_w - 13) / 2, " Избранное ");
    wattroff(win, A_HEADER_GRAY);
    for (i = 0; i < vis; i++) {
        int idx = scroll + i;
        int attr;
        char disp[PATH_MAX + 2];
        if (idx >= g_app.bookmark_count) break;
        attr = (idx == sel) ? A_MENU_SEL : A_MENU_NORMAL;
        wattron(win, attr);
        mvwhline(win, 1 + i, 1, ' ', menu_w - 2);
        snprintf(disp, sizeof disp, " %s", g_app.bookmarks[idx]);
        mvwaddnstr(win, 1 + i, 1, disp,
                   (int)utf8_prefix_bytes(disp, menu_w - 2));
        wattroff(win, attr);
    }
}

static void bookmarks_delete(int sel)
{
    char msg[PATH_MAX + 64];
    int i;
    snprintf(msg, sizeof msg, "Удалить закладку %.100s?",
             g_app.bookmarks[sel]);
    if (!dialog_confirm("Удаление", msg)) return;
    for (i = sel; i < g_app.bookmark_count - 1; i++)
        copy_str(g_app.bookmarks[i], PATH_MAX, g_app.bookmarks[i + 1]);
    g_app.bookmark_count--;
    save_config();
}

void run_bookmarks_menu(void)
{
    int max_len = 10, i, menu_w, menu_h, rows, cols, start_x, start_y;
    int sel = 0, ch, running = 1, scroll = 0, vis;
    int close_key;
    WINDOW *win;
    if (g_app.bookmark_count == 0) {
        message_box("Избранное", "Список пуст");
        return;
    }
    close_key = binding_key_by_name("bookmarks");
    for (i = 0; i < g_app.bookmark_count; i++) {
        int len = utf8_strlen(g_app.bookmarks[i]);
        if (len > max_len) max_len = len;
    }
    menu_w = max_len + 6; if (menu_w > 60) menu_w = 60;
    menu_h = g_app.bookmark_count + 2; if (menu_h > 20) menu_h = 20;
    getmaxyx(stdscr, rows, cols);
    start_x = (cols - menu_w) / 2; start_y = (rows - menu_h) / 2;
    apply_shadow(start_y, start_x, menu_h, menu_w);
    win = newwin(menu_h, menu_w, start_y, start_x);
    if (!win) return;
    wbkgd(win, COLOR_PAIR(PAIR_DIALOG));
    ui_prepare_win(win);
    box(win, 0, 0);
    vis = menu_h - 2;
    while (running && g_app.running) {
        werase(win);
        box(win, 0, 0);
        if (sel < scroll) scroll = sel;
        if (sel >= scroll + vis) scroll = sel - vis + 1;
        bookmarks_draw(win, menu_w, scroll, sel, vis);
        wrefresh(win);
        ch = get_key(0);
        if (ch == ERR) continue;
        if (ch == 27 || ch == 'q' || (close_key && ch == close_key))
            running = 0;
        else if (ch == KEY_DOWN || ch == 'j')
            sel = (sel + 1) % g_app.bookmark_count;
        else if (ch == KEY_UP || ch == 'k')
            sel = (sel - 1 + g_app.bookmark_count) % g_app.bookmark_count;
        else if (ch == '\n' || ch == KEY_ENTER) {
            open_bookmark(g_app.bookmarks[sel]); running = 0;
        } else if (ch == 'd' || ch == 'D') {
            bookmarks_delete(sel);
            if (g_app.bookmark_count == 0) running = 0;
            else if (sel >= g_app.bookmark_count)
                sel = g_app.bookmark_count - 1;
            werase(stdscr); refresh();
        }
    }
    delwin(win); touchwin(stdscr); refresh();
}

/* ==== Settings ==== */

enum { SK_HEADER, SK_EMPTY, SK_RESET, SK_THEME, SK_BIND, SK_ASSOC, SK_ADD };

typedef struct { int kind; int id; char label[128]; } SettingItem;

static void binding_key_to_input(int k, char *out, size_t outsz)
{
    if (k == '\n')      snprintf(out, outsz, "\\n");
    else if (k == '\t') snprintf(out, outsz, "\\t");
    else if (k == ' ')  snprintf(out, outsz, "\\s");
    else if (k == 27)   snprintf(out, outsz, "\\e");
    else if (k >= 32 && k < 127) { out[0] = (char)k; out[1] = '\0'; }
    else                snprintf(out, outsz, "?");
}

static int binding_input_to_key(const char *s)
{
    if (s[0] == '\\') {
        if (s[1] == 'n') return '\n';
        if (s[1] == 't') return '\t';
        if (s[1] == 's') return ' ';
        if (s[1] == 'e') return 27;
        return 0;
    }
    {
        unsigned char c = (unsigned char)s[0];
        if (c >= 32 && c < 127) return c;
    }
    return 0;
}

static int find_binding_by_key(int key, int exclude_idx)
{
    int i;
    for (i = 0; i < g_app.binding_count; i++)
        if (i != exclude_idx && g_app.bindings[i].key == key)
            return i;
    return -1;
}

static void add_item(SettingItem *it, int *n, int kind, int id,
                     const char *label)
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
    add_item(items, &n, SK_HEADER, 0, "Сбросы");
    add_item(items, &n, SK_RESET, 0, "Сбросить настройки");
    add_item(items, &n, SK_RESET, 1, "Сбросить избранное");
    add_item(items, &n, SK_RESET, 2, "Сбросить цвета темы");
    add_item(items, &n, SK_RESET, 3, "Сбросить бинды");
    add_item(items, &n, SK_EMPTY, 0, NULL);
    add_item(items, &n, SK_HEADER, 0, "Тема");
    for (i = 0; i < THEME_FIELDS; i++)
        add_item(items, &n, SK_THEME, i, theme_name_by_idx(i));
    add_item(items, &n, SK_EMPTY, 0, NULL);
    add_item(items, &n, SK_HEADER, 0, "Бинды");
    for (i = 0; i < g_app.binding_count; i++)
        add_item(items, &n, SK_BIND, i, g_app.bindings[i].name);
    add_item(items, &n, SK_EMPTY, 0, NULL);
    add_item(items, &n, SK_HEADER, 0, "Ассоциации файлов");
    for (i = 0; i < g_app.assoc_count; i++) {
        char lab[64];
        snprintf(lab, sizeof lab, ".%s", g_app.associations[i].ext);
        add_item(items, &n, SK_ASSOC, i, lab);
    }
    add_item(items, &n, SK_ADD, 0, "Добавить ассоциацию");
    *count_out = n;
}

static void settings_draw_item(WINDOW *win, SettingItem *it, int row,
                               int w, int is_sel)
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
    } else if (it->kind == SK_BIND) {
        char kbuf[16];
        key_display_name(g_app.bindings[it->id].key, kbuf, sizeof kbuf);
        mvwprintw(win, row, 2, " %-20s [%s]", it->label, kbuf);
    } else if (it->kind == SK_ASSOC)
        mvwprintw(win, row, 2, " %-20s [%s]", it->label,
                  g_app.associations[it->id].cmd);
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
    mvwaddnstr(win, 0, 2, " Настройки ", w - 4);
    for (i = 0; i < vis; i++) {
        int idx = scroll + i;
        if (idx >= count) break;
        settings_draw_item(win, &items[idx], 1 + i, w, idx == sel);
    }
    if (count > vis) {
        int bar_x = w - 2, bar_h = h - 3, j;
        int thumb_h = (bar_h * vis) / count;
        int max_scroll = count - vis;
        int thumb_pos = max_scroll > 0 ? (bar_h - thumb_h) * scroll
                          / max_scroll : 0;
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
    mvwprintw(win, h - 2, 2,
              "j/k, Enter, d, Esc/O - выход");
    wrefresh(win);
}

static void perform_reset(int id)
{
    if (id == 0) {
        if (!dialog_confirm("Сброс", "Сбросить настройки?")) return;
        default_config(); default_bindings();
        g_app.assoc_count = 0;
        g_theme_raw[0] = '\0'; g_new_theme_seen = 0;
        init_attributes(); save_config();
        clearok(stdscr, TRUE);
        wbkgd(stdscr, COLOR_PAIR(PAIR_BG));
        refresh();
    } else if (id == 1) {
        if (!dialog_confirm("Сброс", "Очистить избранное?")) return;
        g_app.bookmark_count = 0;
        save_config();
    } else if (id == 2) {
        if (!dialog_confirm("Сброс", "Сбросить цвета темы?")) return;
        default_config();
        g_theme_raw[0] = '\0'; g_new_theme_seen = 0;
        init_attributes(); save_config();
        clearok(stdscr, TRUE);
        wbkgd(stdscr, COLOR_PAIR(PAIR_BG));
        refresh();
    } else if (id == 3) {
        if (!dialog_confirm("Сброс", "Сбросить бинды?")) return;
        default_bindings(); save_config();
    }
}

static void edit_theme(int idx)
{
    char *val = theme_field_by_idx(idx);
    char buf[16];
    if (!val) return;
    copy_str(buf, sizeof buf, val);
    if (!dialog_input("Цвет", "Значение (hex):", buf, sizeof buf)) return;
    if (!theme_color_ok(buf)) {
        message_box("Ошибка", "Нужен цвет в формате #RRGGBB");
        return;
    }
    copy_str(val, 8, buf);
    g_theme_raw[0] = '\0';
    init_attributes(); save_config();
    clearok(stdscr, TRUE);
    wbkgd(stdscr, COLOR_PAIR(PAIR_BG));
    refresh();
}

static void edit_binding(int idx)
{
    char buf[16];
    char msg[256];
    char kname[16];
    int nk, conflict;
    binding_key_to_input(g_app.bindings[idx].key, buf, sizeof buf);
    if (!dialog_input("Бинд", "\\n, \\t, \\s, \\e или символ:",
                      buf, sizeof buf)) return;
    nk = binding_input_to_key(buf);
    if (!nk) {
        message_box("Ошибка", "Некорректная клавиша");
        return;
    }
    if (nk == g_app.bindings[idx].key) return;
    conflict = find_binding_by_key(nk, idx);
    if (conflict >= 0) {
        key_display_name(nk, kname, sizeof kname);
        snprintf(msg, sizeof msg,
                 "Клавиша '%s' уже назначена на «%.80s»",
                 kname, g_app.bindings[conflict].name);
        message_box("Конфликт бинда", msg);
        return;
    }
    g_app.bindings[idx].key = nk;
    save_config();
}

static void edit_assoc(int idx)
{
    char buf[PATH_MAX];
    copy_str(buf, sizeof buf, g_app.associations[idx].cmd);
    if (!dialog_input("Команда", "Программа:", buf, sizeof buf)) return;
    if (!buf[0]) return;
    copy_str(g_app.associations[idx].cmd, PATH_MAX, buf);
    save_config();
}

static void add_assoc(void)
{
    char ext[64] = "", cmd[PATH_MAX] = "xdg-open";
    char *p;
    if (g_app.assoc_count >= MAX_ASSOC) return;
    if (!dialog_input("Новая ассоциация", "Расширение:", ext, sizeof ext))
        return;
    p = ext;
    while (*p == '.') p++;
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) return;
    if (!dialog_input("Новая ассоциация", "Программа:", cmd, sizeof cmd))
        return;
    if (!cmd[0] || !name_ok(p)) return;
    copy_str(g_app.associations[g_app.assoc_count].ext, 32, p);
    copy_str(g_app.associations[g_app.assoc_count].cmd, PATH_MAX, cmd);
    g_app.assoc_count++;
    save_config();
}

static void handle_setting_enter(SettingItem *it)
{
    if (it->kind == SK_RESET)       perform_reset(it->id);
    else if (it->kind == SK_THEME)  edit_theme(it->id);
    else if (it->kind == SK_BIND)   edit_binding(it->id);
    else if (it->kind == SK_ASSOC)  edit_assoc(it->id);
    else if (it->kind == SK_ADD)    add_assoc();
}

static void delete_assoc(SettingItem *it)
{
    char msg[160];
    int i;
    if (it->kind != SK_ASSOC) return;
    snprintf(msg, sizeof msg, "Удалить ассоциацию %.100s?", it->label);
    if (!dialog_confirm("Удаление", msg)) return;
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
    WINDOW *win;
    getmaxyx(stdscr, rows, cols);
    h = (rows * 64) / 100; if (h > rows - 4) h = rows - 4; if (h < 12) h = 12;
    w = 76; if (w > cols * 80 / 100) w = cols * 80 / 100;
    if (w > cols - 4) w = cols - 4;
    if (w < 50) w = 50;
    y = (rows - h) / 2; x = (cols - w) / 2;
    apply_shadow(y, x, h, w);
    win = newwin(h, w, y, x);
    if (!win) return;
    wbkgd(win, COLOR_PAIR(PAIR_DIALOG));
    ui_prepare_win(win);
    box(win, 0, 0);
    vis = h - 4 > 0 ? h - 4 : 1;
    close_key = binding_key_by_name("settings");
    while (running && g_app.running) {
        build_settings_items(items, &count);
        if (sel >= count) sel = count - 1;
        while (sel < count && !is_selectable(items[sel].kind)) sel++;
        if (sel >= count) for (i = count - 1; i >= 0; i--)
            if (is_selectable(items[i].kind)) { sel = i; break; }
        if (sel < scroll) scroll = sel;
        if (sel >= scroll + vis) scroll = sel - vis + 1;
        if (scroll < 0) scroll = 0;
        settings_draw(win, items, count, scroll, sel, vis, h, w);
        int ch = wgetch(win);
        if (ch == ERR) continue;
        if (ch == 27 || ch == 'q' || (close_key && ch == close_key))
            running = 0;
        else if (ch == KEY_DOWN || ch == 'j')
            sel = move_sel(sel, 1, items, count);
        else if (ch == KEY_UP || ch == 'k')
            sel = move_sel(sel, -1, items, count);
        else if (ch == '\n' || ch == KEY_ENTER)
            handle_setting_enter(&items[sel]);
        else if (ch == 'd' || ch == 'D') delete_assoc(&items[sel]);
    }
    delwin(win); touchwin(stdscr); refresh();
}
