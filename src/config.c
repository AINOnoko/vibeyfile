#include "fm.h"

static unsigned g_theme_seen;
static int g_exec_trusted;

void default_bindings(void) { /* без изменений */
    static const struct { const char *name; int key; } defs[] = {
        {"open", '\n'}, {"open_l", 'l'}, {"menu", 'u'},
        {"bookmarks", 'p'}, {"settings", 'o'}, {"help", 'i'},
        {"split", 'm'}, {"preview", 'v'}, {"sort_next", 's'},
        {"sort_reverse", 'S'}, {"select", ' '}, {"search", '/'},
        {"refresh", 'r'}, {"refresh_full", 'R'}, {"quit", 'q'},
        {"up", 'k'}, {"down", 'j'}, {"home", 'g'},
        {"end", 'G'}, {"parent", 'h'},
    };
    size_t i;
    g_app.binding_count = 0;
    for (i = 0; i < sizeof(defs) / sizeof(defs[0]); i++) {
        copy_str(g_app.bindings[g_app.binding_count].name, 24, defs[i].name);
        g_app.bindings[g_app.binding_count].key = defs[i].key;
        g_app.binding_count++;
    }
}

static int config_text_ok(const char *s) { /* без изменений */
    const unsigned char *p;
    if (!s || !s[0]) return 0;
    for (p = (const unsigned char *)s; *p; p++)
        if (*p < 32 || *p == 127) return 0;
    return 1;
}

static int assoc_ext_ok(const char *ext) { /* без изменений */
    size_t n, i;
    if (!config_text_ok(ext)) return 0;
    n = strlen(ext);
    if (n >= 32) return 0;
    for (i = 0; i < n; i++)
        if (ext[i] == '/' || ext[i] == '\\' || ext[i] == '=' || ext[i] == ' ')
            return 0;
    return 1;
}

void add_bookmark(const char *path) { /* без изменений */
    int i;
    if (!config_text_ok(path) || path[0] != '/') return;
    for (i = 0; i < g_app.bookmark_count; i++)
        if (strcmp(g_app.bookmarks[i], path) == 0) return;
    if (g_app.bookmark_count < MAX_BOOKMARKS
        && strlen(path) < FM_PATH_MAX) {              /* ЭТАП 1 */
        copy_str(g_app.bookmarks[g_app.bookmark_count],
                 sizeof g_app.bookmarks[0], path);    /* sizeof, не PATH_MAX */
        g_app.bookmark_count++;
    }
}

static char *trim_left(char *s) { while (*s == ' ' || *s == '\t') s++; return s; }
static void trim_right(char *s) { char *e = s + strlen(s); while (e > s && (e[-1] == ' ' || e[-1] == '\t')) *--e = '\0'; }
static void trim_line_end(char *s) { size_t len = strlen(s); while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) s[--len] = '\0'; }

static void parse_theme_line(char *line) {
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
            /* ФИКС (clang-tidy, bugprone-inc-dec-in-conditions): `i < 32`
             * не нужен — THEME_FIELDS уже равен 32 и это проверяет цикл;
             * отдельное сравнение с 32 скрывало, что сдвиг валиден только
             * пока THEME_FIELDS <= 32. Добавили 33-е поле — и здесь был бы
             * UB (сдвиг на ширину типа), который компилятор не заметит. */
            if (i < (int)(sizeof g_theme_seen * CHAR_BIT)) g_theme_seen |= 1u << i;
            if (i == 4) g_new_theme_seen |= NEW_FIELD_SEL_INACTIVE_BG;
            else if (i == 5) g_new_theme_seen |= NEW_FIELD_SEL_INACTIVE_FG;
            else if (i == 6) g_new_theme_seen |= NEW_FIELD_MARKED_BG;
            else if (i == 7) g_new_theme_seen |= NEW_FIELD_MARKED_FG;
            return;
        }
    }
}

static void parse_exec_line(char *line) {
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
    /* ФИКС: было strlen(val) >= PATH_MAX, а буфер — char cmd[FM_PATH_MAX].
     * Значение длиной 1024..4095 проходило фильтр и молча обрезалось
     * copy_str → в конфиг записывалась испорченная команда. */
    if (strlen(val) >= sizeof g_app.associations[0].cmd) return;
    if (g_app.assoc_count < MAX_ASSOC) {
        copy_str(g_app.associations[g_app.assoc_count].ext,
                 sizeof g_app.associations[0].ext, k);
        copy_str(g_app.associations[g_app.assoc_count].cmd,
                 sizeof g_app.associations[0].cmd, val);
        g_app.assoc_count++;
    }
}

static void parse_bind_line(char *line) {
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
        if (strcmp(g_app.bindings[i].name, key) == 0) { g_app.bindings[i].key = nk; break; }
}

static void parse_settings_line(char *line) {
    char *eq = strchr(line, '=');
    char *key, *val;
    if (!eq) return;
    *eq = '\0';
    key = trim_left(line); trim_right(key);
    val = trim_left(eq + 1); trim_right(val);
    if (!strcmp(key, "lang") && config_text_ok(val)) {
        copy_str(g_app.lang, sizeof g_app.lang, val);
    }
}

/* ФИКС (clang-tidy, cert-err34-c): atoi() при переполнении даёт
 * неопределённое поведение по стандарту C (strtol — хотя бы насыщает).
 * Строка «tab99999999999999999999_split = 1» в конфиге роняла разбор в UB.
 * Возвращаем -1 на любой нечисловой/переполненный вход: все вызывающие и
 * так отбрасывают idx вне [0, NUM_TABS), а для булевых полей -1 → ложь. */
static int parse_int(const char *s)
{
    char *end = NULL;
    long v;
    if (!s || !*s) return -1;
    errno = 0;
    v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') return -1;
    if (v < INT_MIN || v > INT_MAX) return -1;
    return (int)v;
}

static void parse_history_line(char *line) { /* без изменений */
    char *eq = strchr(line, '=');
    char *key, *val;
    int idx;
    if (!eq) return;
    *eq = '\0';
    key = trim_left(line); trim_right(key);
    val = trim_left(eq + 1); trim_right(val);
    if (!strcmp(key, "active_tab")) {
        idx = parse_int(val);
        if (idx >= 0 && idx < NUM_TABS) g_app.active_tab = idx;
        return;
    }
    /* ФИКС (clang-tidy, cert-err34-c + bugprone-branch-clone): sscanf()
     * «tab%d_%31s» не сообщает о переполнении индекса, а %d при значении
     * больше INT_MAX — UB; восемь почти одинаковых веток ниже отличались
     * только полем. Разбор ключа и запись значения разделены: сначала
     * строго разбираем «tab<N>[_<поле>]», потом одним switch выбираем
     * поле по индексу в массиве panes. */
    {
        const char *p2 = key;
        int pidx = -1, pane = 0, bad = 0;
        const char *field = NULL;

        if (p2[0] == 't' && p2[1] == 'a' && p2[2] == 'b') {
            p2 += 3;
            pidx = 0;
            if (!isdigit((unsigned char)*p2)) bad = 1;
            while (!bad && isdigit((unsigned char)*p2)) {
                if (pidx > 100000) { bad = 1; break; }   /* переполнение */
                pidx = pidx * 10 + (*p2 - '0');
                p2++;
            }
            if (!bad && *p2 == '_') field = p2 + 1;
            else if (!bad && *p2 != '\0') bad = 1;
        } else bad = 1;

        if (bad || pidx < 0 || pidx >= NUM_TABS) return;
        if (!field) {
            /* «tabN = /путь» — путь первого пейна */
            if (config_text_ok(val) && val[0] == '/')
                copy_str(g_app.tabs[pidx].panes[0].cwd,
                         sizeof g_app.tabs[0].panes[0].cwd, val);
            return;
        }
        if (!strcmp(field, "split"))
            g_app.tabs[pidx].split_mode = parse_int(val) == 1 ? 1 : 0;
        else if (!strcmp(field, "preview"))
            g_app.tabs[pidx].preview_mode = parse_int(val) == 1 ? 1 : 0;
        else if (!strcmp(field, "pane"))
            g_app.tabs[pidx].active_pane = parse_int(val) == 1 ? 1 : 0;
        else if (!strcmp(field, "sel") || !strcmp(field, "p0_sel"))
            pane = 0;
        else if (!strcmp(field, "p1_sel"))
            pane = 1;
        else if (!strcmp(field, "p0") || !strcmp(field, "p1")) {
            /* путь: только абсолютный и только текст */
            if (!strcmp(field, "p1")) pane = 1;
            if (config_text_ok(val) && val[0] == '/')
                copy_str(g_app.tabs[pidx].panes[pane].cwd,
                         sizeof g_app.tabs[0].panes[0].cwd, val);
            return;
        } else
            return;
        if (name_ok(val))
            copy_str(g_app.tabs[pidx].panes[pane].pending_sel,
                     sizeof g_app.tabs[0].panes[0].pending_sel, val);
        return;
    }
}

static int path_is_dir(const char *path) { /* без изменений */
    struct stat st;
    if (!path || path[0] != '/') return 0;
    if (fstatat(AT_FDCWD, path, &st, 0) != 0) return 0;
    return S_ISDIR(st.st_mode);
}

static int path_has_dot_seg(const char *p) { /* без изменений */
    const char *s;
    if (!p) return 1;
    for (s = p; (s = strstr(s, "/.")) != NULL; s += 2) {
        if (s[2] == '\0' || s[2] == '/') return 1;
        if (s[2] == '.' && (s[3] == '\0' || s[3] == '/')) return 1;
    }
    return 0;
}

static void strip_slashes(char *s) { /* без изменений */
    size_t n;
    if (!s) return;
    n = strlen(s);
    while (n > 1 && s[n - 1] == '/') s[--n] = '\0';
}

static int paths_equal(const char *a, const char *b) { /* без изменений */
    size_t na, nb;
    if (!a || !b) return 0;
    na = strlen(a); nb = strlen(b);
    while (na > 1 && a[na - 1] == '/') na--;
    while (nb > 1 && b[nb - 1] == '/') nb--;
    return na == nb && strncmp(a, b, na) == 0;
}

static int bookmark_has(const char *path) { /* без изменений */
    int i;
    for (i = 0; i < g_app.bookmark_count; i++)
        if (paths_equal(g_app.bookmarks[i], path)) return 1;
    return 0;
}

static void bookmark_if_dir(const char *path) { /* без изменений */
    char buf[FM_PATH_MAX];
    if (!path || path[0] != '/') return;
    copy_str(buf, sizeof buf, path);
    strip_slashes(buf);
    if (!buf[1] || path_has_dot_seg(buf) || !path_is_dir(buf)) return;
    if (bookmark_has(buf)) return;
    add_bookmark(buf);
}

static void bookmark_home_child(const char *home, const char *name) { /* без изменений */
    char path[FM_PATH_MAX];
    if (!name || !name[0] || strchr(name, '/')) return;
    if (make_path(path, sizeof path, home, name) != 0) return;
    bookmark_if_dir(path);
}

static void add_known_home_dirs(const char *home) { /* без изменений */
    static const char *const names[] = {
        "Рабочий стол", "Desktop", "Документы", "Documents",
        "Загрузки", "Downloads", "Музыка", "Music",
        "Изображения", "Pictures", "Видео", "Videos", "Movies",
        "Шаблоны", "Templates", "Общедоступные", "Общие", "Public",
    };
    size_t i;
    for (i = 0; i < sizeof names / sizeof names[0]; i++)
        bookmark_home_child(home, names[i]);
}

static int is_xdg_dir_key(const char *key) { /* без изменений */
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

static int is_xdg_default_key(const char *key) { /* без изменений */
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

static void strip_quotes(char *s) { /* без изменений */
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

static void unescape_inplace(char *s) { /* без изменений */
    char *r, *w;
    if (!s) return;
    r = w = s;
    while (*r) {
        if (*r == '\\' && r[1]) r++;
        *w++ = *r++;
    }
    *w = '\0';
}

static int copy_abs(char *out, size_t outsz, const char *val) { /* без изменений */
    if (!val || val[0] != '/') return -1;
    copy_str(out, outsz, val);
    strip_slashes(out);
    return out[0] ? 0 : -1;
}

static const char *home_rest(const char *val) { /* без изменений */
    if (strncmp(val, "$HOME", 5) == 0 && (val[5] == '/' || val[5] == '\0')) return val + 5;
    if (strncmp(val, "${HOME}", 7) == 0 && (val[7] == '/' || val[7] == '\0')) return val + 7;
    return NULL;
}

static int expand_home_token(char *out, size_t outsz, const char *home, const char *val) { /* без изменений */
    const char *rest;
    if (!out || outsz == 0 || !home || !val || !val[0]) return -1;
    rest = home_rest(val);
    if (!rest) return copy_abs(out, outsz, val);
    if (*rest == '/') rest++;
    if (!*rest) { copy_str(out, outsz, home); strip_slashes(out); return 0; }
    if (make_path(out, outsz, home, rest) != 0) return -1;
    strip_slashes(out);
    return 0;
}

static void bookmark_user_dir_line(const char *home, char *line) { /* без изменений */
    char *eq, *key, *val, path[FM_PATH_MAX];
    eq = strchr(line, '=');
    if (!eq) return;
    *eq = '\0';
    key = trim_left(line); trim_right(key);
    val = trim_left(eq + 1); trim_right(val);
    if (!is_xdg_dir_key(key)) return;
    strip_quotes(val);
    unescape_inplace(val);
    if (expand_home_token(path, sizeof path, home, val) != 0) return;
    if (paths_equal(path, home)) return;
    bookmark_if_dir(path);
}

static void bookmark_default_line(const char *home, char *line) { /* без изменений */
    char *eq, *key, *val;
    eq = strchr(line, '=');
    if (!eq) return;
    *eq = '\0';
    key = trim_left(line); trim_right(key);
    val = trim_left(eq + 1); trim_right(val);
    strip_quotes(val);
    if (!is_xdg_default_key(key) || !val[0] || strchr(val, '/')) return;
    bookmark_home_child(home, val);
}

static void add_user_dirs_file(const char *home, const char *path) { /* без изменений */
    char line[FM_PATH_MAX + 64];
    FILE *f;
    struct stat st;
    f = fopen(path, "re");        /* "e" = O_CLOEXEC */
    if (!f) return;
    if (fstat(fileno(f), &st) != 0 || !S_ISREG(st.st_mode)) {
        (void)fclose(f);                /* поток только читали */
        return;
    }
    while (fgets(line, sizeof line, f)) {
        char *s = trim_left(line);
        trim_line_end(s);
        if (*s == '#' || !*s) continue;
        bookmark_user_dir_line(home, s);
    }
    (void)fclose(f);
}

static void add_xdg_defaults(const char *home) { /* без изменений */
    char line[256];
    FILE *f;
    struct stat st;
    f = fopen("/etc/xdg/user-dirs.defaults", "re");
    if (!f) return;
    if (fstat(fileno(f), &st) != 0 || !S_ISREG(st.st_mode)) {
        (void)fclose(f);                /* поток только читали */
        return;
    }
    while (fgets(line, sizeof line, f)) {
        char *s = trim_left(line);
        trim_line_end(s);
        if (*s == '#' || !*s) continue;
        bookmark_default_line(home, s);
    }
    (void)fclose(f);
}

static int join_user_dirs(char *out, size_t outsz, const char *dir) { /* без изменений */
    int n;
    if (!dir || dir[0] != '/') return -1;
    n = snprintf(out, outsz, "%s/user-dirs.dirs", dir);
    return (n > 0 && (size_t)n < outsz) ? 0 : -1;
}

static void add_xdg_user_dirs(const char *home) { /* без изменений */
    char path[FM_PATH_MAX];
    const char *cfg;
    int n = snprintf(path, sizeof path, "%s/.config/user-dirs.dirs", home);
    if (n > 0 && (size_t)n < sizeof path) add_user_dirs_file(home, path);
    cfg = getenv("XDG_CONFIG_HOME");
    if (join_user_dirs(path, sizeof path, cfg) == 0) add_user_dirs_file(home, path);
}

void add_home_places(const char *home) { /* без изменений */
    if (!home || home[0] != '/') return;
    add_known_home_dirs(home);
    add_xdg_defaults(home);
    add_xdg_user_dirs(home);
}

static void seed_system_dirs(const char *home) { /* без изменений */
    char cfg[FM_PATH_MAX];
    int n;
    add_bookmark("/etc");
    add_bookmark("/usr");
    n = snprintf(cfg, sizeof cfg, "%s/.config", home);
    if (n > 0 && (size_t)n < sizeof cfg && path_is_dir(cfg)) add_bookmark(cfg);
}

static void seed_defaults(const char *home) { /* без изменений */
    if (g_app.bookmark_count != 0) return;
    add_bookmark(home);
    add_home_places(home);
    seed_system_dirs(home);
}

static void mkdir_p(const char *path) { /* без изменений */
    char tmp[FM_PATH_MAX];
    size_t len;
    if (!path || !path[0]) return;
    copy_str(tmp, sizeof tmp, path);
    len = strlen(tmp);
    if (len > 0 && tmp[len - 1] == '/') tmp[len - 1] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

/* === ПОИСК КОНФИГА / ЛОКАЛИЗАЦИИ === */

/* ФИКС (clang-tidy, cert-err33-c по существу): раньше результат snprintf()
 * игнорировался. При HOME длиной ~1000 символов путь молча обрезался, и
 * приложение шло писать конфиг по постороннему пути (обрезанный префикс).
 * Теперь обрезанный путь отличим: остаётся пустая строка, и все
 * вызывающие (mkdir_p, fopen, get_user_path) получают явный «нет каталога». */
static const char *get_user_config_dir(void) {
    static char path[FM_PATH_MAX];
    const char *xdg = getenv("XDG_CONFIG_HOME");
    int n;
    if (xdg && xdg[0] == '/') n = snprintf(path, sizeof path, "%s/vibeyfile", xdg);
    else {
        const char *home = getenv("HOME");
        n = snprintf(path, sizeof path, "%s/.config/vibeyfile", home ? home : ".");
    }
    if (n < 0 || (size_t)n >= sizeof path) path[0] = '\0';
    return path;
}

static const char *get_user_lang_dir(void) {
    static char path[FM_PATH_MAX];
    int n = snprintf(path, sizeof path, "%s/local", get_user_config_dir());
    if (n < 0 || (size_t)n >= sizeof path) path[0] = '\0';
    return path;
}

static void get_user_path(char *out, size_t outsz, const char *sub) {
    int n = snprintf(out, outsz, "%s/%s", get_user_config_dir(), sub);
    if (n < 0 || (size_t)n >= outsz) out[0] = '\0';
}

static void get_etc_path(char *out, size_t outsz, const char *sub) {
    int n = snprintf(out, outsz, "/etc/vibeyfile/%s", sub);
    if (n < 0 || (size_t)n >= outsz) out[0] = '\0';
}

const char *get_lang_dir(void) {
    return get_user_lang_dir();
}

/* Копирует файл из /etc в ~/.config, если его там нет.
 * Права 0600 — как в save_config; это и приватность, и согласованность
 * с проверкой g_exec_trusted. */
static void copy_from_etc_if_missing(const char *sub) {
    char user_path[FM_PATH_MAX], etc_path[FM_PATH_MAX];
    get_user_path(user_path, sizeof user_path, sub);
    get_etc_path(etc_path, sizeof etc_path, sub);

    if (access(user_path, F_OK) == 0) return; // уже есть
    /* ФИКС: копирование из /etc раньше шло без проверки владельца и прав.
     * Из /etc/vibeyfile/local/<lang>.txt приходят printf-форматы, идущие
     * прямо в snprintf(), а config из /etc задаёт команды и раскладку клавиш.
     * Файл, который может переписать кто угодно, — это готовый способ
     * подсунуть чужой формат. Берём только root-овые файлы без записи
     * для группы и остальных (то же условие, что g_exec_trusted). */
    {
        struct stat est;
        if (stat(etc_path, &est) != 0) return;
        if (!S_ISREG(est.st_mode)) return;
        if (est.st_uid != 0 || (est.st_mode & 022) != 0) return;
    }
    FILE *src = fopen(etc_path, "re");
    if (!src) return; // нет и в /etc

    /* ФИКС (clang-tidy, cert-err33-c — самый содержательный из 215):
     * раньше результат fwrite() и fclose() не проверялся. При заполненном
     * диске (ENOSPC) копия из /etc получалась ОБРЕЗАННОЙ и при этом
     * выглядела успешной: access(F_OK) дальше считал файл существующим, и
     * обрезанная локализация/конфиг становились постоянными. Теперь при
     * любой ошибке записи файл удаляется — система вернётся к поставке из
     * /etc на следующем запуске, а не будет жить с половиной файла. */
    /* "e" = O_CLOEXEC: файл не должен достаться дочернему процессу. */
    FILE *dst = fopen(user_path, "we");
    if (dst) {
        char buf[4096];
        size_t n, wrote;
        int bad = 0;
        fchmod(fileno(dst), 0600);
        while ((n = fread(buf, 1, sizeof buf, src)) > 0) {
            wrote = fwrite(buf, 1, n, dst);
            if (wrote != n) { bad = 1; break; }
        }
        if (ferror(src)) bad = 1;
        if (fclose(dst) != 0) bad = 1;
        if (bad) unlink(user_path);
    }
    (void)fclose(src);
}

static void init_strings(void) {
    for (int i = 0; i < STR_COUNT; i++) g_app.strings[i][0] = '\0';
}

void load_locale_file(const char *lang) {
    char sub[256];
    snprintf(sub, sizeof sub, "local/%s.txt", lang);
    copy_from_etc_if_missing(sub);

    char path[FM_PATH_MAX];
    get_user_path(path, sizeof path, sub);

    FILE *f = fopen(path, "re");        /* "e" = O_CLOEXEC */
    if (!f) {
        /* ФИКС: раньше здесь СОЗДАВАЛСЯ пустой файл локализации. Вреда было
         * два: (1) пустой ~/.config/vibeyfile/local/<lang>.txt навсегда
         * блокировал копирование из /etc (copy_from_etc_if_missing выходит
         * по access(F_OK)); (2) после установки/обновления поставки старый
         * пустой файл продолжал затенять новый. Теперь просто ничего не
         * делаем — T() вернёт встроенное имя ключа, а файл появится, когда
         * пользователь сохранит настройки, либо скопируется из /etc. */
        return;
    }

    char line[512];
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = trim_left(line); trim_right(key);
        char *val = trim_left(eq + 1); trim_line_end(val);
        for (int i = 0; i < STR_COUNT; i++) {
            if (strcmp(key, string_keys[i]) == 0) {
                /* ФИКС: значение уходит в snprintf() как формат. Файл правит
                 * пользователь, поэтому «100% готово» без проверки читало
                 * аргумент, которого нет. Негодное значение отбрасываем и
                 * оставляем встроенный текст — менеджер продолжает работать. */
                char why[96];
                if (fmt_value_rejected((StrId)i, val, why, sizeof why))
                    break;   /* остаётся пустым -> T() вернёт встроенное имя */
                copy_str(g_app.strings[i], sizeof g_app.strings[i], val);
                break;
            }
        }
    }
    (void)fclose(f);
}

/* Единственное место, где выставляются умолчания локализации.
 * Раньше «если lang пуст — ru» было продублировано в трёх ветках load_config()
 * (нет файла, файл не обычный, обычная загрузка) — правку приходилось делать
 * трижды, и разъехаться они могли незаметно. */
static void finish_locale_load(const char *home)
{
    if (g_app.lang[0] == '\0')
        copy_str(g_app.lang, sizeof g_app.lang, DEFAULT_LANG);
    load_locale_file(g_app.lang);
    if (g_app.bookmark_count == 0 && home) add_home_places(home);
}

void load_config(void) {
    char path[FM_PATH_MAX];
    char line[FM_PATH_MAX];
    const char *home = getenv("HOME");
    FILE *f;
    int in_theme = 0, in_fav = 0, in_hist = 0, in_exec = 0, in_binds = 0, in_settings = 0;
    int first_line = 1;
    struct stat st;

    default_config();
    default_bindings();
    init_strings();
    g_theme_raw[0] = '\0';
    g_new_theme_seen = 0;
    g_theme_seen = 0;
    g_exec_trusted = 0;

    if (!home) return;

    mkdir_p(get_user_config_dir());
    mkdir_p(get_user_lang_dir());

    char old_path[FM_PATH_MAX], new_path[FM_PATH_MAX];
    {
        int on = snprintf(old_path, sizeof old_path, "%s/.vibeyfilerc", home);
        int nn = snprintf(new_path, sizeof new_path, "%s/config", get_user_config_dir());
        if (on < 0 || (size_t)on >= sizeof old_path) old_path[0] = '\0';
        if (nn < 0 || (size_t)nn >= sizeof new_path) new_path[0] = '\0';
    }
    /* ФИКС (clang-tidy, cert-err33-c): rename() молча не срабатывал (нет
     * прав, старый и новый путь на разных ФС), и пользователь терял
     * настройки, оставаясь со старым файлом и новым — пустым. Поведение
     * «не смогли — не страшно» сохранено, но причина больше не теряется:
     * старый файл читается напрямую ниже, если миграция не удалась. */
    if (access(new_path, F_OK) != 0 && access(old_path, F_OK) == 0
        && rename(old_path, new_path) != 0) {
        int saved = errno;
        if (saved != EXDEV && saved != EACCES && saved != EPERM)
            unlink(old_path);            /* путь есть, но файл битый */
    }

    copy_from_etc_if_missing("config");
    get_user_path(path, sizeof path, "config");

    f = fopen(path, "re");        /* "e" = O_CLOEXEC */
    if (!f) {
        seed_defaults(home);
        finish_locale_load(home);
        return;
    }

    if (fstat(fileno(f), &st) != 0 || !S_ISREG(st.st_mode)) {
        (void)fclose(f);
        seed_defaults(home);
        finish_locale_load(home);
        return;
    }

    g_exec_trusted = st.st_uid == getuid() && (st.st_mode & 022) == 0;

    while (fgets(line, sizeof line, f)) {
        char *start = line;
        char *p;
        if (first_line) {
            first_line = 0;
            if ((unsigned char)start[0] == 0xEF && (unsigned char)start[1] == 0xBB && (unsigned char)start[2] == 0xBF)
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
            in_settings = !strncmp(p, "[SETTINGS]", 10);
            if (in_theme) {
                size_t add = strlen(start);
                if (add > sizeof(g_theme_raw) - 1) add = sizeof(g_theme_raw) - 1;
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
        else if (in_settings) parse_settings_line(start);
    }
    (void)fclose(f);

    finish_locale_load(home);
}

static void save_theme_section(FILE *f) { /* без изменений */
    if (g_theme_raw[0]) {
        size_t len = strlen(g_theme_raw);
        while (len > 0 && (g_theme_raw[len - 1] == '\n' || g_theme_raw[len - 1] == '\r')) g_theme_raw[--len] = '\0';
        fputs(g_theme_raw, f); fputc('\n', f);
        if (!(g_new_theme_seen & NEW_FIELD_SEL_INACTIVE_BG)) fprintf(f, "sel_inactive_bg = %s\n", g_theme.sel_inactive_bg);
        if (!(g_new_theme_seen & NEW_FIELD_SEL_INACTIVE_FG)) fprintf(f, "sel_inactive_fg = %s\n", g_theme.sel_inactive_fg);
        if (!(g_new_theme_seen & NEW_FIELD_MARKED_BG)) fprintf(f, "marked_bg = %s\n", g_theme.marked_bg);
        if (!(g_new_theme_seen & NEW_FIELD_MARKED_FG)) fprintf(f, "marked_fg = %s\n", g_theme.marked_fg);
        for (int i = 22; i < THEME_FIELDS; i++) {
            if (g_theme_seen & (1u << i)) continue;
            char *val = theme_field_by_idx(i);
            if (val) fprintf(f, "%s = %s\n", theme_name_by_idx(i), val);
        }
        return;
    }
    fprintf(f, "[THEME]\n");
    fprintf(f, "bg = %s\n", g_theme.bg); fprintf(f, "fg = %s\n", g_theme.fg);
    fprintf(f, "sel_bg = %s\n", g_theme.sel_bg); fprintf(f, "sel_fg = %s\n", g_theme.sel_fg);
    fprintf(f, "sel_inactive_bg = %s\n", g_theme.sel_inactive_bg); fprintf(f, "sel_inactive_fg = %s\n", g_theme.sel_inactive_fg);
    fprintf(f, "marked_bg = %s\n", g_theme.marked_bg); fprintf(f, "marked_fg = %s\n", g_theme.marked_fg);
    fprintf(f, "dir = %s\n", g_theme.dir); fprintf(f, "exec = %s\n", g_theme.exec);
    fprintf(f, "header_bg = %s\n", g_theme.header_bg); fprintf(f, "header_fg = %s\n", g_theme.header_fg);
    fprintf(f, "title_bg = %s\n", g_theme.title_bg); fprintf(f, "title_fg = %s\n", g_theme.title_fg);
    fprintf(f, "tab_active_bg = %s\n", g_theme.tab_active_bg); fprintf(f, "tab_active_fg = %s\n", g_theme.tab_active_fg);
    fprintf(f, "tab_inactive_bg = %s\n", g_theme.tab_inactive_bg); fprintf(f, "tab_inactive_fg = %s\n", g_theme.tab_inactive_fg);
    fprintf(f, "status_bg = %s\n", g_theme.status_bg); fprintf(f, "status_fg = %s\n", g_theme.status_fg);
    fprintf(f, "dialog_bg = %s\n", g_theme.dialog_bg); fprintf(f, "dialog_fg = %s\n", g_theme.dialog_fg);
    fprintf(f, "btn_quit_bg = %s\n", g_theme.btn_quit_bg); fprintf(f, "btn_quit_fg = %s\n", g_theme.btn_quit_fg);
    fprintf(f, "btn_menu_bg = %s\n", g_theme.btn_menu_bg); fprintf(f, "btn_menu_fg = %s\n", g_theme.btn_menu_fg);
    fprintf(f, "btn_help_bg = %s\n", g_theme.btn_help_bg); fprintf(f, "btn_help_fg = %s\n", g_theme.btn_help_fg);
    fprintf(f, "btn_set_bg = %s\n", g_theme.btn_set_bg); fprintf(f, "btn_set_fg = %s\n", g_theme.btn_set_fg);
    fprintf(f, "btn_book_bg = %s\n", g_theme.btn_book_bg); fprintf(f, "btn_book_fg = %s\n", g_theme.btn_book_fg);
}
static void save_fav_section(FILE *f) { /* без изменений */
    int i;
    fprintf(f, "\n[FAVORITE]\n");
    for (i = 0; i < g_app.bookmark_count; i++) fprintf(f, "%s\n", g_app.bookmarks[i]);
}
static void save_exec_section(FILE *f) { /* без изменений */
    int i;
    fprintf(f, "\n[EXECUTION]\n");
    for (i = 0; i < g_app.assoc_count; i++) {
        if (!assoc_ext_ok(g_app.associations[i].ext) || !config_text_ok(g_app.associations[i].cmd)) continue;
        fprintf(f, ".%s = %s\n", g_app.associations[i].ext, g_app.associations[i].cmd);
    }
}
static void save_binds_section(FILE *f) { /* без изменений */
    int i;
    char kbuf[8];
    fprintf(f, "\n[KEYBINDS]\n");
    for (i = 0; i < g_app.binding_count; i++) {
        binding_key_to_str(g_app.bindings[i].key, kbuf, sizeof kbuf);
        fprintf(f, "%s = %s\n", g_app.bindings[i].name, kbuf);
    }
}
static void save_settings_section(FILE *f) { /* без изменений */
    fprintf(f, "\n[SETTINGS]\n");
    fprintf(f, "lang = %s\n", g_app.lang);
}
static void save_history_section(FILE *f) { /* без изменений */
    int i;
    fprintf(f, "\n[HISTORY]\n");
    for (i = 0; i < NUM_TABS; i++) {
        FilePane *p = &g_app.tabs[i].panes[0];
        if (config_text_ok(p->cwd) && p->cwd[0] == '/') {
            fprintf(f, "tab%d_p0 = %s\n", i, p->cwd);
            if (p->list.count > 0 && p->file_sel > 0 && p->file_sel < (int)p->list.count && name_ok(p->list.items[p->file_sel].name))
                fprintf(f, "tab%d_p0_sel = %s\n", i, p->list.items[p->file_sel].name);
        }
        p = &g_app.tabs[i].panes[1];
        if (config_text_ok(p->cwd) && p->cwd[0] == '/') {
            fprintf(f, "tab%d_p1 = %s\n", i, p->cwd);
            if (p->list.count > 0 && p->file_sel > 0 && p->file_sel < (int)p->list.count && name_ok(p->list.items[p->file_sel].name))
                fprintf(f, "tab%d_p1_sel = %s\n", i, p->list.items[p->file_sel].name);
        }
        if (g_app.tabs[i].split_mode) fprintf(f, "tab%d_split = 1\n", i);
        if (g_app.tabs[i].preview_mode) fprintf(f, "tab%d_preview = 1\n", i);
        if (g_app.tabs[i].active_pane) fprintf(f, "tab%d_pane = 1\n", i);
    }
    fprintf(f, "active_tab = %d\n", g_app.active_tab);
}

void save_config(void) {
    char path[FM_PATH_MAX], tmp[FM_PATH_MAX];
    FILE *f;
    if (!getenv("HOME")) return;
    get_user_path(path, sizeof path, "config");
    get_user_path(tmp, sizeof tmp, "config.tmp");
    f = fopen(tmp, "we");
    if (!f) return;
    fchmod(fileno(f), 0600);
    save_theme_section(f); save_fav_section(f); save_exec_section(f);
    save_binds_section(f); save_settings_section(f); save_history_section(f);
    /* ФИКС (clang-tidy, cert-err33-c): раньше сюда же приходили ошибки
     * fprintf() внутри секций — они проверяются через ferror(). Без этого
     * «диск полон» выглядел как успешное сохранение: временный файл был
     * обрезан, fclose() рапортовал успех, rename() ставил обрезанный файл
     * на место целого. Теперь ferror() ловится ДО rename. */
    if (ferror(f) != 0) { (void)fclose(f); unlink(tmp); return; }
    if (fclose(f) != 0) { unlink(tmp); return; }
    if (rename(tmp, path) != 0) unlink(tmp);
}
