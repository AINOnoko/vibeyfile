#include "fm.h"

const char *string_keys[STR_COUNT] = {
#define X(id) #id,
    STRINGS(X)
#undef X
};

/* Возвращает локализованную строку. Если локализация недоступна,
 * возвращает:
 *   - для функциональных строк (имена файлов, расширения, кнопки) —
 *     разумный встроенный дефолт;
 *   - для остальных — внутреннее имя ключа без префикса STR_
 *     (читаемое «системное имя»). */
const char *T(StrId id) {
    if (id < 0 || id >= STR_COUNT) return "";
    if (g_app.strings[id][0]) return g_app.strings[id];
    switch (id) {
    case STR_TEMPLATE_DEFAULT_TXT: return "file.txt";
    case STR_TEMPLATE_DEFAULT_PY:  return "file.py";
    case STR_TEMPLATE_DEFAULT_C:   return "file.c";
    case STR_TEMPLATE_DEFAULT_SH:  return "file.sh";
    case STR_FORMAT_ZIP:           return ".zip";
    case STR_FORMAT_TGZ:           return ".tgz";
    case STR_FORMAT_TXZ:           return ".txz";
    case STR_BTN_YES:              return "Yes";
    case STR_BTN_NO:               return "No";
    default:
        /* ФИКС: было string_keys[id] + 4 — «срезать STR_». Срез верен только
         * для имён, где первые 4 символа буквально «STR_», но ключи в
         * STRINGS(X) записаны БЕЗ него (X(STATUS_HELP) → "STATUS_HELP").
         * Поэтому на экран попадало «US_HELP», «R_TITLE», «_DIR» —
         * я сам это и занёс, а увидел только когда проверил запуск БЕЗ
         * файла локализации. Теперь возвращается ключ целиком. */
        return string_keys[id];
    }
}

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

/* Усечение строки по ГРАНИЦЕ СИМВОЛА. snprintf("%s") обрезает по байтам
 * и может разрубить многобайтовую последовательность → на терминале
 * появляются мусорные байты (а в strcmp — «разные» строки, которые
 * выглядят одинаково). */
size_t utf8_truncate(char *dst, size_t dstsz, const char *src)
{
    size_t b;
    if (!dst || dstsz == 0) return 0;
    dst[0] = '\0';
    if (!src) return 0;
    b = utf8_prefix_bytes(src, 1 << 24);   /* все полные символы */
    if (b >= dstsz) {
        b = dstsz - 1;
        /* откатиться до начала последнего символа */
        while (b > 0 && ((unsigned char)src[b] & 0xC0) == 0x80) b--;
    }
    memcpy(dst, src, b);
    dst[b] = '\0';
    return b;
}

/* === БЕЗОПАСНОСТЬ ФОРМАТА: валидатор строк локализации ===
 * Разбирает значение как printf-формат и считает подстановки. Отвергает:
 *  - спецификаторы, которых мы никогда не подставляем (%n, %p, %f, %x...);
 *  - больше FMT_ARGS_MAX подстановок (значит, в перевод вписали лишний %);
 *  - «%» в конце строки и незакрытые/битые флаги.
 * Флаги/ширина/точность разрешены — «%-18s» в переводах легитимен. */
static int fmt_parse(const char *fmt, int *nconv, char *why, size_t whysz)
{
    const char *p;
    if (!fmt) return -1;
    *nconv = 0;
    for (p = fmt; *p; p++) {
        if (*p != '%') continue;
        p++;
        if (*p == '%') continue;                 /* %% — литеральный процент */
        while (*p && strchr("-+ #0'", *p)) p++;  /* флаги */
        while (*p >= '0' && *p <= '9') p++;      /* ширина */
        if (*p == '.') { p++; while (*p >= '0' && *p <= '9') p++; }  /* точность */
        if (!*p) {
            snprintf(why, whysz, "«%%» в конце строки");
            return -1;
        }
        switch (*p) {
        case 's': case 'd': case 'i': case 'u':
        case 'c': case 'l': {
            /* l/ll + d/i/u — допустимая длина; отдельно не разбираем */
            break;
        }
        case 'n': case 'p': case 'x': case 'X': case 'o':
        case 'f': case 'F': case 'g': case 'G': case 'e': case 'E':
        case 'a': case 'A': {
            snprintf(why, whysz, "недопустимый спецификатор «%%%c»", *p);
            return -1;
        }
        default:
            snprintf(why, whysz, "неизвестный спецификатор после «%%» (байт 0x%02X)",
                     (unsigned char)*p);
            return -1;
        }
        (*nconv)++;
        if (*nconv > FMT_ARGS_MAX) {
            snprintf(why, whysz, "больше %d подстановок", FMT_ARGS_MAX);
            return -1;
        }
    }
    return 0;
}

int fmt_key_is_checked(StrId id)
{
    return fmt_key_arity(id) >= 0;
}

/* ФИКС (clang-tidy, bugprone-branch-clone): FMT_STRINGS — это список
 * «ключ, число подстановок», и switch из него давал до 11 совпадающих тел
 * подряд (все ключи с арностью 1). Заменено таблицей значений: она
 * строится один раз из того же макроса, поэтому расхождение между
 * списком ключей и арностью физически невозможно — раньше это держалось
 * на дисциплине копирования. */
static signed char fmt_arity_tab[STR_COUNT];
static unsigned fmt_arity_built;

static void fmt_arity_build(void)
{
    int i;
    if (fmt_arity_built) return;
    /* ВАЖНО: -1 (не формат) и 0 (формат без подстановок) — разные вещи,
     * и в FMT_STRINGS есть ключи с арностью 0 (например, тексты
     * ассоциаций). Поэтому сначала весь массив помечается как «не
     * формат», и только потом FMT_STRINGS проставляет арности, включая
     * нули. Первая версия этой правки заливала нули в -1 и тем самым
     * ВЫКЛЮЧАЛА проверку форматов у четырёх ключей — поймано сверкой
     * с ожидаемыми арностями в test_audit_locale_formats. */
    for (i = 0; i < (int)(sizeof fmt_arity_tab / sizeof fmt_arity_tab[0]); i++)
        fmt_arity_tab[i] = -1;
#define X(k, n) fmt_arity_tab[STR_##k] = (signed char)(n);
    FMT_STRINGS(X)
#undef X
    fmt_arity_built = 1;
}

int fmt_key_arity(StrId id)
{
    if ((int)id < 0 || (int)id >= STR_COUNT) return -1;
    fmt_arity_build();
    return fmt_arity_tab[id];
}

int fmt_value_rejected(StrId id, const char *val, char *why, size_t whysz)
{
    int n = 0, argc = fmt_key_arity(id);
    if (argc < 0) return 0;                     /* не формат — не наше дело */
    if (!val || !val[0]) return 0;              /* пустое = «нет перевода» */
    if (fmt_parse(val, &n, why, whysz) != 0) return 1;
    /* ФИКС: синтаксис верный, но число подстановок не совпадает с тем,
     * сколько аргументов реально передаёт код. «Строка %d из %d» вместо
     * «%s, %d из %d» — это уже чтение чужого аргумента со стека. */
    if (n != argc) {
        snprintf(why, whysz, "подстановок %d, а код передаёт %d", n, argc);
        return 1;
    }
    return 0;
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
        /* ФИКС: символы, которые ext4 примет, а FAT/NTFS — нет.
         * Менеджер копирует на флешки, и создавать файл, который потом
         * невозможно записать на носитель, — плохая услуга. Запрет
         * действует только на СОЗДАНИЕ имени (name_ok), существующие
         * файлы с такими именами по-прежнему видны и открываются. */
        if (c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|')
            return 0;
    }
    /* Windows не хранит точку/пробел в конце имени — такой файл
     * «исчезнет» при копировании. */
    if (name[n - 1] == '.' || name[n - 1] == ' ') return 0;
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
    StrId kind_id;
    int text;
} ExtInfo;

static const ExtInfo k_exts[] = {
    {"txt", 'T', STR_TYPE_TEXT, 1}, {"log", 'T', STR_TYPE_TEXT, 1},
    {"md", 'M', STR_TYPE_TEXT, 1}, {"rst", 0, STR_TYPE_TEXT, 1},
    {"c", 'C', STR_TYPE_CODE, 1}, {"h", 'C', STR_TYPE_CODE, 1},
    {"cpp", 'C', STR_TYPE_CODE, 1}, {"cc", 'C', STR_TYPE_CODE, 1}, {"hpp", 'C', STR_TYPE_CODE, 1},
    {"py", 'P', STR_TYPE_CODE, 1}, {"rs", 'R', STR_TYPE_CODE, 1}, {"go", 'G', STR_TYPE_CODE, 1},
    {"js", 'J', STR_TYPE_CODE, 1}, {"ts", 'J', STR_TYPE_CODE, 1},
    {"java", 0, STR_TYPE_CODE, 1}, {"rb", 0, STR_TYPE_CODE, 1},
    {"json", '{', STR_TYPE_CONF, 1}, {"yaml", 'Y', STR_TYPE_CONF, 1},
    {"yml", 'Y', STR_TYPE_CONF, 1}, {"toml", 'Y', STR_TYPE_CONF, 1},
    {"ini", 0, STR_TYPE_CONF, 1}, {"xml", '<', STR_TYPE_CONF, 1},
    {"sh", '$', STR_TYPE_SCRIPT, 1}, {"bash", '$', STR_TYPE_SCRIPT, 1},
    {"zsh", '$', STR_TYPE_SCRIPT, 1}, {"fish", 0, STR_TYPE_SCRIPT, 1},
    {"html", 'W', STR_TYPE_WEB, 0}, {"htm", 'W', STR_TYPE_WEB, 0},
    {"css", 'S', STR_TYPE_WEB, 0}, {"scss", 0, STR_TYPE_WEB, 0},
    {"png", 'I', STR_TYPE_IMG, 0}, {"jpg", 'I', STR_TYPE_IMG, 0},
    {"jpeg", 'I', STR_TYPE_IMG, 0}, {"gif", 'I', STR_TYPE_IMG, 0},
    {"bmp", 'I', STR_TYPE_IMG, 0}, {"svg", 'I', STR_TYPE_IMG, 0}, {"webp", 0, STR_TYPE_IMG, 0},
    {"mp3", 'A', STR_TYPE_AUDIO, 0}, {"wav", 'A', STR_TYPE_AUDIO, 0},
    {"flac", 'A', STR_TYPE_AUDIO, 0}, {"ogg", 'A', STR_TYPE_AUDIO, 0},
    {"mp4", 'V', STR_TYPE_VIDEO, 0}, {"mkv", 'V', STR_TYPE_VIDEO, 0},
    {"avi", 'V', STR_TYPE_VIDEO, 0}, {"webm", 'V', STR_TYPE_VIDEO, 0},
    {"zip", 'Z', STR_TYPE_ARCHIVE, 0}, {"tar", 'Z', STR_TYPE_ARCHIVE, 0},
    {"gz", 'Z', STR_TYPE_ARCHIVE, 0}, {"xz", 'Z', STR_TYPE_ARCHIVE, 0},
    {"bz2", 'Z', STR_TYPE_ARCHIVE, 0}, {"7z", 'Z', STR_TYPE_ARCHIVE, 0},
    {"pdf", 'F', STR_TYPE_PDF, 0},
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

char file_icon(const char *name, int is_dir, int is_link, mode_t mode)
{
    const ExtInfo *info;
    if (!name) return '-';
    if (is_link) return '@';
    if (is_dir) return 'D';
    if (name[0] == '.') return '.';
    if (mode_exec(mode)) return 'X';
    info = lookup_ext(name);
    return (char)((info && info->icon) ? info->icon : '-');
}

const char *type_str(const char *name, int is_dir, int is_link, mode_t mode)
{
    const ExtInfo *info;
    if (!name) return T(STR_TYPE_FILE);
    if (is_dir) return T(STR_TYPE_DIR);
    if (is_link) return T(STR_TYPE_LINK);
    info = lookup_ext(name);
    if (info && info->kind_id) return T(info->kind_id);
    return mode_exec(mode) ? T(STR_TYPE_EXEC) : T(STR_TYPE_FILE);
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
    if (key == '\n')                 copy_str(out, outsz, T(STR_KEY_ENTER));
    else if (key == '\t')            copy_str(out, outsz, T(STR_KEY_TAB));
    else if (key == ' ')             copy_str(out, outsz, T(STR_KEY_SPACE));
    else if (key == 27)              copy_str(out, outsz, T(STR_KEY_ESC));
    else if (key >= KEY_F(1) && key <= KEY_F(12))
        snprintf(out, outsz, "F%d", key - KEY_F(1) + 1);
    else if (key == KEY_UP)          copy_str(out, outsz, T(STR_KEY_UP));
    else if (key == KEY_DOWN)        copy_str(out, outsz, T(STR_KEY_DOWN));
    else if (key == KEY_LEFT)        copy_str(out, outsz, T(STR_KEY_LEFT));
    else if (key == KEY_RIGHT)       copy_str(out, outsz, T(STR_KEY_RIGHT));
    else if (key == KEY_HOME)        copy_str(out, outsz, T(STR_KEY_HOME));
    else if (key == KEY_END)         copy_str(out, outsz, T(STR_KEY_END));
    else if (key == KEY_PPAGE)       copy_str(out, outsz, T(STR_KEY_PGUP));
    else if (key == KEY_NPAGE)       copy_str(out, outsz, T(STR_KEY_PGDN));
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

/* ЭТАП 1: вектор указателей вместо calloc(n, PATH_MAX).
 * 256 * 4096 = 1 МиБ calloc'а на каждую операцию delete/pin/archive
 * превращается в 256 * sizeof(char*) = 2 КиБ: сами пути выделяются
 * по фактической длине через strdup() в get_selected_or_current().
 */
char **alloc_path_vec(int n)
{
    if (n <= 0) return NULL;
    /* C: calloc возвращает void*, приведение к char** неявное и корректное;
     * в C++ так нельзя, отсюда и предупреждение clang-tidy. */
    return calloc((size_t)n, sizeof(char *));
}

void free_path_vec(char **v, int n)
{
    int i;
    if (!v) return;
    for (i = 0; i < n; i++) free(v[i]);
    free(v);
}
