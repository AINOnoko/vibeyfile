/* fuzz.c — фаззер чистых функций под ASan/UBSan.
 * Цели: UTF-8 (util.c), разбор путей, сниффер (exec.c), токенизатор команд.
 * Ловит: переполнение буфера, невалидные чтения, UB в целочисленной арифметике.
 */

#include "fm.h"
#include <limits.h>

static unsigned long long rnd_state = 88172645463325252ULL;
static unsigned rnd(void)
{
    rnd_state ^= rnd_state << 13;
    rnd_state ^= rnd_state >> 7;
    rnd_state ^= rnd_state << 17;
    return (unsigned)(rnd_state >> 11);
}

/* ── генератор «интересных» строк ── */
static void gen_string(char *out, size_t cap)
{
    static const char *seeds[] = {
        "", ".", "..", "/", "//", "///", "a", "ab", "\xD0\xBF", "日本語",
        "\xF0\x9F\x98\x80", "\xC0\x80", "\xE0\x80\x80", "\xF0\x80\x80\x80",
        "\xFF", "\x80", "\xBF", "..//..", "/..", "./.", "a/b", "a//b",
        "\xEF\xBB\xBF" "bom", "~", "~/x", "CON", "a b", "\t\n", "\\",
        "...........................", "%s%n%p", "$(x)", "`id`",
    };
    size_t n = sizeof seeds / sizeof seeds[0];
    size_t len = 0;
    unsigned mode = rnd() % 4;

    if (mode == 0) {                       /* чистый seed */
        const char *s = seeds[rnd() % n];
        snprintf(out, cap, "%s", s);
        return;
    }
    if (mode == 1) {                       /* seed + мусор */
        const char *s = seeds[rnd() % n];
        len = (size_t)snprintf(out, cap, "%s", s);
        while (len + 1 < cap && (rnd() % 3)) out[len++] = (char)(1 + rnd() % 255);
        out[len] = '\0';
        return;
    }
    if (mode == 2) {                       /* случайные байты, включая NUL-безопасные */
        len = rnd() % (cap < 40 ? cap : 40);
        for (size_t i = 0; i < len; i++) out[i] = (char)(1 + rnd() % 255);
        out[len] = '\0';
        return;
    }
    /* mode 3: длинная строка из повторяющегося многобайтового символа */
    {
        const char *u = seeds[rnd() % n];
        size_t ul = strlen(u);
        len = 0;
        if (ul == 0) { out[0] = '\0'; return; }
        while (len + ul < cap - 1) { memcpy(out + len, u, ul); len += ul; }
        out[len] = '\0';
    }
}


/* Проверка «строка является валидным UTF-8». Нужна, чтобы не требовать
 * границ символов от заведомо битого ввода (имена файлов из zip с latin-1,
 * обрезанные последовательности) — там границ просто не существует. */
static int valid_utf8(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        int n;
        if (*p < 0x80) { p++; continue; }
        if ((*p & 0xE0) == 0xC0) n = 1;
        else if ((*p & 0xF0) == 0xE0) n = 2;
        else if ((*p & 0xF8) == 0xF0) n = 3;
        else return 0;
        if ((p[1] & 0xC0) != 0x80) return 0;
        for (int k = 1; k <= n; k++)
            if ((p[k] & 0xC0) != 0x80) return 0;
        p += n + 1;
    }
    return 1;
}

/* ── 1. UTF-8: utf8_strlen / utf8_prefix_bytes / build_padded ── */
static void fuzz_utf8(void)
{
    char s[256], out[128];
    for (int it = 0; it < 300000; it++) {
        gen_string(s, sizeof s);
        (void)utf8_strlen(s);

        int maxc = (int)(rnd() % 300) - 20;      /* включает 0 и отрицательные */
        size_t b = utf8_prefix_bytes(s, maxc);
        if (b > strlen(s)) {
            printf("FAIL utf8_prefix_bytes: %zu > len %zu (maxc=%d)\n",
                   b, strlen(s), maxc);
            abort();
        }
        /* ВАЖНО: b указывает на ГРАНИЦУ СИМВОЛА, а не «на следующий символ».
         * Разрезание — это когда байт b является ПРОДОЛЖЕНИЕМ (10xxxxxx)
         * и при этом он принадлежит символу, начатому ранее b.
         * Если s начинается с orphan-continuation (битый UTF-8), b==0
         * и s[0] & 0xC0 == 0x80 — это НЕ разрезание, функция вернула
         * корректную границу (начало строки). */
        if (valid_utf8(s) && b > 0 && b < strlen(s)) {
            unsigned char c = (unsigned char)s[b];
            if ((c & 0xC0) == 0x80) {
                printf("FAIL utf8_prefix_bytes разрезал UTF-8 при maxc=%d\n", maxc);
                printf("  строка (%zu):", strlen(s));
                for (size_t k = 0; k < strlen(s) && k < 40; k++)
                    printf(" %02X", (unsigned char)s[k]);
                printf("\n  b=%zu, байт в b=0x%02X, prev=0x%02X\n",
                       b, (unsigned char)s[b],
                       b > 0 ? (unsigned char)s[b-1] : 0);
                abort();
            }
        }

        int w = (int)(rnd() % 200) - 10;
        int right = (int)(rnd() % 2);
        build_padded(out, sizeof out, s, w, right);
        if (strlen(out) >= sizeof out) {
            printf("FAIL build_padded переполнил буфер\n");
            abort();
        }
    }
    printf("  utf8          : ok (300k)\n");
}

/* ── 2. Пути: make_path / path_parent / path_is_inside / name_ok ── */
/* utf8_truncate — новый код аудита: обрезка обязана оставаться валидным
 * UTF-8 и никогда не выходить за dstsz (включая dstsz=0 и dstsz=1). */
static void fuzz_utf8_truncate(void)
{
    char src[256];
    for (int it = 0; it < 200000; it++) {
        size_t cap = (rnd() % 40);            /* 0..39, включая 0 и 1 */
        char dst[40];
        size_t n;
        gen_string(src, sizeof src);
        memset(dst, 0xAA, sizeof dst);        /* канарейка */
        n = utf8_truncate(dst, cap, src);
        if (n >= cap && cap > 0) {
            printf("FAIL utf8_truncate: n=%zu при cap=%zu\n", n, cap);
            abort();
        }
        if (cap == 0) continue;
        if (strlen(dst) != n) {
            printf("FAIL utf8_truncate: strlen=%zu, вернул %zu\n", strlen(dst), n);
            abort();
        }
        /* за пределами cap ничего не тронуто */
        for (size_t k = cap; k < sizeof dst; k++) {
            if ((unsigned char)dst[k] != 0xAA) {
                printf("FAIL utf8_truncate: запись за cap (k=%zu)\n", k);
                abort();
            }
        }
        /* результат — префикс источника */
        if (strncmp(dst, src, n) != 0) {
            printf("FAIL utf8_truncate: результат не префикс источника\n");
            abort();
        }
        /* если источник валиден и влезает целиком — копия целиком */
        if (valid_utf8(src) && strlen(src) < cap && strcmp(dst, src) != 0) {
            printf("FAIL utf8_truncate: короткая строка обрезана\n");
            abort();
        }
        /* обрезка валидного UTF-8 не должна давать висячий continuation */
        if (valid_utf8(src) && n > 0 && n < strlen(src)) {
            unsigned char c = (unsigned char)dst[n - 1];
            if (c >= 0xC0) {
                /* последний скопированный байт — ведущий, значит символ
                 * разрезан: у него нет продолжений */
                printf("FAIL utf8_truncate: разрезал символ (n=%zu, cap=%zu, "
                       "последний байт 0x%02X)\n", n, cap, c);
                abort();
            }
        }
    }
    printf("  utf8_truncate : ok (200k)\n");
}

static void fuzz_paths(void)
{
    char s[512], out[FM_PATH_MAX], out2[64];
    (void)out2;
    for (int it = 0; it < 300000; it++) {
        gen_string(s, sizeof s);

        int rc = make_path(out, sizeof out, s, s);
        if (rc == 0 && strnlen(out, sizeof out) >= sizeof out) {
            printf("FAIL make_path: нет NUL\n");
            abort();
        }
        if (rc == 0) {
            /* результат не должен выходить за буфер */
            size_t l = strlen(out);
            if (l >= sizeof out) { printf("FAIL make_path overflow\n"); abort();
            }
        }
        /* Маленький буфер: make_path ОБЯЗАН вернуть -1 и НЕ писать ЗА
         * границу. Запись ВНУТРИ буфера при ошибке допустима (контракт
         * функции этого не запрещает), поэтому проверяем именно выход
         * за пределы — он был бы виден как порча canary сразу после буфера. */
        {
            struct { char buf[64]; char canary[16]; } probe;
            memset(&probe, 0xAA, sizeof probe);
            (void)make_path(probe.buf, sizeof probe.buf, s, s);
            for (size_t k = 0; k < sizeof probe.canary; k++) {
                if (probe.canary[k] != (char)0xAA) {
                    printf("FAIL make_path писал ЗА границу буфера\n");
                    abort();
                }
            }
        }

        (void)path_parent(out, sizeof out, s);
        (void)path_is_inside(s, s);
        (void)path_is_inside(out, s);
        (void)name_ok(s);
        (void)path_display(out, sizeof out, s);
        (void)utf8_strlen(s);
    }
    printf("  paths         : ok (300k)\n");
}

/* ── 3. Сниффер бинарности (exec.c) ── */
static void fuzz_sniffer(void)
{
    char path[] = "/tmp/fz_sniff.bin";
    unsigned char buf[600];
    for (int it = 0; it < 4000; it++) {
        size_t n = rnd() % sizeof buf;
        for (size_t i = 0; i < n; i++) {
            unsigned m = rnd() % 8;
            if (m == 0) buf[i] = 0;
            else if (m == 1) buf[i] = (unsigned char)(rnd() % 32);
            else if (m == 2) buf[i] = 0x7F;
            else buf[i] = (unsigned char)(32 + rnd() % 200);
        }
        FILE *f = fopen(path, "wb");
        if (!f) { printf("skip\n"); return; }
        fwrite(buf, 1, n, f);
        fclose(f);
        (void)fm_content_is_binary(path);
        (void)fm_content_is_binary(path);
    }
    unlink(path);
    printf("  sniffer       : ok (4k)\n");
}

/* ── 4. Токенизатор команд ── */
static void fuzz_tokenize(void)
{
    char s[300], *argv[MAX_ARGS];
    for (int it = 0; it < 20000; it++) {
        gen_string(s, sizeof s);
        int n = tokenize_cmd(s, argv, MAX_ARGS);
        if (n < 0 || n >= MAX_ARGS) { printf("FAIL tokenize: n=%d\n", n); abort(); }
        if (n > 0 && argv[n] != NULL) { printf("FAIL tokenize: нет NULL-терминатора\n"); abort(); }
        free_argv(argv, n);
    }
    printf("  tokenize_cmd  : ok (20k)\n");
}

/* ── 5. format_size / format_mode: граничные значения ── */
static void fuzz_format(void)
{
    char out[64];
    off_t vals[] = { 0, 1, -1, (off_t)LLONG_MAX, (off_t)LLONG_MIN + 1,
                     1024, 1024*1024, 1024LL*1024*1024*1024, -1024 };
    for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        format_size(out, sizeof out, vals[i]);
        if (strnlen(out, sizeof out) >= sizeof out) { printf("FAIL format_size\n"); abort(); }
    }
    for (mode_t m = 0; m < 010000; m += 0111) {
        format_mode(out, sizeof out, m);
        if (strlen(out) > 10) { printf("FAIL format_mode\n"); abort(); }
    }
    /* маленький буфер */
    char tiny[4];
    format_mode(tiny, sizeof tiny, 0644);
    if (tiny[0] != '\0') { printf("FAIL format_mode не обнулил малый буфер\n"); abort(); }
    printf("  format_*      : ok\n");
}

int main(void)
{
    setlocale(LC_ALL, "C.UTF-8");
    setvbuf(stdout, NULL, _IONBF, 0);   /* чтобы отчёт был виден при abort() */
    printf("фаззинг (ASan+UBSan):\n");
    fuzz_utf8();
    fuzz_utf8_truncate();
    fuzz_paths();
    fuzz_sniffer();
    fuzz_tokenize();
    fuzz_format();
    printf("все цели: без падений и санитайзер-ошибок\n");
    return 0;
}
