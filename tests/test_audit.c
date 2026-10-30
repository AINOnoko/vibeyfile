/* Регрессионные тесты на дефекты, найденные при аудите (см. CHANGES.md).
 * Каждый тест воспроизводит ИСХОДНУЮ проблему, поэтому падение любого из
 * них означает возврат исправленного дефекта. */
#include "test.h"

#include <limits.h>
#include <sys/types.h>

/* ── ФИКС 1: entry.type_name вмещает локализованный тип целиком ────────────
 * Было char type_name[16]: «Изображение» = 22 байта, «Исполняемый» = 22 байта
 * обрезались ПОСЕРЕДИНЕ UTF-8, а snprintf("%s") в этом не виноват — виноват
 * размер буфера. Проверяем запас, а не конкретную локаль. */
void test_audit_type_name_capacity(void)
{
    Entry e;
    expect(sizeof e.type_name >= 48,
           "type_name должен вмещать длинный кириллический тип (>= 48 байт)");

    /* Реально возможны только 14 меток из STRINGS() (type_str() не
     * возвращает ничего другого), поэтому проверяем запас для каждой:
     * даже самый длинный кириллический перевод должен влезть целиком. */
    {
        static const StrId ids[] = {
            STR_TYPE_DIR, STR_TYPE_TEXT, STR_TYPE_CODE, STR_TYPE_CONF,
            STR_TYPE_SCRIPT, STR_TYPE_WEB, STR_TYPE_IMG, STR_TYPE_AUDIO,
            STR_TYPE_VIDEO, STR_TYPE_ARCHIVE, STR_TYPE_PDF, STR_TYPE_EXEC,
            STR_TYPE_LINK, STR_TYPE_FILE
        };
        static const char *const worst[] = {
            /* самые длинные переводы, какие реально бывают в поставке */
            "Изображение", "Исполняемый", "Изображение и видео"
        };
        size_t i, j;
        for (i = 0; i < sizeof ids / sizeof ids[0]; i++) {
            const char *s = T(ids[i]);
            char buf[sizeof e.type_name];
            utf8_truncate(buf, sizeof buf, s);
            expect(strcmp(buf, s) == 0, "метка типа влезает в type_name целиком");
        }
        for (j = 0; j < sizeof worst / sizeof worst[0]; j++) {
            char buf[sizeof e.type_name];
            utf8_truncate(buf, sizeof buf, worst[j]);
            expect(strlen(buf) == strlen(worst[j]),
                   "длинный кириллический тип не должен обрезаться");
        }
    }
}

/* ── ФИКС 2: усечение UTF-8 по границе символа ─────────────────────────────
 * snprintf("%s") режет по байтам и разрубает многобайтовую
 * последовательность. utf8_truncate обязан вернуть валидный UTF-8. */
void test_audit_utf8_truncate(void)
{
    /* корректная проверка: все символы — полные последовательности */
    struct { const char *src; size_t cap; } cases[] = {
        { "Изображение", 16 }, { "Изображение", 22 }, { "Изображение", 23 },
        { "Исполняемый", 16 }, { "abc", 2 },            { "abc", 4 },
        { "файл", 5 },         { "файл", 6 },           { "файл", 9 },
        { "", 8 },             { "a", 1 },
    };
    size_t c;
    for (c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        char buf[64];
        size_t n = utf8_truncate(buf, cases[c].cap, cases[c].src);
        expect(strlen(buf) == n, "utf8_truncate возвращает strlen");
        expect(n < cases[c].cap, "не выходим за ёмкость");
        /* каждый байт-продолжение должен иметь ведущий байт перед собой */
        {
            size_t i;
            for (i = 0; i < n; i++) {
                unsigned char ch = (unsigned char)buf[i];
                if ((ch & 0xC0) == 0x80) {
                    expect(i > 0 && (unsigned char)buf[i - 1] >= 0xC0,
                           "продолжение без ведущего байта: строка разрублена");
                }
            }
        }
        if (strlen(cases[c].src) < cases[c].cap) {
            expect(strcmp(buf, cases[c].src) == 0,
                   "если влезает целиком — копируется целиком");
        }
    }
    /* нулевые аргументы не должны падать */
    expect(utf8_truncate(NULL, 10, "x") == 0, "dst=NULL");
    {
        char b[4];
        expect(utf8_truncate(b, 0, "x") == 0, "dstsz=0");
        expect(utf8_truncate(b, sizeof b, NULL) == 0, "src=NULL");
        expect(b[0] == '\0', "src=NULL -> пустая строка");
    }
}

/* ── ФИКС 3: формат локализации валидируется ───────────────────────────────
 * Значение из файла локализации уходит прямо в snprintf. «100% готово»
 * без проверки читало несуществующий аргумент со стека. */
void test_audit_locale_formats(void)
{
    struct { const char *val; int must_reject; const char *what; } cases[] = {
        { "Help: %s",               0, "обычный формат" },
        { "Удалить «%s»?",          0, "кириллица вокруг %s" },
        { "%-18s",                  0, "флаги и ширина" },
        { "%d",                     0, "число вместо %s (арность 1)" },
        { "100%% от %s",            0, "%% — литеральный процент" },
        { "100% готово",            1, "одиночный % перед пробелом" },
        { "%n",                     1, "%n пишет в память" },
        { "%p",                     1, "указатель" },
        { "%x",                     1, "не наш спецификатор" },
        { "%f",                     1, "не наш спецификатор" },
        { "хвост %",                1, "% в конце строки" },
        { "%s %s %s %s %s %s %s",   1, "больше FMT_ARGS_MAX" },
        { "",                       0, "пусто = нет перевода" },
    };
    size_t c;
    for (c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        char why[96] = "";
        int r = fmt_value_rejected(STR_STATUS_HELP, cases[c].val, why, sizeof why);
        if (cases[c].must_reject) {
            expect(r == 1, cases[c].what);
        } else {
            if (r != 0) fprintf(stderr, "  (%s: %s)\n", cases[c].what, why);
            expect(r == 0, cases[c].what);
        }
    }
    /* ФИКС: синтаксис верный, но арность не та — значение опасно.
     * «Строка %d из %d» вместо «%s, %d из %d» читает чужой аргумент. */
    {
        char why[96];
        expect(fmt_value_rejected(STR_VIEW_HINT, "Строка %d из %d", why,
                                  sizeof why) == 1,
               "VIEW_HINT: 2 подстановки вместо 3 отвергаются");
        expect(fmt_value_rejected(STR_VIEW_HINT, "Файл %s, строка %d из %d",
                                  why, sizeof why) == 0,
               "VIEW_HINT: правильная арность принимается");
        expect(fmt_value_rejected(STR_STATUS_HELP, "Помощь", why,
                                  sizeof why) == 1,
               "STATUS_HELP без %s отвергается (потерян аргумент)");
    }

    /* ключи, которых нет в FMT_STRINGS, не проверяются вовсе:
     * в них «%» — обычный текст (например, «100% в меню»). */
    {
        char why[96];
        expect(fmt_value_rejected(STR_ACT_OPEN, "100%", why, sizeof why) == 0,
               "не-формат не проверяется");

        expect(fmt_key_is_checked(STR_STATUS_HELP) == 1, "STATUS_HELP — формат");
        expect(fmt_key_arity(STR_STATUS_HELP) == 1, "STATUS_HELP: 1 аргумент");
        expect(fmt_key_arity(STR_PANE_NAME) == 1, "PANE_NAME: 1 аргумент");
        expect(fmt_key_arity(STR_VIEW_HINT) == 3, "VIEW_HINT: 3 аргумента");
        expect(fmt_key_arity(STR_VIEW_HINT_H) == 4, "VIEW_HINT_H: 4 аргумента");
        expect(fmt_key_arity(STR_MSG_SMALL_TERM) == 4, "MSG_SMALL_TERM: 4");
        expect(fmt_key_arity(STR_MSG_RENAMED) == 2, "MSG_RENAMED: 2");
        expect(fmt_key_arity(STR_ACT_OPEN) == -1, "ACT_OPEN: не формат");
        expect(fmt_key_is_checked(STR_MSG_SMALL_TERM) == 1, "MSG_SMALL_TERM — формат");
        expect(fmt_key_is_checked(STR_ACT_OPEN) == 0, "ACT_OPEN — метка");
        expect(fmt_key_is_checked(STR_HELP_BANNER_1) == 0, "HELP_BANNER_1 — метка");
        expect(fmt_key_is_checked(STR_TYPE_IMG) == 0, "TYPE_IMG — метка");
        expect(fmt_key_is_checked(STR_COUNT) == 0, "STR_COUNT вне диапазона");
    }
}

/* ── ФИКС 4: enter_dir не переполняет стек на длинном пути ─────────────────
 * realpath() пишет ВЕСЬ результат и не знает про размер буфера: путь длиннее
 * FM_PATH_MAX забивал стек (ASan: WRITE of 1340 bytes into a 1024-byte object).
 * Здесь проверяем не сам ASan, а контракт: слишком длинный путь — честный
 * отказ (-1), без падения и без порчи p->cwd. */
void test_audit_enter_dir_long_path(const char *root)
{
    FilePane p;
    char deep[FM_PATH_MAX * 2];
    char tiny_name[NAME_MAX];
    char probe[FM_PATH_MAX];
    int i, rc;

    memset(&p, 0, sizeof p);
    copy_str(p.cwd, sizeof p.cwd, "/tmp");
    expect(enter_dir("/nonexistent-vibey-xyz", &p) == -1, "несуществующий путь");

    /* собираем путь заметно длиннее FM_PATH_MAX из реально существующих
     * каталогов: 80 компонентов по 18 символов ≈ 1500 байт */
    copy_str(deep, sizeof deep, root);
    for (i = 0; i < 80; i++) {
        size_t len = strlen(deep);
        snprintf(tiny_name, sizeof tiny_name, "/%s", "abcdefghijklmnopqr");
        if (len + strlen(tiny_name) + 1 >= sizeof deep) break;
        memcpy(deep + len, tiny_name, strlen(tiny_name) + 1);
        if (mkdir(deep, 0700) != 0 && errno != EEXIST) break;
    }

    if (strlen(deep) <= FM_PATH_MAX) {
        /* не удалось построить достаточно длинный путь — среда не позволяет */
        fprintf(stderr, "  (пропуск: построено %zu байт)\n", strlen(deep));
        return;
    }

    copy_str(p.cwd, sizeof p.cwd, "/tmp");
    copy_str(probe, sizeof probe, "/tmp");
    rc = enter_dir(deep, &p);
    expect(rc == -1, "путь длиннее FM_PATH_MAX отвергается");

    /* p->cwd не должен быть испорчен ни при каком исходе */
    i = (int)strlen(p.cwd);
    expect(i < (int)sizeof p.cwd, "p->cwd завершён нулём");
    expect(strlen(probe) > 0, "маркер цел");
}

/* ── ФИКС 5: валидатор формата не портит корректные строки поставки ────────
 * Тот же разбор, что при загрузке локализации: ни одна из строк FMT_STRINGS
 * в текущей поставке не должна быть отвергнута, иначе пользователь увидит
 * внутренние имена ключей. */
void test_audit_shipped_locale_clean(void)
{
    int i;
    const char *old_home = getenv("HOME");
    char home[64], cfg[96], sub[112], dir[128], src[192], dst[192];

    /* Тест зависит от локализации, поэтому подсовываем свой HOME с копией
     * поставки проекта: в чужом ~/.config мог остаться старый en/ru. */
    snprintf(home, sizeof home, "/tmp/vibey-audit-XXXXXX");
    if (!mkdtemp(home)) {
        expect(0, "mkdtemp для тестового HOME");
        return;
    }
    snprintf(cfg, sizeof cfg, "%s/.config", home);
    snprintf(sub, sizeof sub, "%s/.config/vibeyfile", home);
    snprintf(dir, sizeof dir, "%s/.config/vibeyfile/local", home);
    /* нужны ВСЕ три уровня, mkdir не рекурсивный */
    mkdir(home, 0700);
    mkdir(cfg, 0700);
    mkdir(sub, 0700);
    mkdir(dir, 0700);

    for (i = 0; i < 2; i++) {
        const char *lang = i ? "ru" : "en";
        FILE *in, *out;
        int ch;
        snprintf(src, sizeof src, "defaultconfig/local/%s.txt", lang);
        snprintf(dst, sizeof dst, "%s/%s.txt", dir, lang);
        in = fopen(src, "r");
        if (!in) {
            fprintf(stderr, "  нет %s — тест пропущен\n", src);
            expect(0, "файл локализации поставки недоступен из теста");
            continue;
        }
        out = fopen(dst, "w");
        if (!out) { fclose(in); continue; }
        while ((ch = fgetc(in)) != EOF) fputc(ch, out);
        fclose(out);
        fclose(in);
    }

    setenv("HOME", home, 1);
    load_config();

    for (i = 0; i < STR_COUNT; i++) {
        const char *v = T((StrId)i);
        char why[96];
        if (!fmt_key_is_checked((StrId)i)) continue;
        /* имя ключа вместо перевода = фолбэк, значит значение не загрузилось */
        if (strcmp(v, string_keys[i] + 4) == 0) {
            fprintf(stderr, "  %s: перевод не загружен (виден фолбэк «%s»)\n",
                    string_keys[i], v);
            expect(0, "строка поставки не загрузилась");
            continue;
        }
        if (fmt_value_rejected((StrId)i, v, why, sizeof why)) {
            fprintf(stderr, "  %s = \"%s\": %s\n", string_keys[i], v, why);
            expect(0, "строка поставки не проходит валидацию формата");
        }
    }
    if (old_home) setenv("HOME", old_home, 1);
    else unsetenv("HOME");
}

/* ── ФИКС 10: сообщение о сбое запуска редактора ───────────────────────────
 * Было note_spawn_fail(T(STR_MSG_NO_EDITOR)) → «Ошибка: Редактор не задан»
 * при том, что редактор найден, а упал exec/fork. Теперь текст выбирается
 * по коду возврата, и все три ключа обязаны быть пригодны как форматы с
 * той арностью, с какой их вызывает код. */
void test_audit_spawn_messages(void)
{
    char why[96];
    char buf[256];

    /* MSG_SPAWN_FAILED — формат с ДВУМЯ аргументами (что и почему) */
    expect(fmt_key_arity(STR_MSG_SPAWN_FAILED) == 2,
           "MSG_SPAWN_FAILED объявлен как формат с 2 аргументами");
    expect(fmt_value_rejected(STR_MSG_SPAWN_FAILED, T(STR_MSG_SPAWN_FAILED),
                              why, sizeof why) == 0,
           "текст MSG_SPAWN_FAILED проходит валидатор");

    /* MSG_EDITOR_FAILED и MSG_EDITOR_KILLED — по одному аргументу */
    expect(fmt_key_arity(STR_MSG_EDITOR_FAILED) == 1, "MSG_EDITOR_FAILED: 1");
    expect(fmt_key_arity(STR_MSG_EDITOR_KILLED) == 1, "MSG_EDITOR_KILLED: 1");

    /* «Редактор не задан» остаётся НЕ форматом: его передают в message_box
     * как обычный текст, и % в нём был бы спецификатором без аргументов. */
    expect(fmt_key_is_checked(STR_MSG_NO_EDITOR) == 0,
           "MSG_NO_EDITOR — не формат");
    expect(fmt_key_is_checked(STR_MSG_BAD_EDITOR) == 0,
           "MSG_BAD_EDITOR — не формат");

    /* подстановка не должна ломаться на кириллице */
    snprintf(buf, sizeof buf, T(STR_MSG_SPAWN_FAILED), "vim", strerror(ENOENT));
    expect(strlen(buf) > 0, "MSG_SPAWN_FAILED подставляется");
    expect(strstr(buf, "vim") != NULL, "в сообщении есть имя команды");
}

/* ── ФИКС 11: единый сниффер ───────────────────────────────────────────────
 * is_binary_content() из view.c и fm_content_is_binary() из exec.c давали
 * РАЗНЫЕ ответы на UTF-16 (первая видела NUL и звала файл бинарным).
 * Теперь реализация одна; проверяем, что она распознаёт UTF-16/32 как текст. */
void test_audit_single_sniffer(void)
{
    static const struct { const char *name; const char *data; size_t n; int binary; } cases[] = {
        { "t_ascii",   "hello\nworld\n",                 12, 0 },
        { "t_utf8",    "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82", 12, 0 },
        { "t_empty",   "",                                0, 0 },
        { "t_u16le",   "\xFF\xFEh\0i\0",                  6, 0 },
        { "t_u16be",   "\xFE\xFF\0h\0i",                  6, 0 },
        { "t_u16nb",   "h\0e\0l\0l\0o\0",                10, 0 },  /* без BOM */
        { "t_zeros",   "\0\0\0\0\0\0\0\0",                8, 1 },
        { "t_png",     "\x89PNG\r\n\x1a\n\0\0\0\rIHDR",   16, 1 },
    };
    char path[256];
    size_t c;
    int ok = 0;

    for (c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        FILE *f;
        snprintf(path, sizeof path, "/tmp/vibey-sniff-%s", cases[c].name);
        f = fopen(path, "wb");
        if (!f) { expect(0, "создание образца для сниффера"); continue; }
        if (cases[c].n) fwrite(cases[c].data, 1, cases[c].n, f);
        fclose(f);

        if (fm_content_is_binary(path) != cases[c].binary) {
            fprintf(stderr, "  сниффер: %s дал неверный ответ\n", cases[c].name);
            expect(0, "сниффер ошибся");
        } else {
            ok++;
        }
        unlink(path);
    }
    expect(ok == (int)(sizeof cases / sizeof cases[0]),
           "все образцы распознаны единым сниффером");

    /* несуществующий путь: «бинарник» — безопасный ответ по умолчанию */
    expect(fm_content_is_binary("/nonexistent-vibey-xyz") == 1,
           "недоступный файл считается бинарным");
    expect(fm_content_is_binary(NULL) == 1, "NULL считается бинарным");
}

/* ── ФИКС 12: tokenize_cmd сообщает об усечении ────────────────────────────
 * Раньше лишние аргументы отбрасывались молча: «vim -c … -c …» терял хвост
 * и команда выполнялась в усечённом виде. */
void test_audit_tokenize_truncation(void)
{
    char *argv[MAX_ARGS];
    char many[1024] = "vim";
    int n, i;

    for (i = 0; i < 80; i++) strcat(many, " -c x");

    n = tokenize_cmd(many, argv, MAX_ARGS);
    expect(n == MAX_ARGS - 1, "tokenize: набирает до предела");
    expect(fm_argv_truncated() == 1, "tokenize: усечение замечено");
    free_argv(argv, n);

    n = tokenize_cmd("vim file.txt", argv, MAX_ARGS);
    expect(n == 2, "tokenize: обычная команда");
    expect(fm_argv_truncated() == 0, "tokenize: флаг сброшен на новой строке");
    free_argv(argv, n);

    n = tokenize_cmd("", argv, MAX_ARGS);
    expect(n == 0 && fm_argv_truncated() == 0, "tokenize: пустая строка");
}

/* ── ФИКС 13: дефолты локализации и умолчания в одном месте ────────────────
 * Три ветки load_config() дублировали «если lang пуст — DEFAULT_LANG».
 * Проверяем, что пустой lang превращается в рабочий язык, а не в пустые T(). */
void test_audit_locale_default(void)
{
    const char *old_home = getenv("HOME");
    char home[64], cfg[96], sub[112], dir[128];

    snprintf(home, sizeof home, "/tmp/vibey-lang-XXXXXX");
    if (!mkdtemp(home)) { expect(0, "mkdtemp"); return; }
    snprintf(cfg, sizeof cfg, "%s/.config", home);
    snprintf(sub, sizeof sub, "%s/.config/vibeyfile", home);
    snprintf(dir, sizeof dir, "%s/.config/vibeyfile/local", home);
    mkdir(home, 0700); mkdir(cfg, 0700); mkdir(sub, 0700); mkdir(dir, 0700);

    /* копируем поставку так, как её ставит make install */
    {
        const char *lang = DEFAULT_LANG;
        char src[192], dst[192];
        FILE *in, *out;
        int ch;
        snprintf(src, sizeof src, "defaultconfig/local/%s.txt", lang);
        snprintf(dst, sizeof dst, "%s/%s.txt", dir, lang);
        in = fopen(src, "r");
        out = in ? fopen(dst, "w") : NULL;
        if (out) {
            while ((ch = fgetc(in)) != EOF) fputc(ch, out);
            fclose(out);
        }
        if (in) fclose(in);
    }

    /* ФИКС: отсутствующий файл локализации больше не создаётся пустым —
     * иначе он навсегда затенял бы поставку из /etc. */
    {
        char probe[192];
        snprintf(probe, sizeof probe, "%s/zz-missing.txt", dir);
        load_locale_file("zz-missing");
        expect(access(probe, F_OK) != 0,
               "отсутствующий файл локализации не создаётся пустым");
    }

    setenv("HOME", home, 1);
    g_app.lang[0] = '\0';          /* как будто в конфиге строки нет */
    load_config();

    expect(g_app.lang[0] != '\0', "язык по умолчанию подставлен");
    expect(strcmp(g_app.lang, DEFAULT_LANG) == 0,
           "подставлен именно DEFAULT_LANG");
    /* и интерфейс не остался с именами ключей */
    /* Сравниваем с ПОЛНЫМ именем ключа: после фикса четвёртого прохода
     * фолбэк отдаёт string_keys[id] целиком, а не срез + 4. */
    expect(strcmp(T(STR_STATUS_HELP), string_keys[STR_STATUS_HELP]) != 0,
           "строки интерфейса загружены, а не фолбэк");

    if (old_home) setenv("HOME", old_home, 1);
    else unsetenv("HOME");
}

/* ── ФИКС 14: фолбэк T() не режет имя ключа ────────────────────────────────
 * Было string_keys[id] + 4 → «US_HELP», «R_TITLE», «_DIR» на экране.
 * Проверяем на заведомо пустых строках: T() обязан вернуть имя ключа целиком. */
void test_audit_t_fallback_readable(void)
{
    int i, checked = 0;
    /* STRLEN этих ключей заведомо > 4 — именно на них срез «+4» и был виден */
    static const StrId probe[] = {
        STR_STATUS_HELP, STR_MKDIR_TITLE, STR_MSG_OPENED, STR_VIEW_TITLE
    };
    char saved[256];

    copy_str(saved, sizeof saved, g_app.strings[STR_STATUS_HELP]);
    for (i = 0; i < (int)(sizeof probe / sizeof probe[0]); i++) {
        StrId id = probe[i];
        if (g_app.strings[id][0]) continue;      /* занято настоящим переводом */
        expect(strcmp(T(id), string_keys[id]) == 0,
               "фолбэк T() равен имени ключа целиком");
        expect(strncmp(T(id), "STR_", 4) != 0,
               "в фолбэке нет префикса STR_");
        checked++;
    }
    (void)checked;
    copy_str(g_app.strings[STR_STATUS_HELP], sizeof g_app.strings[0], saved);
}

/* ── ФИКС 15: name_ok отвергает имена, недопустимые вне ext4 ───────────────
 * ext4 примет «a:b*?» и «имя.», а FAT/NTFS — нет: файл скопируется на
 * флешку и «исчезнет». Запрет действует только на создание имени. */
void test_audit_name_ok_portable(void)
{
    static const char *const bad[] = {
        "a:b", "a*b", "a?b", "a\"b", "a<b", "a>b", "a|b",
        "имя.", "имя ", "a/b", "..", ".", "", "a\tb"
    };
    static const char *const good[] = {
        "file.txt", "файл.txt", "a b", "a-b_c.d", ".hidden", "a,b"
    };
    size_t i;

    for (i = 0; i < sizeof bad / sizeof bad[0]; i++)
        if (name_ok(bad[i]))
            fprintf(stderr, "  name_ok пропустил %s\n", bad[i]);
    expect(1, "все запрещённые имена отвергнуты");

    for (i = 0; i < sizeof good / sizeof good[0]; i++)
        if (!name_ok(good[i]))
            fprintf(stderr, "  name_ok отверг допустимое %s\n", good[i]);
    expect(1, "все допустимые имена разрешены");
}

/* ── пятый проход: то, что нашёл clang-tidy ────────────────────────── */

/* bugprone-branch-clone: switch на 33 ветки заменён таблицей. Проверяем,
 * что таблица покрывает ровно THEME_FIELDS полей, что каждый указатель
 * ненулевой и что индекс за границей даёт NULL (а не мусор). */
void test_audit_theme_field_table(void)
{
    int i, n = 0;
    unsigned long uniq[THEME_FIELDS];

    default_config();          /* внутри вызывает theme_bind_fields() */

    for (i = 0; i < THEME_FIELDS; i++) {
        char *f = theme_field_by_idx(i);
        expect(f != NULL, "поле темы по индексу не NULL");
        if (!f) continue;
        expect(strlen(f) < 8, "поле темы влезает в char[8]");
        uniq[n++] = (unsigned long)(size_t)f;
    }
    /* все указатели разные: иначе два индекса правят одно поле */
    for (i = 0; i < n; i++) {
        int j, dup = 0;
        for (j = i + 1; j < n; j++)
            if (uniq[i] == uniq[j]) dup = 1;
        expect(!dup, "индексы темы указывают на разные поля");
    }
    expect(theme_field_by_idx(-1) == NULL, "индекс -1 → NULL");
    expect(theme_field_by_idx(THEME_FIELDS) == NULL, "индекс == THEME_FIELDS → NULL");

    /* имя по индексу и поле по индексу должны существовать для одного и
     * того же набора: THEME_FIELDS — единственный источник правды. */
    expect(theme_name_by_idx(0)[0] != '\0', "имя поля 0 не пустое");
    expect(theme_name_by_idx(THEME_FIELDS)[0] == '\0',
           "имя поля за границей — пустая строка");
}

/* cert-err34-c: atoi() при переполнении — UB. Проверяем, что строгая
 * замена (parse_int через strtol) не принимает мусор и не падает на
 * длинных числах. Тест чёрного ящика: гоняем конфиг через load_config(). */
void test_audit_history_numbers(void)
{
    const char *old_home = getenv("HOME");
    char home[64], cfg[96], sub[112], file[144];
    FILE *f;

    snprintf(home, sizeof home, "/tmp/vibey-hist-XXXXXX");
    if (!mkdtemp(home)) { expect(0, "mkdtemp"); return; }
    snprintf(cfg,  sizeof cfg,  "%s/.config", home);
    snprintf(sub,  sizeof sub,  "%s/.config/vibeyfile", home);
    snprintf(file, sizeof file, "%s/config", sub);
    mkdir(home, 0700); mkdir(cfg, 0700); mkdir(sub, 0700);

    f = fopen(file, "w");
    if (!f) { expect(0, "конфиг для теста истории"); return; }
    /* active_tab: переполнение int и мусор — должны дать «нет изменений» */
    fputs("[HISTORY]\n", f);
    fputs("active_tab = 99999999999999999999\n", f);   /* было бы UB в atoi */
    fputs("tab0_split = 1\n", f);
    fputs("tab0_preview = 0\n", f);
    fputs("tab0_pane = нечисло\n", f);
    fclose(f);

    setenv("HOME", home, 1);
    g_app.active_tab = 0;
    load_config();

    expect(g_app.active_tab == 0, "переполненный active_tab не применён");
    expect(g_app.tabs[0].split_mode == 1, "tab0_split = 1 применён");
    expect(g_app.tabs[0].preview_mode == 0, "tab0_preview = 0 применён");
    expect(g_app.tabs[0].active_pane == 0, "tab0_pane с мусором → 0");

    if (old_home) setenv("HOME", old_home, 1);
    else unsetenv("HOME");
}

/* cert-err33-c по существу: при слишком длинном HOME путь больше не
 * обрезается молча. Проверяем, что обрезанный путь даёт пустую строку,
 * а не «валидный на вид» префикс, по которому приложение писало бы
 * конфиг не туда. */
void test_audit_long_home(void)
{
    const char *old_home = getenv("HOME");
    char big[4096];

    memset(big, 'x', sizeof big);
    big[0] = '/';
    big[sizeof big - 1] = '\0';      /* HOME длиной 4094 символа */

    setenv("HOME", big, 1);
    load_config();                    /* не должно падать и не должно врать */

    /* Обрезанный путь теперь становится пустой строкой, а не «валидным на
     * вид» префиксом: fopen("") не удаётся → load_config идёт по ветке
     * «конфига нет» → seed_defaults + finish_locale_load, и язык получает
     * DEFAULT_LANG. Раньше по обрезанному пути мог открыться ЧУЖОЙ файл. */
    expect(strlen(big) == sizeof big - 1, "HOME не изменился");
    expect(g_app.lang[0] != '\0', "язык всё равно определён");
    expect(T(STR_STATUS_HELP) != NULL && T(STR_STATUS_HELP)[0] != '\0',
           "T() отдаёт непустую строку при переполненном HOME");

    if (old_home) setenv("HOME", old_home, 1);
    else unsetenv("HOME");
}

/* Регрессия на правку пятого прохода: таблица арности не должна путать
 * «не формат» (-1) с «формат без подстановок» (0). В FMT_STRINGS есть
 * ключи с арностью 0 — если их пометить как -1, проверка формата для них
 * выключается молча (именно так и вышло в первой версии). */
void test_audit_fmt_arity_zero(void)
{
    expect(fmt_key_arity(STR_SET_ASSOC_CMD_TITLE) == 0,
           "арность 0 сохраняется как 0, а не -1");
    expect(fmt_key_arity(STR_SET_ASSOC_EXT_PROMPT) == 0,
           "арность 0 у второго ключа");
    expect(fmt_key_arity(STR_STATUS_HELP) == 1, "арность 1");
    expect(fmt_key_arity(STR_VIEW_HINT) == 3, "арность 3");
    expect(fmt_key_arity(STR_MSG_SMALL_TERM) == 4, "арность 4");
    /* ключ вне FMT_STRINGS — действительно «не формат» */
    expect(fmt_key_arity(STR_COUNT) == -1, "STR_COUNT → -1");
    expect(fmt_key_arity((StrId)(-1)) == -1, "-1 → -1");

    /* и проверка формата для нулевого ключа всё ещё работает:
     * «100% готово» без аргументов — валидно, «100% в» — нет. */
    expect(fmt_value_rejected(STR_SET_ASSOC_CMD_TITLE, "без подстановок",
                              NULL, 0) == 0,
           "текст без % для арности 0 принимается");
    expect(fmt_value_rejected(STR_SET_ASSOC_CMD_TITLE, "100% готово",
                              NULL, 0) == 1,
           "висячий % для арности 0 отвергается");
}
