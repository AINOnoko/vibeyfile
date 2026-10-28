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

char file_icon(const char *name, int is_dir, mode_t mode)
{
    const char *dot;
    if (!name) return '-';
    if (is_dir)                     return 'D';
    if (name[0] == '.')             return '.';
    if (mode & (S_IXUSR | S_IXGRP | S_IXOTH)) return 'X';
    dot = strrchr(name, '.');
    if (!dot || dot == name)        return '-';
    dot++;
    if (!strcasecmp(dot, "txt") || !strcasecmp(dot, "log")) return 'T';
    if (!strcasecmp(dot, "c") || !strcasecmp(dot, "h"))     return 'C';
    if (!strcasecmp(dot, "cpp") || !strcasecmp(dot, "cc")
        || !strcasecmp(dot, "hpp")) return 'C';
    if (!strcasecmp(dot, "py"))     return 'P';
    if (!strcasecmp(dot, "rs"))     return 'R';
    if (!strcasecmp(dot, "go"))     return 'G';
    if (!strcasecmp(dot, "js") || !strcasecmp(dot, "ts"))    return 'J';
    if (!strcasecmp(dot, "html") || !strcasecmp(dot, "htm")) return 'W';
    if (!strcasecmp(dot, "css"))    return 'S';
    if (!strcasecmp(dot, "md"))     return 'M';
    if (!strcasecmp(dot, "json"))   return '{';
    if (!strcasecmp(dot, "yaml") || !strcasecmp(dot, "yml")
        || !strcasecmp(dot, "toml")) return 'Y';
    if (!strcasecmp(dot, "xml"))    return '<';
    if (!strcasecmp(dot, "sh") || !strcasecmp(dot, "bash")
        || !strcasecmp(dot, "zsh")) return '$';
    if (!strcasecmp(dot, "png") || !strcasecmp(dot, "jpg")
        || !strcasecmp(dot, "jpeg") || !strcasecmp(dot, "gif")
        || !strcasecmp(dot, "bmp") || !strcasecmp(dot, "svg")) return 'I';
    if (!strcasecmp(dot, "mp3") || !strcasecmp(dot, "wav")
        || !strcasecmp(dot, "flac") || !strcasecmp(dot, "ogg")) return 'A';
    if (!strcasecmp(dot, "mp4") || !strcasecmp(dot, "mkv")
        || !strcasecmp(dot, "avi") || !strcasecmp(dot, "webm")) return 'V';
    if (!strcasecmp(dot, "zip") || !strcasecmp(dot, "tar")
        || !strcasecmp(dot, "gz")  || !strcasecmp(dot, "xz")
        || !strcasecmp(dot, "bz2") || !strcasecmp(dot, "7z")) return 'Z';
    if (!strcasecmp(dot, "pdf"))    return 'F';
    return '-';
}

const char *type_str(const char *name, int is_dir, mode_t mode)
{
    const char *dot;
    int is_exec;
    if (!name) return "файл";
    if (is_dir) return "папка";
    is_exec = (mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0;
    dot = strrchr(name, '.');
    if (!dot || dot == name) return is_exec ? "исп" : "файл";
    dot++;
    if (!strcasecmp(dot, "txt") || !strcasecmp(dot, "log")
        || !strcasecmp(dot, "md") || !strcasecmp(dot, "rst")) return "текст";
    if (!strcasecmp(dot, "c") || !strcasecmp(dot, "h")
        || !strcasecmp(dot, "cpp") || !strcasecmp(dot, "cc")
        || !strcasecmp(dot, "hpp") || !strcasecmp(dot, "py")
        || !strcasecmp(dot, "rs") || !strcasecmp(dot, "go")
        || !strcasecmp(dot, "js") || !strcasecmp(dot, "ts")
        || !strcasecmp(dot, "java") || !strcasecmp(dot, "rb")) return "код";
    if (!strcasecmp(dot, "json") || !strcasecmp(dot, "yaml")
        || !strcasecmp(dot, "yml") || !strcasecmp(dot, "toml")
        || !strcasecmp(dot, "ini") || !strcasecmp(dot, "xml")) return "конфиг";
    if (!strcasecmp(dot, "sh") || !strcasecmp(dot, "bash")
        || !strcasecmp(dot, "zsh") || !strcasecmp(dot, "fish")) return "скрипт";
    if (!strcasecmp(dot, "html") || !strcasecmp(dot, "htm")
        || !strcasecmp(dot, "css") || !strcasecmp(dot, "scss")) return "веб";
    if (!strcasecmp(dot, "png") || !strcasecmp(dot, "jpg")
        || !strcasecmp(dot, "jpeg") || !strcasecmp(dot, "gif")
        || !strcasecmp(dot, "bmp") || !strcasecmp(dot, "svg")
        || !strcasecmp(dot, "webp")) return "изобр";
    if (!strcasecmp(dot, "mp3") || !strcasecmp(dot, "wav")
        || !strcasecmp(dot, "flac") || !strcasecmp(dot, "ogg")) return "аудио";
    if (!strcasecmp(dot, "mp4") || !strcasecmp(dot, "mkv")
        || !strcasecmp(dot, "avi") || !strcasecmp(dot, "webm")) return "видео";
    if (!strcasecmp(dot, "zip") || !strcasecmp(dot, "tar")
        || !strcasecmp(dot, "gz")  || !strcasecmp(dot, "xz")
        || !strcasecmp(dot, "bz2") || !strcasecmp(dot, "7z")) return "архив";
    if (!strcasecmp(dot, "pdf")) return "PDF";
    return "файл";
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
    else if (key >= 32 && key < 127) { out[0] = (char)key; out[1] = '\0'; }
    else                             snprintf(out, outsz, "?");
}
