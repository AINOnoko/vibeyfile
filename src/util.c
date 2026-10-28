#include "fm.h"

void copy_str(char *dst, size_t dstsz, const char *src)
{
    size_t len;
    if (!dst || dstsz == 0) return;
    if (!src) src = "";
    len = strlen(src);
    if (len >= dstsz) len = dstsz - 1;
    memmove(dst, src, len);
    dst[len] = '\0';
}

int utf8_strlen(const char *s)
{
    int n = 0;
    if (!s) return 0;
    for (; *s; s++)
        if (((unsigned char)*s & 0xC0) != 0x80) n++;
    return n;
}

static int utf8_step_at(const char *s, size_t i, size_t len)
{
    unsigned char c;
    int step, k;
    if (!s || i >= len) return 0;
    c = (unsigned char)s[i];
    if ((c & 0x80) == 0x00) return 1;
    if ((c & 0xE0) == 0xC0) step = 2;
    else if ((c & 0xF0) == 0xE0) step = 3;
    else if ((c & 0xF8) == 0xF0) step = 4;
    else return 1;
    if (i + (size_t)step > len) return 1;
    for (k = 1; k < step; k++) {
        if (((unsigned char)s[i + (size_t)k] & 0xC0) != 0x80)
            return 1;
    }
    return step;
}

void mvwaddn_utf8(WINDOW *win, int y, int x, const char *s, int max_chars)
{
    size_t n;
    if (!win || max_chars < 1) return;
    if (!s) s = "";
    n = utf8_prefix_bytes(s, max_chars);
    mvwaddnstr(win, y, x, s, (int)n);
}

size_t utf8_prefix_bytes(const char *s, int max_chars)
{
    size_t len, bytes = 0;
    int chars = 0;
    if (!s || max_chars <= 0) return 0;
    len = strlen(s);
    while (bytes < len && chars < max_chars) {
        int step = utf8_step_at(s, bytes, len);
        if (step <= 0) break;
        bytes += (size_t)step;
        chars++;
    }
    return bytes;
}

void build_padded(char *out, size_t outsz, const char *in,
                  int width, int right)
{
    size_t len, bytes = 0;
    int chars = 0, pad, o, i, max_bytes;
    if (!out || outsz == 0) return;
    out[0] = '\0';
    if (!in) in = "";
    if (width < 0) width = 0;
    max_bytes = (int)outsz - 1;
    if (max_bytes < 0) return;
    len = strlen(in);
    while (bytes < len && chars < width) {
        int step = utf8_step_at(in, bytes, len);
        if (step <= 0 || (int)bytes + step > max_bytes) break;
        bytes += (size_t)step;
        chars++;
    }
    pad = width - chars;
    if (pad < 0) pad = 0;
    o = 0;
    if (right)
        for (i = 0; i < pad && o < max_bytes; i++) out[o++] = ' ';
    for (i = 0; i < (int)bytes && o < max_bytes; i++) out[o++] = in[i];
    if (!right)
        for (i = 0; i < pad && o < max_bytes; i++) out[o++] = ' ';
    out[o] = '\0';
}

void format_size(char *out, size_t outsz, off_t size)
{
    const char *units[] = { "B", "K", "M", "G", "T" };
    double v;
    int u = 0;
    if (!out || outsz == 0) return;
    if (size < 0) size = 0;
    v = (double)size;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; u++; }
    if (u == 0) snprintf(out, outsz, "%lld%s", (long long)size, units[0]);
    else        snprintf(out, outsz, "%.1f%s", v, units[u]);
}

void format_mode(char *out, size_t outsz, mode_t mode)
{
    if (!out || outsz == 0) return;
    if (outsz < 10) { out[0] = '\0'; return; }
    out[0] = (mode & S_IRUSR) ? 'r' : '-';
    out[1] = (mode & S_IWUSR) ? 'w' : '-';
    out[2] = (mode & S_IXUSR) ? 'x' : '-';
    out[3] = (mode & S_IRGRP) ? 'r' : '-';
    out[4] = (mode & S_IWGRP) ? 'w' : '-';
    out[5] = (mode & S_IXGRP) ? 'x' : '-';
    out[6] = (mode & S_IROTH) ? 'r' : '-';
    out[7] = (mode & S_IWOTH) ? 'w' : '-';
    out[8] = (mode & S_IXOTH) ? 'x' : '-';
    out[9] = '\0';
}

int make_path(char *out, size_t outsz, const char *dir, const char *name)
{
    size_t dl, nl;
    int root;
    if (!out || outsz == 0 || !dir || !name) return -1;
    dl = strlen(dir);
    nl = strlen(name);
    root = (dl == 1 && dir[0] == '/');
    if (root) {
        if (nl + 2 > outsz) return -1;
        out[0] = '/';
        memcpy(out + 1, name, nl);
        out[1 + nl] = '\0';
        return 0;
    }
    while (dl > 1 && dir[dl - 1] == '/') dl--;
    if (dl + 1 + nl + 1 > outsz) return -1;
    memcpy(out, dir, dl);
    out[dl] = '/';
    memcpy(out + dl + 1, name, nl);
    out[dl + 1 + nl] = '\0';
    return 0;
}

int name_ok(const char *name)
{
    size_t n, i;
    if (!name || !name[0]) return 0;
    if (!strcmp(name, ".") || !strcmp(name, "..")) return 0;
    n = strlen(name);
    if (n >= MAX_NAME || n > 255) return 0;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c == '/' || c == '\\' || c < 32 || c == 127) return 0;
    }
    return 1;
}

int path_parent(char *out, size_t outsz, const char *path)
{
    size_t n;
    char *slash;
    if (!out || outsz == 0 || !path || !path[0]) return -1;
    if (!strcmp(path, "/")) return -1;
    n = strlen(path);
    if (n + 1 > outsz) return -1;
    memcpy(out, path, n + 1);
    while (n > 1 && out[n - 1] == '/') {
        out[--n] = '\0';
    }
    slash = strrchr(out, '/');
    if (!slash) return -1;
    if (slash == out) out[1] = '\0';
    else *slash = '\0';
    return 0;
}

int path_is_inside(const char *parent, const char *child)
{
    size_t n;
    if (!parent || !child || !parent[0] || !child[0]) return 0;
    if (!strcmp(parent, child)) return 1;
    n = strlen(parent);
    if (n == 1 && parent[0] == '/') return child[0] == '/';
    return strncmp(parent, child, n) == 0 && child[n] == '/';
}

static size_t home_prefix_len(const char *home)
{
    size_t n;
    if (!home || home[0] != '/' || !home[1]) return 0;
    n = strlen(home);
    while (n > 1 && home[n - 1] == '/') n--;
    return n;
}

static int path_under_home(const char *home, size_t n, const char *path)
{
    if (!n || !path || strncmp(path, home, n) != 0) return 0;
    return path[n] == '\0' || path[n] == '/';
}

static void copy_tilde(char *out, size_t outsz, const char *rest)
{
    if (!rest || !rest[0]) {
        copy_str(out, outsz, "~");
        return;
    }
    if (outsz < 3) {
        out[0] = '\0';
        return;
    }
    out[0] = '~';
    out[1] = '/';
    copy_str(out + 2, outsz - 2, rest);
}

void path_display(char *out, size_t outsz, const char *path)
{
    const char *home = getenv("HOME");
    size_t n;
    if (!out || outsz == 0) return;
    if (!path) path = "";
    n = home_prefix_len(home);
    if (!path_under_home(home, n, path)) {
        copy_str(out, outsz, path);
        return;
    }
    if (path[n] == '/') n++;
    copy_tilde(out, outsz, path + n);
}

typedef struct {
    const char *ext;
    char icon;
    const char *kind;
    int text;
} ExtInfo;

static const ExtInfo k_exts[] = {
    {"txt", 'T', "текст", 1}, {"log", 'T', "текст", 1},
    {"md", 'M', "текст", 1}, {"rst", 0, "текст", 1},
    {"c", 'C', "код", 1}, {"h", 'C', "код", 1},
    {"cpp", 'C', "код", 1}, {"cc", 'C', "код", 1}, {"hpp", 'C', "код", 1},
    {"py", 'P', "код", 1}, {"rs", 'R', "код", 1}, {"go", 'G', "код", 1},
    {"js", 'J', "код", 1}, {"ts", 'J', "код", 1},
    {"java", 0, "код", 1}, {"rb", 0, "код", 1},
    {"json", '{', "конфиг", 1}, {"yaml", 'Y', "конфиг", 1},
    {"yml", 'Y', "конфиг", 1}, {"toml", 'Y', "конфиг", 1},
    {"ini", 0, "конфиг", 1}, {"xml", '<', "конфиг", 1},
    {"sh", '$', "скрипт", 1}, {"bash", '$', "скрипт", 1},
    {"zsh", '$', "скрипт", 1}, {"fish", 0, "скрипт", 1},
    {"html", 'W', "веб", 0}, {"htm", 'W', "веб", 0},
    {"css", 'S', "веб", 0}, {"scss", 0, "веб", 0},
    {"png", 'I', "изобр", 0}, {"jpg", 'I', "изобр", 0},
    {"jpeg", 'I', "изобр", 0}, {"gif", 'I', "изобр", 0},
    {"bmp", 'I', "изобр", 0}, {"svg", 'I', "изобр", 0}, {"webp", 0, "изобр", 0},
    {"mp3", 'A', "аудио", 0}, {"wav", 'A', "аудио", 0},
    {"flac", 'A', "аудио", 0}, {"ogg", 'A', "аудио", 0},
    {"mp4", 'V', "видео", 0}, {"mkv", 'V', "видео", 0},
    {"avi", 'V', "видео", 0}, {"webm", 'V', "видео", 0},
    {"zip", 'Z', "архив", 0}, {"tar", 'Z', "архив", 0},
    {"gz", 'Z', "архив", 0}, {"xz", 'Z', "архив", 0},
    {"bz2", 'Z', "архив", 0}, {"7z", 'Z', "архив", 0},
    {"pdf", 'F', "PDF", 0},
};

static const ExtInfo *lookup_ext(const char *name)
{
    const char *dot;
    size_t i;
    if (!name) return NULL;
    dot = strrchr(name, '.');
    if (!dot || dot == name || !dot[1]) return NULL;
    for (i = 0; i < sizeof k_exts / sizeof k_exts[0]; i++)
        if (!strcasecmp(dot + 1, k_exts[i].ext)) return &k_exts[i];
    return NULL;
}

static int mode_exec(mode_t mode)
{
    return (mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0;
}

char file_icon(const char *name, int is_dir, mode_t mode)
{
    const ExtInfo *info;
    if (!name) return '-';
    if (is_dir) return 'D';
    if (name[0] == '.') return '.';
    if (mode_exec(mode)) return 'X';
    info = lookup_ext(name);
    return (info && info->icon) ? info->icon : '-';
}

const char *type_str(const char *name, int is_dir, mode_t mode)
{
    const ExtInfo *info;
    if (!name) return "файл";
    if (is_dir) return "папка";
    info = lookup_ext(name);
    if (info && info->kind) return info->kind;
    return mode_exec(mode) ? "исп" : "файл";
}

int is_text_file(const char *name)
{
    const ExtInfo *info = lookup_ext(name);
    return info && info->text;
}

int binding_key_by_name(const char *name)
{
    int i;
    if (!name) return 0;
    for (i = 0; i < g_app.binding_count; i++)
        if (strcmp(g_app.bindings[i].name, name) == 0)
            return g_app.bindings[i].key;
    return 0;
}

void key_display_name(int key, char *out, size_t outsz)
{
    if (!out || outsz == 0) return;
    if (key == '\n')                 snprintf(out, outsz, "Enter");
    else if (key == '\t')            snprintf(out, outsz, "Tab");
    else if (key == ' ')             snprintf(out, outsz, "Space");
    else if (key == 27)              snprintf(out, outsz, "Esc");
    else if (key >= KEY_F(1) && key <= KEY_F(12))
        snprintf(out, outsz, "F%d", key - KEY_F(1) + 1);
    else if (key == KEY_UP)          snprintf(out, outsz, "Up");
    else if (key == KEY_DOWN)        snprintf(out, outsz, "Down");
    else if (key == KEY_LEFT)        snprintf(out, outsz, "Left");
    else if (key == KEY_RIGHT)       snprintf(out, outsz, "Right");
    else if (key == KEY_HOME)        snprintf(out, outsz, "Home");
    else if (key == KEY_END)         snprintf(out, outsz, "End");
    else if (key == KEY_PPAGE)       snprintf(out, outsz, "PgUp");
    else if (key == KEY_NPAGE)       snprintf(out, outsz, "PgDn");
    else if (key >= 32 && key < 127) { out[0] = (char)key; out[1] = '\0'; }
    else                             snprintf(out, outsz, "?");
}

static int named_key_code(const char *s)
{
    static const struct { const char *name; int key; } tab[] = {
        { "UP", KEY_UP }, { "DOWN", KEY_DOWN },
        { "LEFT", KEY_LEFT }, { "RIGHT", KEY_RIGHT },
        { "HOME", KEY_HOME }, { "END", KEY_END },
        { "PGUP", KEY_PPAGE }, { "PGDN", KEY_NPAGE },
        { "BS", KEY_BACKSPACE }, { "DEL", KEY_DC },
    };
    size_t i;
    if (s[0] == 'F') {
        int n = 0;
        const char *p = s + 1;
        if (*p < '1' || *p > '9') return 0;
        while (*p >= '0' && *p <= '9') n = n * 10 + (*p++ - '0');
        if (*p == '\0' && n >= 1 && n <= 12) return KEY_F(n);
    }
    for (i = 0; i < sizeof tab / sizeof tab[0]; i++)
        if (!strcmp(s, tab[i].name)) return tab[i].key;
    return 0;
}

static const char *named_key_str(int key)
{
    if (key == KEY_UP) return "UP";
    if (key == KEY_DOWN) return "DOWN";
    if (key == KEY_LEFT) return "LEFT";
    if (key == KEY_RIGHT) return "RIGHT";
    if (key == KEY_HOME) return "HOME";
    if (key == KEY_END) return "END";
    if (key == KEY_PPAGE) return "PGUP";
    if (key == KEY_NPAGE) return "PGDN";
    if (key == KEY_BACKSPACE) return "BS";
    if (key == KEY_DC) return "DEL";
    return NULL;
}

int binding_str_to_key(const char *s)
{
    int named;
    if (!s || !s[0]) return 0;
    named = named_key_code(s);
    if (named) return named;
    if (s[0] == '\\' && s[1] == 'n') return '\n';
    if (s[0] == '\\' && s[1] == 't') return '\t';
    if (s[0] == '\\' && s[1] == 's') return ' ';
    if (s[0] == '\\' && s[1] == 'e') return 27;
    if ((unsigned char)s[0] >= 32 && (unsigned char)s[0] < 127)
        return (unsigned char)s[0];
    return 0;
}

void binding_key_to_str(int key, char *out, size_t outsz)
{
    const char *named;
    if (!out || outsz == 0) return;
    if (key >= KEY_F(1) && key <= KEY_F(12)) {
        snprintf(out, outsz, "F%d", key - KEY_F(1) + 1);
        return;
    }
    named = named_key_str(key);
    if (named) { snprintf(out, outsz, "%s", named); return; }
    if (key == '\n')                 snprintf(out, outsz, "\\n");
    else if (key == '\t')            snprintf(out, outsz, "\\t");
    else if (key == ' ')             snprintf(out, outsz, "\\s");
    else if (key == 27)              snprintf(out, outsz, "\\e");
    else if (key >= 32 && key < 127) { out[0] = (char)key; out[1] = '\0'; }
    else                             snprintf(out, outsz, "?");
}

char (*alloc_paths(int n))[PATH_MAX]
{
    if (n <= 0) return NULL;
    return calloc((size_t)n, sizeof(char[PATH_MAX]));
}

void free_paths(char (*p)[PATH_MAX])
{
    free(p);
}
