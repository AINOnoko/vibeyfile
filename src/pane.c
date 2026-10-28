#include "fm.h"

void compute_geometry(int x, int w, int show_type, int show_date,
                      int show_size, PaneGeom *g)
{
    int name_w = w - 6 - 1;
    if (show_size) name_w -= (SIZE_W + 1);
    if (show_date) name_w -= (DATE_W + 1);
    if (show_type) name_w -= (TYPE_W + 1);
    if (name_w < 4) name_w = 4;
    g->show_type = show_type;
    g->show_date = show_date;
    g->show_size = show_size;
    g->name_w = name_w;
    g->x_name = x + 6;
    g->x_type = show_type ? g->x_name + name_w + 1 : -1;
    g->x_date = show_date ?
        (show_type ? g->x_type + TYPE_W + 1 : g->x_name + name_w + 1) : -1;
    g->x_size = show_size ? x + w - SIZE_W : -1;
}

void draw_pane_header(FilePane *p, int pane_idx, int x, int w,
                      int show_type, int show_date, int show_size)
{
    PaneGeom g;
    char h_name[64], h_type[64], h_date[64], h_size[64];
    char name_padded[512], type_padded[64], date_padded[64], size_padded[64];
    const char *arrow = p->sort_desc ? "▼" : "▲";
    compute_geometry(x, w, show_type, show_date, show_size, &g);
    if (pane_idx >= 0 && pane_idx < 2) {
        g_app.g_x_name[pane_idx] = g.x_name;
        g_app.g_x_type[pane_idx] = g.x_type;
        g_app.g_x_date[pane_idx] = g.x_date;
        g_app.g_x_size[pane_idx] = g.x_size;
    }
    attron(A_HEADER_GRAY);
    mvhline(1, x, ' ', w);
    snprintf(h_name, sizeof h_name, "Имя %s",
             (p->sort_field == SORT_NAME) ? arrow : " ");
    snprintf(h_type, sizeof h_type, "Тип %s",
             (p->sort_field == SORT_TYPE) ? arrow : " ");
    snprintf(h_date, sizeof h_date, "Изменён %s",
             (p->sort_field == SORT_DATE) ? arrow : " ");
    snprintf(h_size, sizeof h_size, "Размер %s",
             (p->sort_field == SORT_SIZE) ? arrow : " ");
    build_padded(name_padded, sizeof name_padded, h_name, g.name_w, 0);
    build_padded(type_padded, sizeof type_padded, h_type, TYPE_W, 0);
    build_padded(date_padded, sizeof date_padded, h_date, DATE_W, 0);
    build_padded(size_padded, sizeof size_padded, h_size, SIZE_W, 0);
    mvaddstr(1, x + 2, "[X]");
    mvaddstr(1, g.x_name, name_padded);
    if (g.show_type && g.x_type >= 0 && g.x_type < x + w)
        mvaddstr(1, g.x_type, type_padded);
    if (g.show_date && g.x_date >= 0 && g.x_date < x + w)
        mvaddstr(1, g.x_date, date_padded);
    if (g.show_size && g.x_size >= 0 && g.x_size < x + w)
        mvaddstr(1, g.x_size, size_padded);
    attroff(A_HEADER_GRAY);
}

static int row_attr(FilePane *p, Entry *e, int idx, int is_active)
{
    if (idx == p->file_sel && is_active)   return A_SEL_ACTIVE;
    if (idx == p->file_sel && !is_active)  return A_SEL_INACTIVE;
    if (e->selected)                       return A_MARKED;
    if (e->is_dir || e->is_parent)         return A_DIR;
    if (e->mode & (S_IXUSR | S_IXGRP | S_IXOTH)) return A_EXEC;
    return A_PANEL;
}

static void entry_date_size(Entry *e, char *date_str, char *size_str,
                            size_t dsz, size_t ssz, const struct tm *now_tm)
{
    struct tm tmv;
    if (e->is_parent) {
        snprintf(size_str, ssz, "DIR");
        return;
    }
    if (localtime_r(&e->mtime, &tmv)) {
        if (tmv.tm_year == now_tm->tm_year)
            strftime(date_str, dsz, "%m-%d %H:%M", &tmv);
        else
            strftime(date_str, dsz, "%Y-%m-%d ", &tmv);
    }
    if (e->is_dir) snprintf(size_str, ssz, "DIR");
    else           format_size(size_str, ssz, e->size);
}

static void build_row(char *row, size_t rowsz, FilePane *p, Entry *e,
                      PaneGeom *g, int idx, const struct tm *now_tm)
{
    char date_str[16] = "-", size_str[16] = "-";
    char name_disp[MAX_NAME + 16];
    char name_padded[MAX_NAME * 4 + 32];
    char type_padded[64], date_padded[64], size_padded[64];
    char icon, mark_char;
    const char *tname;
    int pos;

    entry_date_size(e, date_str, size_str, sizeof date_str,
                    sizeof size_str, now_tm);

    if (e->is_parent)
        snprintf(name_disp, sizeof name_disp, "<--- %s", e->name);
    else
        snprintf(name_disp, sizeof name_disp, "%s", e->name);

    icon = e->icon;
    tname = e->type_name;

    build_padded(name_padded, sizeof name_padded, name_disp, g->name_w, 0);
    build_padded(type_padded, sizeof type_padded, tname, TYPE_W, 0);
    build_padded(date_padded, sizeof date_padded, date_str, DATE_W, 0);
    build_padded(size_padded, sizeof size_padded, size_str, SIZE_W, 1);
    mark_char = (idx == p->file_sel) ? '>' : (e->selected ? '*' : ' ');
    pos = snprintf(row, rowsz, "%c [%c] %s", mark_char, icon, name_padded);
    if (pos < 0 || (size_t)pos >= rowsz) return;
    if (g->show_type)
        pos += snprintf(row + pos, rowsz - (size_t)pos, " %s", type_padded);
    if ((size_t)pos >= rowsz) return;
    if (g->show_date)
        pos += snprintf(row + pos, rowsz - (size_t)pos, " %s", date_padded);
    if ((size_t)pos >= rowsz) return;
    if (g->show_size)
        snprintf(row + pos, rowsz - (size_t)pos, " %s", size_padded);
}

static void draw_pane_row(FilePane *p, int x, int y, int w, int idx,
                          int is_active, int show_type, int show_date,
                          int show_size, const struct tm *now_tm)
{
    Entry *e = &p->list.items[idx];
    PaneGeom g;
    char row[2048];
    int attr, safe_bytes;
    compute_geometry(x, w, show_type, show_date, show_size, &g);
    attr = row_attr(p, e, idx, is_active);
    build_row(row, sizeof row, p, e, &g, idx, now_tm);
    safe_bytes = (int)utf8_prefix_bytes(row, w);
    attron(attr);
    mvhline(y, x, ' ', w);
    mvaddnstr(y, x, row, safe_bytes);
    attroff(attr);
}

static void draw_scrollbar(FilePane *p, int x, int w, int items_y, int vis)
{
    int bar_x = x + w - 1;
    int total = (int)p->list.count;
    int thumb_h = (vis * vis) / total;
    int max_scroll = total - vis;
    int thumb_pos, i;
    if (thumb_h < 1) thumb_h = 1;
    if (thumb_h > vis) thumb_h = vis;
    thumb_pos = max_scroll > 0 ? (vis - thumb_h) * p->file_top / max_scroll : 0;
    if (thumb_pos + thumb_h > vis) thumb_pos = vis - thumb_h;
    if (thumb_pos < 0) thumb_pos = 0;
    for (i = 0; i < vis; i++) {
        if (i >= thumb_pos && i < thumb_pos + thumb_h) {
            attron(A_SCROLL_THUMB);
            mvaddch(items_y + i, bar_x, ' ');
            attroff(A_SCROLL_THUMB);
        } else {
            attron(A_SCROLL_TRACK);
            mvaddch(items_y + i, bar_x, ACS_VLINE);
            attroff(A_SCROLL_TRACK);
        }
    }
}

void draw_pane(FilePane *p, int pane_idx, int x, int w, int is_active,
               int show_type, int show_date, int show_size,
               const struct tm *now_tm)
{
    int rows, cols, items_y, vis, needs_scroll, content_w, i;
    getmaxyx(stdscr, rows, cols);
    (void)cols;
    items_y = 2;
    vis = rows - 4;
    if (vis < 1) vis = 1;
    needs_scroll = ((int)p->list.count > vis);
    content_w = needs_scroll ? w - 1 : w;
    if (content_w < 4) { content_w = w; needs_scroll = 0; }
    draw_pane_header(p, pane_idx, x, content_w, show_type, show_date,
                     show_size);
    attron(A_PANEL);
    for (i = 0; i < vis; i++) mvhline(items_y + i, x, ' ', content_w);
    attroff(A_PANEL);
    if (p->file_top < 0) p->file_top = 0;
    if (p->file_sel >= (int)p->list.count)
        p->file_sel = p->list.count ? (int)p->list.count - 1 : 0;
    if (p->file_sel < 0) p->file_sel = 0;
    if (p->file_sel < p->file_top) p->file_top = p->file_sel;
    if (p->file_sel >= p->file_top + vis)
        p->file_top = p->file_sel - vis + 1;
    for (i = 0; i < vis; i++) {
        int idx = p->file_top + i;
        if (idx >= (int)p->list.count) break;
        draw_pane_row(p, x, items_y + i, content_w, idx, is_active,
                      show_type, show_date, show_size, now_tm);
    }
    if (needs_scroll) draw_scrollbar(p, x, w, items_y, vis);
}
