#include "fm.h"

static void draw_preview_main(int x, int w, int rows);

static void normalize_view(void)
{
    if (CURRENT_TAB.preview_mode) {
        CURRENT_TAB.split_mode = 1;
        CURRENT_TAB.active_pane = 0;
    }
    if (CURRENT_TAB.active_pane != 1)
        CURRENT_TAB.active_pane = 0;
}

static void draw_tabs(int rows, int cols)
{
    int tab_w = cols / NUM_TABS, i;
    if (tab_w < 1) tab_w = 1;
    for (i = 0; i < NUM_TABS; i++) {
        int x = i * tab_w;
        int w = (i == NUM_TABS - 1) ? (cols - x) : tab_w;
        const char *p = g_app.tabs[i].panes[g_app.tabs[i].active_pane].cwd;
        char base[64], line[128];
        int attr;
        size_t bytes;
        if (w < 1) continue;
        if (!p[0] || strcmp(p, "/") == 0)
            snprintf(base, sizeof base, "/");
        else {
            const char *slash = strrchr(p, '/');
            copy_str(base, sizeof base, slash ? slash + 1 : p);
        }
        snprintf(line, sizeof line, " %d:%s ", i + 1, base);
        attr = (i == g_app.active_tab) ? A_TAB_ACTIVE : A_TAB_INACTIVE;
        attron(attr);
        mvhline(rows - 1, x, ' ', w);
        bytes = utf8_prefix_bytes(line, w);
        mvaddnstr(rows - 1, x, line, (int)bytes);
        attroff(attr);
    }
}

static void build_file_info(FilePane *p, char *out, size_t outsz)
{
    Entry *e;
    char size_str[32], date_str[32], perm[16];
    struct tm tmv, now_tm;
    time_t now;
    if (outsz == 0) return;
    out[0] = '\0';
    if (!p || !p->list.items || p->list.count == 0 || p->file_sel < 0
        || p->file_sel >= (int)p->list.count) return;
    e = &p->list.items[p->file_sel];
    if (e->is_parent) {
        snprintf(out, outsz, " ..  (родительский каталог)");
        return;
    }
    if (e->is_dir) snprintf(size_str, sizeof size_str, "DIR");
    else           format_size(size_str, sizeof size_str, e->size);
    now = time(NULL);
    localtime_r(&now, &now_tm);
    strcpy(date_str, "?");
    if (localtime_r(&e->mtime, &tmv)) {
        if (tmv.tm_year == now_tm.tm_year)
            strftime(date_str, sizeof date_str, "%m-%d %H:%M", &tmv);
        else
            strftime(date_str, sizeof date_str, "%Y-%m-%d ", &tmv);
    }
    format_mode(perm, sizeof perm, e->mode);
    snprintf(out, outsz, " %s  |  %s  |  %s  |  %s",
             e->name, size_str, date_str, perm);
}

static void draw_header(int cols)
{
    char left[PATH_MAX + 32];
    int avail;
    size_t bytes;
    attron(A_TITLE);
    mvhline(0, 0, ' ', cols);
    snprintf(left, sizeof left, " VibeyFile: %s", ACTIVE_PANE.cwd);
    avail = cols - 18;
    if (avail < 1) avail = 1;
    bytes = utf8_prefix_bytes(left, avail);
    mvaddnstr(0, 0, left, (int)bytes);
    if (cols >= 16)
        mvaddstr(0, cols - 16, "[A] [=] [O] [X]");
    attroff(A_TITLE);
}

static void draw_split(int cols, int rows)
{
    int half = cols / 2;
    int pw0 = half, pw1 = cols - half - 1;
    int y;
    if (pw1 < 1) pw1 = 1;
    draw_pane(&CURRENT_TAB.panes[0], 0, 0, pw0, CURRENT_TAB.active_pane == 0,
              pw0 >= COL_TYPE_MIN, pw0 >= COL_DATE_MIN, pw0 >= COL_SIZE_MIN);
    if (CURRENT_TAB.preview_mode)
        draw_preview_main(half + 1, pw1, rows);
    else
        draw_pane(&CURRENT_TAB.panes[1], 1, half + 1, pw1,
                  CURRENT_TAB.active_pane == 1,
                  pw1 >= COL_TYPE_MIN, pw1 >= COL_DATE_MIN,
                  pw1 >= COL_SIZE_MIN);
    attron(A_TITLE);
    for (y = 1; y < rows - 2; y++) mvaddch(y, half, ACS_VLINE);
    attroff(A_TITLE);
}

static void draw_status_line(int rows, int cols)
{
    char file_info[1024];
    const char *left_text;
    int avail;
    size_t bytes;
    attron(COLOR_PAIR(PAIR_STATUS));
    mvhline(rows - 2, 0, ' ', cols);
    if (g_app.search_active) {
        char line[320];
        int cx;
        snprintf(line, sizeof line, "Поиск: %s", g_app.search_buf);
        bytes = utf8_prefix_bytes(line, cols > 2 ? cols - 2 : 1);
        mvaddnstr(rows - 2, 1, line, (int)bytes);
        curs_set(1);
        cx = 1 + 7 + utf8_strlen(g_app.search_buf);
        if (cx >= cols) cx = cols - 1;
        if (cx < 1) cx = 1;
        move(rows - 2, cx);
    } else {
        char khelp[32];
        char hint[64];
        int hint_len, hint_x, show_hint;
        key_display_name(binding_key_by_name("help"), khelp, sizeof khelp);
        snprintf(hint, sizeof hint, "%s: справка", khelp);
        hint_len = utf8_strlen(hint);
        hint_x = cols - hint_len - 1;
        show_hint = (hint_x >= 10);
        if (show_hint) {
            avail = hint_x - 2;
        } else {
            avail = cols > 2 ? cols - 2 : 1;
        }
        if (avail < 8) avail = cols > 2 ? cols - 2 : 1;

        if (g_app.last_action[0]) left_text = g_app.last_action;
        else {
            build_file_info(&ACTIVE_PANE, file_info, sizeof file_info);
            left_text = file_info;
        }
        bytes = utf8_prefix_bytes(left_text, avail);
        mvaddnstr(rows - 2, 1, left_text, (int)bytes);
        if (show_hint)
            mvaddstr(rows - 2, hint_x, hint);
        curs_set(0);
    }
    attroff(COLOR_PAIR(PAIR_STATUS));
}

void draw(void)
{
    int rows, cols;
    normalize_view();
    getmaxyx(stdscr, rows, cols);
    erase();
    draw_header(cols);
    if (CURRENT_TAB.split_mode)
        draw_split(cols, rows);
    else
        draw_pane(&ACTIVE_PANE, CURRENT_TAB.active_pane, 0, cols, 1,
                  cols >= COL_TYPE_MIN, cols >= COL_DATE_MIN,
                  cols >= COL_SIZE_MIN);
    draw_status_line(rows, cols);
    draw_tabs(rows, cols);
    refresh();
}

static void preview_msg(int x, const char *text)
{
    mvaddstr(2, x + 2, text);
}

static void draw_preview_main(int x, int w, int rows)
{
    FilePane *src = &CURRENT_TAB.panes[0];
    int idx, vis, y, max_lines;
    Entry *e;
    char full[PATH_MAX];
    struct stat st;
    FILE *f;
    char line_buf[1024];
    unsigned char sniff[512];
    size_t nread, i;
    if (w < 4) return;
    attron(A_HEADER_GRAY);
    mvhline(1, x, ' ', w);
    mvaddstr(1, x + 2, "Предпросмотр");
    attroff(A_HEADER_GRAY);
    vis = rows - 4;
    if (vis < 1) vis = 1;
    attron(A_PANEL);
    for (y = 0; y < vis; y++) mvhline(2 + y, x, ' ', w);
    attroff(A_PANEL);
    if (!src->list.items) return;
    idx = src->file_sel;
    if (idx < 0 || idx >= (int)src->list.count) return;
    e = &src->list.items[idx];
    if (e->is_parent) { preview_msg(x, "<--- .."); return; }
    if (make_path(full, sizeof full, src->cwd, e->name) != 0) return;
    if (lstat(full, &st) == 0 && S_ISLNK(st.st_mode) && stat(full, &st) != 0) {
        preview_msg(x, "[Битая ссылка]");
        return;
    }
    if (stat(full, &st) != 0) { preview_msg(x, "[Нет доступа]"); return; }
    if (S_ISDIR(st.st_mode)) { preview_msg(x, "[Папка]"); return; }
    if (!S_ISREG(st.st_mode)) { preview_msg(x, "[Спецфайл]"); return; }
    if (st.st_size > 1024 * 1024) {
        preview_msg(x, "[Слишком большой для предпросмотра]");
        return;
    }
    f = fopen(full, "rb");
    if (!f) { preview_msg(x, "[Нет доступа]"); return; }
    nread = fread(sniff, 1, sizeof sniff, f);
    for (i = 0; i < nread; i++) {
        if (sniff[i] == 0) {
            preview_msg(x, "[Двоичный файл]");
            fclose(f);
            return;
        }
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        preview_msg(x, "[Нет доступа]");
        return;
    }
    y = 2;
    max_lines = rows - 4;
    if (max_lines < 1) max_lines = 1;
    while (y < 2 + max_lines && fgets(line_buf, sizeof line_buf, f)) {
        size_t len = strlen(line_buf);
        size_t b;
        while (len > 0 && (line_buf[len - 1] == '\n'
                           || line_buf[len - 1] == '\r'))
            line_buf[--len] = '\0';
        b = utf8_prefix_bytes(line_buf, w - 2);
        mvaddnstr(y, x + 2, line_buf, (int)b);
        y++;
    }
    fclose(f);
}
