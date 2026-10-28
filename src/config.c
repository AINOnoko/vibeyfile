#include "fm.h"

void default_bindings(void)
{
    static const struct { const char *name; int key; } defs[] = {
        {"open", '\n'}, {"open_l", 'l'},
        {"menu", 'a'}, {"bookmarks", 'p'}, {"settings", 'o'},
        {"help", 'i'}, {"split", 'm'}, {"preview", 'v'},
        {"sort_next", 's'}, {"sort_reverse", 'S'},
        {"select", ' '}, {"search", '/'},
        {"refresh", 'r'}, {"refresh_full", 'R'},
        {"quit", 'q'}, {"up", 'k'}, {"down", 'j'},
        {"home", 'g'}, {"end", 'G'}, {"parent", 'h'},
    };
    size_t i;
    g_app.binding_count = 0;
    for (i = 0; i < sizeof(defs) / sizeof(defs[0]); i++) {
        copy_str(g_app.bindings[g_app.binding_count].name, 24,
                 defs[i].name);
        g_app.bindings[g_app.binding_count].key = defs[i].key;
        g_app.binding_count++;
    }
}

void add_bookmark(const char *path)
{
    int i;
    if (!path || !*path) return;
    for (i = 0; i < g_app.bookmark_count; i++)
        if (strcmp(g_app.bookmarks[i], path) == 0) return;
    if (g_app.bookmark_count < MAX_BOOKMARKS) {
        copy_str(g_app.bookmarks[g_app.bookmark_count], PATH_MAX, path);
        g_app.bookmark_count++;
    }
}

static char *trim_left(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static void trim_right(char *s)
{
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t')) *--e = '\0';
}

static void parse_theme_line(char *line)
{
    char *eq = strchr(line, '=');
    char *key, *val;
    if (!eq) return;
    *eq = '\0';
    key = trim_left(line); trim_right(key);
    val = trim_left(eq + 1); trim_right(val);
    if (!strcmp(key, "bg"))               snprintf(g_theme.bg, 8, "%s", val);
    else if (!strcmp(key, "fg"))          snprintf(g_theme.fg, 8, "%s", val);
    else if (!strcmp(key, "sel_bg"))      snprintf(g_theme.sel_bg, 8, "%s", val);
    else if (!strcmp(key, "sel_fg"))      snprintf(g_theme.sel_fg, 8, "%s", val);
    else if (!strcmp(key, "sel_inactive_bg")) {
        snprintf(g_theme.sel_inactive_bg, 8, "%s", val);
        g_new_theme_seen |= NEW_FIELD_SEL_INACTIVE_BG;
    } else if (!strcmp(key, "sel_inactive_fg")) {
        snprintf(g_theme.sel_inactive_fg, 8, "%s", val);
        g_new_theme_seen |= NEW_FIELD_SEL_INACTIVE_FG;
    } else if (!strcmp(key, "marked_bg")) {
        snprintf(g_theme.marked_bg, 8, "%s", val);
        g_new_theme_seen |= NEW_FIELD_MARKED_BG;
    } else if (!strcmp(key, "marked_fg")) {
        snprintf(g_theme.marked_fg, 8, "%s", val);
        g_new_theme_seen |= NEW_FIELD_MARKED_FG;
    } else if (!strcmp(key, "dir"))          snprintf(g_theme.dir, 8, "%s", val);
    else if (!strcmp(key, "exec"))           snprintf(g_theme.exec, 8, "%s", val);
    else if (!strcmp(key, "header_bg"))      snprintf(g_theme.header_bg, 8, "%s", val);
    else if (!strcmp(key, "header_fg"))      snprintf(g_theme.header_fg, 8, "%s", val);
    else if (!strcmp(key, "title_bg"))       snprintf(g_theme.title_bg, 8, "%s", val);
    else if (!strcmp(key, "title_fg"))       snprintf(g_theme.title_fg, 8, "%s", val);
    else if (!strcmp(key, "tab_active_bg"))  snprintf(g_theme.tab_active_bg, 8, "%s", val);
    else if (!strcmp(key, "tab_active_fg"))  snprintf(g_theme.tab_active_fg, 8, "%s", val);
    else if (!strcmp(key, "tab_inactive_bg"))snprintf(g_theme.tab_inactive_bg, 8, "%s", val);
    else if (!strcmp(key, "tab_inactive_fg"))snprintf(g_theme.tab_inactive_fg, 8, "%s", val);
    else if (!strcmp(key, "status_bg"))      snprintf(g_theme.status_bg, 8, "%s", val);
    else if (!strcmp(key, "status_fg"))      snprintf(g_theme.status_fg, 8, "%s", val);
    else if (!strcmp(key, "dialog_bg"))      snprintf(g_theme.dialog_bg, 8, "%s", val);
    else if (!strcmp(key, "dialog_fg"))      snprintf(g_theme.dialog_fg, 8, "%s", val);
}

static void parse_exec_line(char *line)
{
    char *eq = strchr(line, '=');
    char *key, *val, *k;
    if (!eq) return;
    *eq = '\0';
    key = trim_left(line); trim_right(key);
    val = trim_left(eq + 1); trim_right(val);
    k = key;
    while (*k == '.') k++;
    if (*k && g_app.assoc_count < MAX_ASSOC) {
        copy_str(g_app.associations[g_app.assoc_count].ext, 32, k);
        copy_str(g_app.associations[g_app.assoc_count].cmd, PATH_MAX, val);
        g_app.assoc_count++;
    }
}

static int binding_char_from_str(const char *val)
{
    if (val[0] == '\\' && val[1] == 'n') return '\n';
    if (val[0] == '\\' && val[1] == 't') return '\t';
    if (val[0] == '\\' && val[1] == 's') return ' ';
    if (val[0] == '\\' && val[1] == 'e') return 27;
    if ((unsigned char)val[0] >= 32 && (unsigned char)val[0] < 127)
        return (unsigned char)val[0];
    return 0;
}

static void parse_bind_line(char *line)
{
    char *eq = strchr(line, '=');
    char *key, *val;
    int nk, i;
    if (!eq) return;
    *eq = '\0';
    key = trim_left(line); trim_right(key);
    val = trim_left(eq + 1);
    nk = binding_char_from_str(val);
    if (!nk) return;
    for (i = 0; i < g_app.binding_count; i++)
        if (strcmp(g_app.bindings[i].name, key) == 0) {
            g_app.bindings[i].key = nk;
            break;
        }
}

static void parse_history_line(char *line)
{
    char *eq = strchr(line, '=');
    char *key, *val;
    int idx;
    char suffix[32] = {0};
    if (!eq) return;
    *eq = '\0';
    key = trim_left(line); trim_right(key);
    val = trim_left(eq + 1); trim_right(val);
    if (!strcmp(key, "active_tab")) {
        idx = atoi(val);
        if (idx >= 0 && idx < NUM_TABS) g_app.active_tab = idx;
        return;
    }
    if (sscanf(key, "tab%d_%31s", &idx, suffix) == 2) {
        if (idx < 0 || idx >= NUM_TABS) return;
        if (!strcmp(suffix, "split"))
            g_app.tabs[idx].split_mode = atoi(val) ? 1 : 0;
        else if (!strcmp(suffix, "preview"))
            g_app.tabs[idx].preview_mode = atoi(val) ? 1 : 0;
        else if (!strcmp(suffix, "pane"))
            g_app.tabs[idx].active_pane = (atoi(val) == 1) ? 1 : 0;
        else if (!strcmp(suffix, "sel"))
            copy_str(g_app.tabs[idx].panes[0].pending_sel, MAX_NAME, val);
        else if (!strcmp(suffix, "p0_sel"))
            copy_str(g_app.tabs[idx].panes[0].pending_sel, MAX_NAME, val);
        else if (!strcmp(suffix, "p1_sel"))
            copy_str(g_app.tabs[idx].panes[1].pending_sel, MAX_NAME, val);
        else if (!strcmp(suffix, "p0"))
            copy_str(g_app.tabs[idx].panes[0].cwd, PATH_MAX, val);
        else if (!strcmp(suffix, "p1"))
            copy_str(g_app.tabs[idx].panes[1].cwd, PATH_MAX, val);
    } else if (sscanf(key, "tab%d", &idx) == 1) {
        if (idx >= 0 && idx < NUM_TABS)
            copy_str(g_app.tabs[idx].panes[0].cwd, PATH_MAX, val);
    }
}

static void trim_line_end(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r'))
        s[--len] = '\0';
}

static void seed_defaults(const char *home)
{
    char cfg[PATH_MAX];
    if (g_app.bookmark_count == 0) {
        add_bookmark(home);
        add_bookmark("/etc");
        add_bookmark("/usr");
        snprintf(cfg, sizeof cfg, "%s/.config", home);
        add_bookmark(cfg);
    }
}

void load_config(void)
{
    char path[PATH_MAX], line[PATH_MAX + 64];
    const char *home = getenv("HOME");
    FILE *f;
    int in_theme = 0, in_fav = 0, in_hist = 0, in_exec = 0, in_binds = 0;
    int first_line = 1;

    default_config();
    default_bindings();
    g_theme_raw[0] = '\0';
    g_new_theme_seen = 0;
    if (!home) return;
    snprintf(path, sizeof path, "%s/.vibeyfilerc", home);
    f = fopen(path, "r");
    if (!f) {
        seed_defaults(home);
        return;
    }

    while (fgets(line, sizeof line, f)) {
        char *start = line;
        char *p;
        if (first_line) {
            first_line = 0;
            if ((unsigned char)start[0] == 0xEF
                && (unsigned char)start[1] == 0xBB
                && (unsigned char)start[2] == 0xBF)
                start += 3;
        }
        p = start;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '[') {
            in_theme = !strncmp(p, "[THEME]", 7);
            in_fav   = !strncmp(p, "[FAVORITE]", 10);
            in_hist  = !strncmp(p, "[HISTORY]", 9);
            in_exec  = !strncmp(p, "[EXECUTION]", 11);
            in_binds = !strncmp(p, "[KEYBINDS]", 10);
            if (in_theme) {
                size_t add = strlen(start);
                if (add > sizeof(g_theme_raw) - 1)
                    add = sizeof(g_theme_raw) - 1;
                memcpy(g_theme_raw, start, add);
                g_theme_raw[add] = '\0';
            }
            continue;
        }
        if (in_theme) {
            size_t cur = strlen(g_theme_raw);
            size_t add = strlen(start);
            if (cur + add + 1 < sizeof(g_theme_raw)) {
                memcpy(g_theme_raw + cur, start, add);
                g_theme_raw[cur + add] = '\0';
            }
        }
        trim_line_end(start);
        if (!*start) continue;
        if (in_theme)      parse_theme_line(start);
        else if (in_fav)   add_bookmark(start);
        else if (in_exec)  parse_exec_line(start);
        else if (in_binds) parse_bind_line(start);
        else if (in_hist)  parse_history_line(start);
    }
    fclose(f);
}

static void save_theme_section(FILE *f)
{
    if (g_theme_raw[0]) {
        size_t len = strlen(g_theme_raw);
        while (len > 0 && (g_theme_raw[len - 1] == '\n'
                           || g_theme_raw[len - 1] == '\r'))
            g_theme_raw[--len] = '\0';
        fputs(g_theme_raw, f);
        fputc('\n', f);
        if (!(g_new_theme_seen & NEW_FIELD_SEL_INACTIVE_BG))
            fprintf(f, "sel_inactive_bg = %s\n", g_theme.sel_inactive_bg);
        if (!(g_new_theme_seen & NEW_FIELD_SEL_INACTIVE_FG))
            fprintf(f, "sel_inactive_fg = %s\n", g_theme.sel_inactive_fg);
        if (!(g_new_theme_seen & NEW_FIELD_MARKED_BG))
            fprintf(f, "marked_bg = %s\n", g_theme.marked_bg);
        if (!(g_new_theme_seen & NEW_FIELD_MARKED_FG))
            fprintf(f, "marked_fg = %s\n", g_theme.marked_fg);
        return;
    }
    fprintf(f, "[THEME]\n");
    fprintf(f, "bg = %s\n", g_theme.bg);
    fprintf(f, "fg = %s\n", g_theme.fg);
    fprintf(f, "sel_bg = %s\n", g_theme.sel_bg);
    fprintf(f, "sel_fg = %s\n", g_theme.sel_fg);
    fprintf(f, "sel_inactive_bg = %s\n", g_theme.sel_inactive_bg);
    fprintf(f, "sel_inactive_fg = %s\n", g_theme.sel_inactive_fg);
    fprintf(f, "marked_bg = %s\n", g_theme.marked_bg);
    fprintf(f, "marked_fg = %s\n", g_theme.marked_fg);
    fprintf(f, "dir = %s\n", g_theme.dir);
    fprintf(f, "exec = %s\n", g_theme.exec);
    fprintf(f, "header_bg = %s\n", g_theme.header_bg);
    fprintf(f, "header_fg = %s\n", g_theme.header_fg);
    fprintf(f, "title_bg = %s\n", g_theme.title_bg);
    fprintf(f, "title_fg = %s\n", g_theme.title_fg);
    fprintf(f, "tab_active_bg = %s\n", g_theme.tab_active_bg);
    fprintf(f, "tab_active_fg = %s\n", g_theme.tab_active_fg);
    fprintf(f, "tab_inactive_bg = %s\n", g_theme.tab_inactive_bg);
    fprintf(f, "tab_inactive_fg = %s\n", g_theme.tab_inactive_fg);
    fprintf(f, "status_bg = %s\n", g_theme.status_bg);
    fprintf(f, "status_fg = %s\n", g_theme.status_fg);
    fprintf(f, "dialog_bg = %s\n", g_theme.dialog_bg);
    fprintf(f, "dialog_fg = %s\n", g_theme.dialog_fg);
}

static void save_fav_section(FILE *f)
{
    int i;
    fprintf(f, "\n[FAVORITE]\n");
    for (i = 0; i < g_app.bookmark_count; i++)
        fprintf(f, "%s\n", g_app.bookmarks[i]);
}

static void save_exec_section(FILE *f)
{
    int i;
    fprintf(f, "\n[EXECUTION]\n");
    for (i = 0; i < g_app.assoc_count; i++)
        fprintf(f, ".%s = %s\n",
                g_app.associations[i].ext,
                g_app.associations[i].cmd);
}

static void save_keybind_line(FILE *f, int k, const char *name)
{
    char kbuf[8];
    if (k == '\n')                    snprintf(kbuf, sizeof kbuf, "\\n");
    else if (k == '\t')               snprintf(kbuf, sizeof kbuf, "\\t");
    else if (k == ' ')                snprintf(kbuf, sizeof kbuf, "\\s");
    else if (k == 27)                 snprintf(kbuf, sizeof kbuf, "\\e");
    else if (k >= 32 && k < 127)    { kbuf[0] = (char)k; kbuf[1] = '\0'; }
    else                              snprintf(kbuf, sizeof kbuf, "?");
    fprintf(f, "%s = %s\n", name, kbuf);
}

static void save_binds_section(FILE *f)
{
    int i;
    fprintf(f, "\n[KEYBINDS]\n");
    for (i = 0; i < g_app.binding_count; i++)
        save_keybind_line(f, g_app.bindings[i].key,
                          g_app.bindings[i].name);
}

static void save_history_pane(FILE *f, int tab, int pi, const char *tag)
{
    FilePane *p = &g_app.tabs[tab].panes[pi];
    if (!p->cwd[0]) return;
    fprintf(f, "tab%d_%s = %s\n", tab, tag, p->cwd);
    if (p->list.count > 0 && p->file_sel > 0
        && p->file_sel < (int)p->list.count)
        fprintf(f, "tab%d_%s_sel = %s\n", tab, tag,
                p->list.items[p->file_sel].name);
}

static void save_history_section(FILE *f)
{
    int i;
    fprintf(f, "\n[HISTORY]\n");
    for (i = 0; i < NUM_TABS; i++) {
        save_history_pane(f, i, 0, "p0");
        save_history_pane(f, i, 1, "p1");
        if (g_app.tabs[i].split_mode)
            fprintf(f, "tab%d_split = 1\n", i);
        if (g_app.tabs[i].preview_mode)
            fprintf(f, "tab%d_preview = 1\n", i);
        if (g_app.tabs[i].active_pane)
            fprintf(f, "tab%d_pane = 1\n", i);
    }
    fprintf(f, "active_tab = %d\n", g_app.active_tab);
}

void save_config(void)
{
    char path[PATH_MAX], tmp[PATH_MAX];
    const char *home = getenv("HOME");
    FILE *f;
    if (!home) return;
    if (snprintf(path, sizeof path, "%s/.vibeyfilerc", home) >= (int)sizeof path)
        return;
    if (snprintf(tmp, sizeof tmp, "%s/.vibeyfilerc.tmp", home) >= (int)sizeof tmp)
        return;
    f = fopen(tmp, "w");
    if (!f) return;
    fchmod(fileno(f), 0600);
    save_theme_section(f);
    save_fav_section(f);
    save_exec_section(f);
    save_binds_section(f);
    save_history_section(f);
    if (fclose(f) != 0) { unlink(tmp); return; }
    if (rename(tmp, path) != 0) unlink(tmp);
}
