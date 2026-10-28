#include "fm.h"

static unsigned g_theme_seen;
static int g_exec_trusted;

void default_bindings(void)
{
    static const struct { const char *name; int key; } defs[] = {
        {"open", '\n'}, {"open_l", 'l'},
        {"menu", 'u'},
        {"bookmarks", 'p'}, {"settings", 'o'},
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

static int config_text_ok(const char *s)
{
    const unsigned char *p;
    if (!s || !s[0]) return 0;
    for (p = (const unsigned char *)s; *p; p++)
        if (*p < 32 || *p == 127) return 0;
    return 1;
}

static int assoc_ext_ok(const char *ext)
{
    size_t n, i;
    if (!config_text_ok(ext)) return 0;
    n = strlen(ext);
    if (n >= 32) return 0;
    for (i = 0; i < n; i++)
        if (ext[i] == '/' || ext[i] == '\\' || ext[i] == '=' || ext[i] == ' ')
            return 0;
    return 1;
}

void add_bookmark(const char *path)
{
    int i;
    if (!config_text_ok(path) || path[0] != '/') return;
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

    int i;
    for (i = 0; i < THEME_FIELDS; i++) {
        if (strcmp(key, theme_name_by_idx(i)) == 0) {
            char *field = theme_field_by_idx(i);
            if (!field || !theme_color_ok(val)) return;
            snprintf(field, 8, "%s", val);
            if (i < 32) g_theme_seen |= 1u << i;
            /* Keep old flags for backwards compatibility */
            if (i == 4) g_new_theme_seen |= NEW_FIELD_SEL_INACTIVE_BG;
            else if (i == 5) g_new_theme_seen |= NEW_FIELD_SEL_INACTIVE_FG;
            else if (i == 6) g_new_theme_seen |= NEW_FIELD_MARKED_BG;
            else if (i == 7) g_new_theme_seen |= NEW_FIELD_MARKED_FG;
            return;
        }
    }
}

static void parse_exec_line(char *line)
{
    char *eq = strchr(line, '=');
    char *key, *val, *k;
    if (!eq) return;
    *eq = '\0';
    key = trim_left(line); trim_right(key);
    val = trim_left(eq + 1); trim_right(val);
    if (!g_exec_trusted) return;
    k = key;
    while (*k == '.') k++;
    if (!assoc_ext_ok(k) || !config_text_ok(val)) return;
    if (strlen(val) >= PATH_MAX) return;
    if (g_app.assoc_count < MAX_ASSOC) {
        copy_str(g_app.associations[g_app.assoc_count].ext, 32, k);
        copy_str(g_app.associations[g_app.assoc_count].cmd, PATH_MAX, val);
        g_app.assoc_count++;
    }
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
    nk = binding_str_to_key(val);
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
        else if (!strcmp(suffix, "sel") && name_ok(val))
            copy_str(g_app.tabs[idx].panes[0].pending_sel, MAX_NAME, val);
        else if (!strcmp(suffix, "p0_sel") && name_ok(val))
            copy_str(g_app.tabs[idx].panes[0].pending_sel, MAX_NAME, val);
        else if (!strcmp(suffix, "p1_sel") && name_ok(val))
            copy_str(g_app.tabs[idx].panes[1].pending_sel, MAX_NAME, val);
        else if (!strcmp(suffix, "p0") && config_text_ok(val) && val[0] == '/')
            copy_str(g_app.tabs[idx].panes[0].cwd, PATH_MAX, val);
        else if (!strcmp(suffix, "p1") && config_text_ok(val) && val[0] == '/')
            copy_str(g_app.tabs[idx].panes[1].cwd, PATH_MAX, val);
    } else if (sscanf(key, "tab%d", &idx) == 1) {
        if (idx >= 0 && idx < NUM_TABS
            && config_text_ok(val) && val[0] == '/')
            copy_str(g_app.tabs[idx].panes[0].cwd, PATH_MAX, val);
    }
}

static void trim_line_end(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r'))
        s[--len] = '\0';
}

static int path_is_dir(const char *path)
{
    struct stat st;
    if (!path || path[0] != '/') return 0;
    if (fstatat(AT_FDCWD, path, &st, 0) != 0) return 0;
    return S_ISDIR(st.st_mode);
}

static int path_has_dot_seg(const char *p)
{
    const char *s;
    if (!p) return 1;
    for (s = p; (s = strstr(s, "/.")) != NULL; s += 2) {
        if (s[2] == '\0' || s[2] == '/') return 1;
        if (s[2] == '.' && (s[3] == '\0' || s[3] == '/')) return 1;
    }
    return 0;
}

static void strip_slashes(char *s)
{
    size_t n;
    if (!s) return;
    n = strlen(s);
    while (n > 1 && s[n - 1] == '/') s[--n] = '\0';
}

static int paths_equal(const char *a, const char *b)
{
    size_t na, nb;
    if (!a || !b) return 0;
    na = strlen(a);
    nb = strlen(b);
    while (na > 1 && a[na - 1] == '/') na--;
    while (nb > 1 && b[nb - 1] == '/') nb--;
    return na == nb && strncmp(a, b, na) == 0;
}

static int bookmark_has(const char *path)
{
    int i;
    for (i = 0; i < g_app.bookmark_count; i++)
        if (paths_equal(g_app.bookmarks[i], path)) return 1;
    return 0;
}

static void bookmark_if_dir(const char *path)
{
    char buf[PATH_MAX];
    if (!path || path[0] != '/') return;
    copy_str(buf, sizeof buf, path);
    strip_slashes(buf);
    if (!buf[1] || path_has_dot_seg(buf) || !path_is_dir(buf)) return;
    if (bookmark_has(buf)) return;
    add_bookmark(buf);
}

static void bookmark_home_child(const char *home, const char *name)
{
    char path[PATH_MAX];
    if (!name || !name[0] || strchr(name, '/')) return;
    if (make_path(path, sizeof path, home, name) != 0) return;
    bookmark_if_dir(path);
}

static void add_known_home_dirs(const char *home)
{
    static const char *const names[] = {
        "Рабочий стол", "Desktop",
        "Документы", "Documents",
        "Загрузки", "Downloads",
        "Музыка", "Music",
        "Изображения", "Pictures",
        "Видео", "Videos", "Movies",
        "Шаблоны", "Templates",
        "Общедоступные", "Общие", "Public",
    };
    size_t i;
    for (i = 0; i < sizeof names / sizeof names[0]; i++)
        bookmark_home_child(home, names[i]);
}

static int is_xdg_dir_key(const char *key)
{
    static const char *const keys[] = {
        "XDG_DESKTOP_DIR", "XDG_DOWNLOAD_DIR", "XDG_TEMPLATES_DIR",
        "XDG_PUBLICSHARE_DIR", "XDG_DOCUMENTS_DIR", "XDG_MUSIC_DIR",
        "XDG_PICTURES_DIR", "XDG_VIDEOS_DIR",
    };
    size_t i;
    if (!key) return 0;
    for (i = 0; i < sizeof keys / sizeof keys[0]; i++)
        if (strcmp(key, keys[i]) == 0) return 1;
    return 0;
}

static int is_xdg_default_key(const char *key)
{
    static const char *const keys[] = {
        "DESKTOP", "DOWNLOAD", "TEMPLATES", "PUBLICSHARE",
        "DOCUMENTS", "MUSIC", "PICTURES", "VIDEOS",
    };
    size_t i;
    if (!key) return 0;
    for (i = 0; i < sizeof keys / sizeof keys[0]; i++)
        if (strcmp(key, keys[i]) == 0) return 1;
    return 0;
}

static void strip_quotes(char *s)
{
    size_t n;
    char q;
    if (!s || !s[0]) return;
    q = s[0];
    if (q != '"' && q != '\'') return;
    n = strlen(s);
    if (n < 2 || s[n - 1] != q) return;
    memmove(s, s + 1, n - 2);
    s[n - 2] = '\0';
}

static void unescape_inplace(char *s)
{
    char *r, *w;
    if (!s) return;
    r = w = s;
    while (*r) {
        if (*r == '\\' && r[1]) r++;
        *w++ = *r++;
    }
    *w = '\0';
}

static int copy_abs(char *out, size_t outsz, const char *val)
{
    if (!val || val[0] != '/') return -1;
    copy_str(out, outsz, val);
    strip_slashes(out);
    return out[0] ? 0 : -1;
}

static const char *home_rest(const char *val)
{
    if (strncmp(val, "$HOME", 5) == 0 && (val[5] == '/' || val[5] == '\0'))
        return val + 5;
    if (strncmp(val, "${HOME}", 7) == 0 && (val[7] == '/' || val[7] == '\0'))
        return val + 7;
    return NULL;
}

static int expand_home_token(char *out, size_t outsz,
                             const char *home, const char *val)
{
    const char *rest;
    if (!out || outsz == 0 || !home || !val || !val[0]) return -1;
    rest = home_rest(val);
    if (!rest) return copy_abs(out, outsz, val);
    if (*rest == '/') rest++;
    if (!*rest) {
        copy_str(out, outsz, home);
        strip_slashes(out);
        return 0;
    }
    if (make_path(out, outsz, home, rest) != 0) return -1;
    strip_slashes(out);
    return 0;
}

static void bookmark_user_dir_line(const char *home, char *line)
{
    char *eq, *key, *val, path[PATH_MAX];
    eq = strchr(line, '=');
    if (!eq) return;
    *eq = '\0';
    key = trim_left(line);
    trim_right(key);
    val = trim_left(eq + 1);
    trim_right(val);
    if (!is_xdg_dir_key(key)) return;
    strip_quotes(val);
    unescape_inplace(val);
    if (expand_home_token(path, sizeof path, home, val) != 0) return;
    if (paths_equal(path, home)) return;
    bookmark_if_dir(path);
}

static void bookmark_default_line(const char *home, char *line)
{
    char *eq, *key, *val;
    eq = strchr(line, '=');
    if (!eq) return;
    *eq = '\0';
    key = trim_left(line);
    trim_right(key);
    val = trim_left(eq + 1);
    trim_right(val);
    strip_quotes(val);
    if (!is_xdg_default_key(key) || !val[0] || strchr(val, '/')) return;
    bookmark_home_child(home, val);
}

static void add_user_dirs_file(const char *home, const char *path)
{
    char line[PATH_MAX + 64];
    FILE *f;
    struct stat st;
    f = fopen(path, "r");
    if (!f) return;
    if (fstat(fileno(f), &st) != 0 || !S_ISREG(st.st_mode)) {
        fclose(f);
        return;
    }
    while (fgets(line, sizeof line, f)) {
        char *s = trim_left(line);
        trim_line_end(s);
        if (*s == '#' || !*s) continue;
        bookmark_user_dir_line(home, s);
    }
    fclose(f);
}

static void add_xdg_defaults(const char *home)
{
    char line[256];
    FILE *f;
    struct stat st;
    f = fopen("/etc/xdg/user-dirs.defaults", "r");
    if (!f) return;
    if (fstat(fileno(f), &st) != 0 || !S_ISREG(st.st_mode)) {
        fclose(f);
        return;
    }
    while (fgets(line, sizeof line, f)) {
        char *s = trim_left(line);
        trim_line_end(s);
        if (*s == '#' || !*s) continue;
        bookmark_default_line(home, s);
    }
    fclose(f);
}

static int join_user_dirs(char *out, size_t outsz, const char *dir)
{
    int n;
    if (!dir || dir[0] != '/') return -1;
    n = snprintf(out, outsz, "%s/user-dirs.dirs", dir);
    return (n > 0 && (size_t)n < outsz) ? 0 : -1;
}

static void add_xdg_user_dirs(const char *home)
{
    char path[PATH_MAX];
    const char *cfg;
    int n = snprintf(path, sizeof path, "%s/.config/user-dirs.dirs", home);
    if (n > 0 && (size_t)n < sizeof path)
        add_user_dirs_file(home, path);
    cfg = getenv("XDG_CONFIG_HOME");
    if (join_user_dirs(path, sizeof path, cfg) == 0)
        add_user_dirs_file(home, path);
}

void add_home_places(const char *home)
{
    if (!home || home[0] != '/') return;
    add_known_home_dirs(home);
    add_xdg_defaults(home);
    add_xdg_user_dirs(home);
}

static void seed_system_dirs(const char *home)
{
    char cfg[PATH_MAX];
    int n;
    add_bookmark("/etc");
    add_bookmark("/usr");
    n = snprintf(cfg, sizeof cfg, "%s/.config", home);
    if (n > 0 && (size_t)n < sizeof cfg && path_is_dir(cfg))
        add_bookmark(cfg);
}

static void seed_defaults(const char *home)
{
    if (g_app.bookmark_count != 0) return;
    add_bookmark(home);
    add_home_places(home);
    seed_system_dirs(home);
}

void load_config(void)
{
    char path[PATH_MAX], line[PATH_MAX + 64];
    const char *home = getenv("HOME");
    FILE *f;
    int in_theme = 0, in_fav = 0, in_hist = 0, in_exec = 0, in_binds = 0;
    int first_line = 1;

    struct stat st;
    default_config();
    default_bindings();
    g_theme_raw[0] = '\0';
    g_new_theme_seen = 0;
    g_theme_seen = 0;
    g_exec_trusted = 0;
    if (!home) return;
    snprintf(path, sizeof path, "%s/.vibeyfilerc", home);
    f = fopen(path, "r");
    if (!f) {
        seed_defaults(home);
        return;
    }
    if (fstat(fileno(f), &st) != 0 || !S_ISREG(st.st_mode)) {
        fclose(f);
        if (g_app.bookmark_count == 0)
            add_home_places(home);
        return;
    }
    g_exec_trusted = st.st_uid == getuid() && (st.st_mode & 022) == 0;
    if (!g_exec_trusted)
        fprintf(stderr,
                "VibeyFile: %s не принадлежит только вам или доступен на запись, "
                "секция EXECUTION пропущена\n", path);

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
    if (g_app.bookmark_count == 0)
        add_home_places(home);
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

        {
            int i;
            for (i = 22; i < THEME_FIELDS && i < 32; i++) {
                char *val;
                if (g_theme_seen & (1u << i)) continue;
                val = theme_field_by_idx(i);
                if (!val) continue;
                fprintf(f, "%s = %s\n", theme_name_by_idx(i), val);
            }
        }

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

    fprintf(f, "btn_quit_bg = %s\n", g_theme.btn_quit_bg);
    fprintf(f, "btn_quit_fg = %s\n", g_theme.btn_quit_fg);
    fprintf(f, "btn_menu_bg = %s\n", g_theme.btn_menu_bg);
    fprintf(f, "btn_menu_fg = %s\n", g_theme.btn_menu_fg);
    fprintf(f, "btn_help_bg = %s\n", g_theme.btn_help_bg);
    fprintf(f, "btn_help_fg = %s\n", g_theme.btn_help_fg);
    fprintf(f, "btn_set_bg = %s\n", g_theme.btn_set_bg);
    fprintf(f, "btn_set_fg = %s\n", g_theme.btn_set_fg);
    fprintf(f, "btn_book_bg = %s\n", g_theme.btn_book_bg);
    fprintf(f, "btn_book_fg = %s\n", g_theme.btn_book_fg);
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
    for (i = 0; i < g_app.assoc_count; i++) {
        if (!assoc_ext_ok(g_app.associations[i].ext)) continue;
        if (!config_text_ok(g_app.associations[i].cmd)) continue;
        fprintf(f, ".%s = %s\n",
                g_app.associations[i].ext,
                g_app.associations[i].cmd);
    }
}

static void save_keybind_line(FILE *f, int k, const char *name)
{
    char kbuf[8];
    binding_key_to_str(k, kbuf, sizeof kbuf);
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
    if (!config_text_ok(p->cwd) || p->cwd[0] != '/') return;
    fprintf(f, "tab%d_%s = %s\n", tab, tag, p->cwd);
    if (p->list.count > 0 && p->file_sel > 0
        && p->file_sel < (int)p->list.count
        && name_ok(p->list.items[p->file_sel].name))
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
