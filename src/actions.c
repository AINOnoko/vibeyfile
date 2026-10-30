#include "fm.h"

static Entry *selected_entry(FilePane *p)
{
    if (!p || !p->list.items || p->list.count == 0) return NULL;
    if (p->file_sel < 0 || p->file_sel >= (int)p->list.count) return NULL;
    return &p->list.items[p->file_sel];
}

static void reload_cwd(const char *cwd)
{
    int t, pi;
    if (!cwd || !cwd[0]) return;
    for (t = 0; t < NUM_TABS; t++)
        for (pi = 0; pi < 2; pi++) {
            FilePane *q = &g_app.tabs[t].panes[pi];
            if (q->cwd[0] && strcmp(q->cwd, cwd) == 0)
                load_dir(q);
        }
}

static void remember(FilePane *p, const char *name)
{
    if (!p || !name) return;
    copy_str(p->pending_sel, sizeof p->pending_sel, name);
}

static int appendf(char *buf, size_t sz, int pos, const char *fmt, ...)
{
    va_list ap;
    int n;
    if (!buf || sz == 0 || pos < 0 || (size_t)pos >= sz) return -1;
    va_start(ap, fmt);
    n = vsnprintf(buf + pos, sz - (size_t)pos, fmt, ap);
    va_end(ap);
    if (n < 0) return -1;
    if ((size_t)n >= sz - (size_t)pos) return (int)(sz - 1);
    return pos + n;
}

static int is_plain_name(const char *name)
{
    const char *base;
    if (!name || !name[0]) return 0;
    base = strrchr(name, '/');
    base = base ? base + 1 : name;
    if (!strcmp(base, "Makefile") || !strcmp(base, "README")
        || !strcmp(base, "LICENSE") || !strcmp(base, "COPYING")
        || !strcasecmp(base, "dockerfile") || !strcmp(base, "CMakeLists.txt"))
        return 1;
    return strchr(base, '.') == NULL;
}

static int launch_one_assoc(const char *name, char *fullbuf, const char *cmd)
{
    char *argv[MAX_ARGS];
    int n = tokenize_cmd(cmd, argv, MAX_ARGS);
    /* n >= MAX_ARGS - 1 — команда упёрлась в предел; fm_argv_truncated()
     * ловит случай, когда токенов больше, но n меньше предела из-за
     * пустых фрагментов. Оба означают «запускать нельзя». */
    if (n <= 0 || n >= MAX_ARGS - 1 || fm_argv_truncated()) {
        free_argv(argv, n);
        return 0;
    }
    argv[n] = fullbuf;
    argv[n + 1] = NULL;
    spawn_detached_argv(argv);
    free_argv(argv, n);
    snprintf(g_app.last_action, sizeof g_app.last_action,
             T(STR_MSG_OPENED_ASSOC), name, cmd);
    return 1;
}

static int launch_by_assoc(const char *name, char *fullbuf)
{
    const char *dot = strrchr(name, '.');
    int i;
    if (!dot || dot == name || !dot[1]) return 0;
    for (i = 0; i < g_app.assoc_count; i++) {
        if (strcasecmp(g_app.associations[i].ext, dot + 1) != 0) continue;
        if (launch_one_assoc(name, fullbuf, g_app.associations[i].cmd))
            return 1;
    }
    return 0;
}

static void launch_xdg(const char *name, char *fullbuf)
{
    char *argv[] = { "xdg-open", fullbuf, NULL };
    spawn_detached_argv(argv);
    snprintf(g_app.last_action, sizeof g_app.last_action, T(STR_MSG_OPENED), name);
}

void launch_path(const char *name, const char *full)
{
    char fullbuf[FM_PATH_MAX];
    if (!name || !full) return;
    copy_str(fullbuf, sizeof fullbuf, full);

    /* 0. Явная ассоциация из конфига приоритетнее любых эвристик. */
    if (launch_by_assoc(name, fullbuf)) return;

    /* 1. Расширение/имя явно говорит «текст» → встроенный просмотрщик.
     *    Для домашней папки это ~90% файлов, и раньше каждый из них
     *    всё равно доходил до сниффера (open+read+close). */
    if (is_text_file(name) || is_plain_name(name)) {
        view_file(fullbuf);
        return;
    }

    /* 2. Сниффер по первым 512 байтам: open+read+close, БЕЗ fork.
     *    Распознаёт UTF-16/32 с NUL — то, для чего раньше звали `file`. */
    if (!fm_content_is_binary(fullbuf)) {
        view_file(fullbuf);
        return;
    }

    /* 3. Бинарник → xdg-open. `file(1)` больше не вызывается: его вердикт
     *    не изменил бы решение (файл уже признан бинарным по содержимому),
     *    а fork+exec(payload) стоил ~200 КиБ адресного пространства в
     *    дочернем процессе и 2-4 мс на каждый запуск. MIME-запрос остался
     *    только в action_info(), где он действительно информативен. */
    launch_xdg(name, fullbuf);
}

void action_parent(FilePane *p)
{
    char prev[FM_PATH_MAX], parent[FM_PATH_MAX];
    const char *base;
    size_t i;
    int err;
    if (!p || !p->cwd[0] || !strcmp(p->cwd, "/")) return;
    copy_str(prev, sizeof prev, p->cwd);
    if (path_parent(parent, sizeof parent, prev) != 0) return;
    if (enter_dir(parent, p) != 0) {
        err = errno;
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 T(STR_MSG_OPEN_FAILED), strerror(err));
        return;
    }
    base = strrchr(prev, '/');
    base = (base && base[1]) ? base + 1 : prev;
    if (!p->list.items) return;
    for (i = 0; i < p->list.count; i++)
        if (strcmp(p->list.items[i].name, base) == 0) {
            p->file_sel = (int)i;
            break;
        }
}

void action_open(FilePane *p)
{
    Entry *e;
    char full[FM_PATH_MAX];
    int err;
    e = selected_entry(p);
    if (!e) {
        message_box(T(STR_MSG_ATTENTION), T(STR_MSG_NOTHING_SELECTED));
        return;
    }
    if (e->is_parent) { action_parent(p); return; }
    if (make_path(full, sizeof full, p->cwd, e->name) != 0) {
        message_box(T(STR_MSG_ERROR), T(STR_MSG_PATH_TOO_LONG));
        return;
    }
    if (e->is_dir) {
        if (enter_dir(full, p) != 0) {
            err = errno;
            snprintf(g_app.last_action, sizeof g_app.last_action,
                     T(STR_MSG_OPEN_FAILED), strerror(err));
        }
        return;
    }
    launch_path(e->name, full);
}

void action_view(FilePane *p)
{
    Entry *e;
    char full[FM_PATH_MAX];
    e = selected_entry(p);
    if (!e) {
        message_box(T(STR_MSG_ATTENTION), T(STR_MSG_NOTHING_SELECTED));
        return;
    }
    if (e->is_parent) {
        message_box(T(STR_MSG_ATTENTION), T(STR_MSG_CANT_VIEW_PARENT));
        return;
    }
    if (e->is_dir) {
        message_box(T(STR_MSG_ATTENTION), T(STR_MSG_NOT_REGULAR));
        return;
    }
    if (make_path(full, sizeof full, p->cwd, e->name) != 0) {
        message_box(T(STR_MSG_ERROR), T(STR_MSG_PATH_TOO_LONG));
        return;
    }
    view_file(full);
    snprintf(g_app.last_action, sizeof g_app.last_action,
             T(STR_MSG_VIEWED), e->name);
}

void action_open_as(FilePane *p)
{
    char cmd_buf[256] = "vim";
    Entry *e;
    char target[FM_PATH_MAX];
    char *argv[MAX_ARGS];
    int n, rc;
    e = selected_entry(p);
    if (!e || e->is_parent) return;
    if (!dialog_input(T(STR_OPEN_AS_TITLE), T(STR_OPEN_AS_PROMPT),
                      cmd_buf, sizeof cmd_buf)) return;
    if (!cmd_buf[0]) return;
    if (make_path(target, sizeof target, p->cwd, e->name) != 0) return;

    n = tokenize_cmd(cmd_buf, argv, MAX_ARGS);
    /* ФИКС: раньше при переполнении MAX_ARGS команда молча выполнялась
     * без хвоста аргументов — «vim -c … -c …» терял последние флаги. */
    if (fm_argv_truncated()) {
        free_argv(argv, n);
        message_box(T(STR_MSG_ERROR), T(STR_MSG_BAD_COMMAND));
        return;
    }
    if (n > 0 && n < MAX_ARGS - 1) {
        argv[n] = target;
        argv[n + 1] = NULL;

        rc = spawn_sync(argv[0], argv, 1, p->cwd);
        fm_flush_input();

        if (rc == 0 || rc == -2) {
            snprintf(g_app.last_action, sizeof g_app.last_action,
                     T(STR_MSG_OPENED_ASSOC), e->name, cmd_buf);
        } else {
            snprintf(g_app.last_action, sizeof g_app.last_action,
                     T(STR_MSG_ERROR_STR), cmd_buf);
        }
    } else if (n > 0) {
        message_box(T(STR_MSG_ERROR), T(STR_MSG_BAD_COMMAND));
    }
    free_argv(argv, n);
}

static int editor_usable(const char *dir, const char *name)
{
    char full[FM_PATH_MAX];
    struct stat st;
    int n;
    if (!dir || !dir[0] || !name) return 0;
    n = snprintf(full, sizeof full, "%s/%s", dir, name);
    if (n < 0 || (size_t)n >= sizeof full) return 0;
    if (fstatat(AT_FDCWD, full, &st, 0) != 0) return 0;
    return S_ISREG(st.st_mode) && (st.st_mode & S_IXUSR);
}

/* ЭТАП 3: PATH разбирается по указателям, без strdup.
 * Было: strlen(PATH) + malloc + запись всех байт + strtok_r (который
 * дополнительно ПИШЕТ нули в копию). На каждый вызов action_edit — до
 * 4 вызовов editor_in_path (nvim/vim/vi/nano).
 * Стало: 0 байт кучи, strchr() только читает; PATH из environ не портим.
 */
static int editor_in_path(const char *path, const char *name)
{
    const char *p, *end;
    char dir[FM_PATH_MAX];
    size_t len;
    if (!path || !name) return 0;
    for (p = path; ; p = end + 1) {
        end = strchr(p, ':');
        len = end ? (size_t)(end - p) : strlen(p);
        if (len == 0) {
            /* пустой элемент PATH (":") по POSIX = текущий каталог */
            dir[0] = '.'; dir[1] = '\0';
        } else if (len < sizeof dir) {
            memcpy(dir, p, len);
            dir[len] = '\0';
        } else {
            if (!end) break;          /* элемент длиннее буфера — пропуск */
            continue;
        }
        if (editor_usable(dir, name)) return 1;
        if (!end) break;
    }
    return 0;
}

static const char *find_editor(void)
{
    static const char *editors[] = { "nvim", "vim", "vi", "nano" };
    const char *env_ed = getenv("EDITOR");
    const char *path = getenv("PATH");
    int i;
    if (env_ed && *env_ed) return env_ed;
    if (!path) return NULL;
    for (i = 0; i < 4; i++)
        if (editor_in_path(path, editors[i])) return editors[i];
    return NULL;
}

/* ФИКС: раньше сбой запуска сообщался одним текстом, который передавал
 * вызывающий. В action_edit это был STR_MSG_NO_EDITOR — «Редактор не задан»
 * при том, что редактор как раз найден, а упал exec/fork. Теперь текст
 * выбирается по коду возврата spawn_sync(), а `what` — уточнение, что
 * именно не запустилось:
 *   127  — execvp не нашёл команду (нет такого файла в PATH);
 *   -1   — сбой fork/waitpid в самом менеджере;
 *   -2   — процесс убит сигналом (rc == -2 вызывающие обычно пропускают:
 *          пользователь нажал Ctrl-C в редакторе — это норма, не ошибка). */
static void note_spawn_failed(int rc, const char *what)
{
    if (rc == 127)
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 T(STR_MSG_SPAWN_FAILED), what, strerror(ENOENT));
    else if (rc == -2)
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 T(STR_MSG_EDITOR_KILLED), what);
    else
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 T(STR_MSG_ERROR_STR), what);
}

void action_edit(FilePane *p)
{
    Entry *e;
    const char *ed;
    char full[FM_PATH_MAX], keep[MAX_NAME];
    char *argv[MAX_ARGS];
    struct stat st;
    int n, rc;
    e = selected_entry(p);
    if (!e || e->is_dir || e->is_parent) return;
    if (make_path(full, sizeof full, p->cwd, e->name) != 0) return;
    if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) {
        message_box(T(STR_MSG_ERROR), T(STR_MSG_NOT_REGULAR));
        return;
    }
    ed = find_editor();
    if (!ed) { message_box(T(STR_MSG_ERROR), T(STR_MSG_NO_EDITOR)); return; }
    n = tokenize_cmd(ed, argv, MAX_ARGS);
    if (n <= 0 || n >= MAX_ARGS - 1 || fm_argv_truncated()) {
        free_argv(argv, n);
        message_box(T(STR_MSG_ERROR), T(STR_MSG_BAD_EDITOR));
        return;
    }
    argv[n] = full;
    argv[n + 1] = NULL;
    copy_str(keep, sizeof keep, e->name);
    rc = spawn_sync(ed, argv, 1, NULL);
    fm_flush_input();
    free_argv(argv, n);
    if (rc == 127)                       /* execvp: команды нет в PATH */
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 T(STR_MSG_SPAWN_FAILED), ed, strerror(ENOENT));
    else if (rc < 0 && rc != -2)          /* fork/waitpid сломались */
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 T(STR_MSG_EDITOR_FAILED), ed);
    /* rc > 0 — редактор отработал и вернул свой код: это не ошибка
     * менеджера, статус не трогаем. rc == -2 — убит сигналом (Ctrl-C). */
    remember(p, keep);
    reload_cwd(p->cwd);
}

void action_refresh(FilePane *p)
{
    if (!p) return;
    if (load_dir(p) != 0)
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 T(STR_MSG_ERROR_STR), strerror(errno));
}

static void bad_name(void)
{
    message_box(T(STR_MSG_ERROR), T(STR_MSG_BAD_NAME));
}

static FILE *open_new_file(const char *path, mode_t mode)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, mode);
    FILE *f;
    if (fd < 0) return NULL;
    f = fdopen(fd, "w");
    if (!f) { close(fd); unlink(path); }
    return f;
}

void action_mkdir(FilePane *p)
{
    char name[MAX_NAME] = "New Folder";
    char target[FM_PATH_MAX];
    int err;
    if (!p) return;
    if (!dialog_input(T(STR_MKDIR_TITLE), T(STR_MKDIR_PROMPT), name, sizeof name))
        return;
    if (!name_ok(name)) { bad_name(); return; }
    if (make_path(target, sizeof target, p->cwd, name) != 0) return;
    if (mkdir(target, 0755) == 0) {
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 T(STR_MSG_DIR_CREATED), name);
        remember(p, name);
        reload_cwd(p->cwd);
    } else {
        err = errno;
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 T(STR_MSG_ERROR_STR), strerror(err));
    }
}

void action_touch(FilePane *p)
{
    char name[MAX_NAME] = "New File.txt";
    char target[FM_PATH_MAX];
    FILE *f;
    int err;
    if (!p) return;
    if (!dialog_input(T(STR_TOUCH_TITLE), T(STR_TOUCH_PROMPT), name, sizeof name))
        return;
    if (!name_ok(name)) { bad_name(); return; }
    if (make_path(target, sizeof target, p->cwd, name) != 0) return;
    f = open_new_file(target, 0644);
    if (f) {
        (void)fclose(f);        /* файл только что создан и пуст */
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 T(STR_MSG_FILE_CREATED), name);
        remember(p, name);
        reload_cwd(p->cwd);
    } else {
        err = errno;
        if (err == EEXIST)
            snprintf(g_app.last_action, sizeof g_app.last_action,
                     T(STR_MSG_FILE_EXISTS));
        else
            snprintf(g_app.last_action, sizeof g_app.last_action,
                     T(STR_MSG_ERROR_STR), strerror(err));
    }
}

static void write_template(FILE *f, int sel)
{
    if (sel == 1) fputs("#!/usr/bin/env python3\n\ndef main():\n"
                        "    pass\n\nif __name__ == '__main__':\n"
                        "    main()\n", f);
    else if (sel == 2) fputs("#include <stdio.h>\n\nint main(void) {\n"
                             "    return 0;\n}\n", f);
    else if (sel == 3) fputs("#!/bin/bash\n\necho \"Hello\"\n", f);
}

void action_template(FilePane *p)
{
    const char *templates[] = { T(STR_TEMPLATE_EMPTY), T(STR_TEMPLATE_PY),
                                T(STR_TEMPLATE_C), T(STR_TEMPLATE_SH) };
    int sel = run_submenu(T(STR_TEMPLATE_TITLE), templates, 4, 0);
    char name[MAX_NAME];
    char target[FM_PATH_MAX];
    FILE *f;
    mode_t mode = 0644;
    if (!p || sel < 0) return;
    /* T() для STR_TEMPLATE_DEFAULT_* уже отдаёт «file.txt» и т.п.,
     * если локализация недоступна. */
    if (sel == 0)      copy_str(name, sizeof name, T(STR_TEMPLATE_DEFAULT_TXT));
    else if (sel == 1) copy_str(name, sizeof name, T(STR_TEMPLATE_DEFAULT_PY));
    else if (sel == 2) copy_str(name, sizeof name, T(STR_TEMPLATE_DEFAULT_C));
    else               copy_str(name, sizeof name, T(STR_TEMPLATE_DEFAULT_SH));
    if (!dialog_input(T(STR_TEMPLATE_TITLE), T(STR_TOUCH_PROMPT), name, sizeof name)) return;
    if (!name_ok(name)) { bad_name(); return; }
    if (make_path(target, sizeof target, p->cwd, name) != 0) return;
    if (sel == 1 || sel == 3) mode = 0755;
    f = open_new_file(target, mode);
    if (!f) {
        if (errno == EEXIST)
            snprintf(g_app.last_action, sizeof g_app.last_action,
                     T(STR_MSG_FILE_EXISTS));
        else
            snprintf(g_app.last_action, sizeof g_app.last_action,
                     T(STR_MSG_ERROR_STR), strerror(errno));
        return;
    }
    /* ФИКС (clang-tidy, cert-err33-c): fputs() в шаблоне и fclose() не
     * проверялись, поэтому «нет места на диске» давало пустой файл с
     * сообщением «создан из шаблона». Теперь ferror() виден, файл
     * удаляется, и пользователь получает честную ошибку. */
    write_template(f, sel);
    if (ferror(f) != 0 || fclose(f) != 0) {
        unlink(target);
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 T(STR_MSG_ERROR_STR), strerror(ENOSPC));
        reload_cwd(p->cwd);
        return;
    }
    if (mode & 0111) (void)chmod(target, mode);
    snprintf(g_app.last_action, sizeof g_app.last_action,
             T(STR_MSG_FROM_TEMPLATE), name);
    remember(p, name);
    reload_cwd(p->cwd);
}

static int append_ext(char *name, size_t nsz, const char *ext)
{
    size_t used = strlen(name);
    int n;
    if (!ext || used >= nsz) return -1;
    n = snprintf(name + used, nsz - used, "%s", ext);
    return (n < 0 || (size_t)n >= nsz - used) ? -1 : 0;
}

static int archive_dest_ok(const char *path)
{
    struct stat st;
    if (fstatat(AT_FDCWD, path, &st, AT_SYMLINK_NOFOLLOW) != 0)
        return errno == ENOENT;
    if (S_ISLNK(st.st_mode)) {
        message_box(T(STR_ARCHIVE_TITLE), T(STR_ARCHIVE_CONFIRM));
        return 0;
    }
    return dialog_confirm(T(STR_ARCHIVE_TITLE), T(STR_ARCHIVE_CONFIRM));
}

void action_archive(FilePane *p)
{
    char **paths;
    int count;
    /* Отображаемые имена — из локализации; расширения — жёстко,
     * чтобы архив открывался unzip/tar даже без перевода. */
    static const char *const fmt_exts[] = { ".zip", ".tgz", ".txz" };
    const char *formats[] = { T(STR_FORMAT_ZIP), T(STR_FORMAT_TGZ), T(STR_FORMAT_TXZ) };
    int fmt;
    char archname[MAX_NAME] = "archive";
    char full_arch[FM_PATH_MAX];
    char *argv[MAX_CLIP + 8];
    int ai = 0, i, limit, rc;
    if (!p) return;
    paths = alloc_path_vec(MAX_CLIP);
    if (!paths) return;
    count = get_selected_or_current(p, paths, MAX_CLIP);
    if (count == 0) goto cleanup;
    fmt = run_submenu(T(STR_FORMAT_TITLE), formats, 3, 0);
    if (fmt < 0) goto cleanup;
    if (count == 1) {
        const char *base = strrchr(paths[0], '/');
        copy_str(archname, sizeof archname, base ? base + 1 : paths[0]);
    }
    if (append_ext(archname, sizeof archname, fmt_exts[fmt]) != 0) {
        bad_name();
        goto cleanup;
    }
    if (!dialog_input(T(STR_ARCHIVE_TITLE), T(STR_ARCHIVE_PROMPT), archname, sizeof archname)) goto cleanup;
    if (!name_ok(archname)) { bad_name(); goto cleanup; }
    if (make_path(full_arch, sizeof full_arch, p->cwd, archname) != 0)
        goto cleanup;
    if (!archive_dest_ok(full_arch)) goto cleanup;
    if (fmt == 0) {
        argv[ai++] = "zip"; argv[ai++] = "-r"; argv[ai++] = full_arch;
    } else if (fmt == 1) {
        argv[ai++] = "tar"; argv[ai++] = "-czf"; argv[ai++] = full_arch; argv[ai++] = "--";
    } else {
        argv[ai++] = "tar"; argv[ai++] = "-cJf"; argv[ai++] = full_arch; argv[ai++] = "--";
    }
    limit = (int)(sizeof argv / sizeof argv[0]) - 1;
    if (ai + count > limit) {
        message_box(T(STR_MSG_ERROR), T(STR_MSG_PATH_TOO_LONG));
        goto cleanup;
    }
    for (i = 0; i < count; i++) {
        char *base = strrchr(paths[i], '/');
        argv[ai++] = base ? base + 1 : paths[i];
    }
    argv[ai] = NULL;
    rc = spawn_sync(argv[0], argv, 1, p->cwd);
    if (rc == 0) {
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 T(STR_ARCHIVE_CREATED), archname);
        remember(p, archname);
        reload_cwd(p->cwd);
    } else {
        note_spawn_failed(rc, T(STR_ARCHIVE_ERROR));
    }
cleanup:
    free_path_vec(paths, count);   /* освобождает и строки, и вектор */
}

void action_unarchive(FilePane *p)
{
    Entry *e;
    const char *dot;
    char full[FM_PATH_MAX], keep[MAX_NAME];
    char *argv[8];
    int ai = 0, rc;
    e = selected_entry(p);
    if (!e || e->is_parent || e->is_dir) return;
    dot = strrchr(e->name, '.');
    if (!dot) return;
    if (make_path(full, sizeof full, p->cwd, e->name) != 0) return;
    if (!strcasecmp(dot, ".zip")) {
        argv[ai++] = "unzip"; argv[ai++] = "-o"; argv[ai++] = "--"; argv[ai++] = full;
    } else if (!strcasecmp(dot, ".tgz") || !strcasecmp(dot, ".taz") || !strcasecmp(dot, ".gz")) {
        argv[ai++] = "tar"; argv[ai++] = "-xzf"; argv[ai++] = "--"; argv[ai++] = full;
    } else if (!strcasecmp(dot, ".xz")) {
        argv[ai++] = "tar"; argv[ai++] = "-xJf"; argv[ai++] = "--"; argv[ai++] = full;
    } else if (!strcasecmp(dot, ".bz2")) {
        argv[ai++] = "tar"; argv[ai++] = "-xjf"; argv[ai++] = "--"; argv[ai++] = full;
    } else if (!strcasecmp(dot, ".tar")) {
        argv[ai++] = "tar"; argv[ai++] = "-xf"; argv[ai++] = "--"; argv[ai++] = full;
    } else {
        message_box(T(STR_MSG_ERROR), T(STR_UNARCHIVE_UNKNOWN));
        return;
    }
    argv[ai] = NULL;
    copy_str(keep, sizeof keep, e->name);
    rc = spawn_sync(argv[0], argv, 1, p->cwd);
    remember(p, keep);
    reload_cwd(p->cwd);
    if (rc == 0)
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 T(STR_UNARCHIVE_DONE), keep);
    else
        note_spawn_failed(rc, T(STR_UNARCHIVE_ERROR));
}

/* ЭТАП 1: буфер обмена — char **; освобождается ТОЛЬКО здесь.
 * Единая точка => невозможно забыть free в одной из веток. */
void clipboard_clear(void)
{
    int i;
    if (!g_app.clip_paths) return;
    for (i = 0; i < g_app.clip_count; i++) {
        free(g_app.clip_paths[i]);
        g_app.clip_paths[i] = NULL;   /* защита от double free */
    }
    g_app.clip_count = 0;
    g_app.clip_is_cut = 0;
}

/* Вектор живёт до выхода: создаётся лениво при первом copy/cut. */
static int clipboard_ensure(void)
{
    if (g_app.clip_paths) return 1;
    g_app.clip_paths = alloc_path_vec(MAX_CLIP);
    return g_app.clip_paths != NULL;
}

void action_copy(FilePane *p)
{
    if (!clipboard_ensure()) return;   /* ENOMEM: буфер остаётся пустым */
    clipboard_clear();
    g_app.clip_count = get_selected_or_current(p, g_app.clip_paths, MAX_CLIP);
    g_app.clip_is_cut = 0;
    if (g_app.clip_count > 0) {
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 T(STR_MSG_COPIED_FILES), g_app.clip_count);
    } else {
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 T(STR_MSG_NOTHING_TO_COPY));
    }
}

void action_cut(FilePane *p)
{
    if (!clipboard_ensure()) return;
    clipboard_clear();
    g_app.clip_count = get_selected_or_current(p, g_app.clip_paths, MAX_CLIP);
    if (g_app.clip_count > 0) {
        g_app.clip_is_cut = 1;
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 T(STR_MSG_CUT_FILES), g_app.clip_count);
    } else {
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 T(STR_MSG_NOTHING_TO_CUT));
    }
}

static int make_unique_dest(char *dst, size_t dstsz, const char *dir,
                            const char *base, const char *src)
{
    char name[MAX_NAME];
    struct stat st;
    int n;
    copy_str(name, sizeof name, base);
    for (n = 0; n < 1000; n++) {
        if (n == 1) {
            if (snprintf(name, sizeof name, "%s_copy", base) >= (int)sizeof name) return -1;
        } else if (n > 1) {
            if (snprintf(name, sizeof name, "%s_copy%d", base, n) >= (int)sizeof name) return -1;
        }
        if (!name_ok(name)) return -1;
        if (make_path(dst, dstsz, dir, name) != 0) return -1;
        if (!strcmp(dst, src)) continue;
        if (lstat(dst, &st) != 0 && errno == ENOENT) return 0;
    }
    return -1;
}

static int paste_one(FilePane *p, int i)
{
    const char *src = g_app.clip_paths[i];
    const char *base = strrchr(src, '/');
    char dst[FM_PATH_MAX];
    CopyProgress cp;
    struct stat st;
    int rc;
    base = base ? base + 1 : src;
    if (!name_ok(base) || lstat(src, &st) != 0) return -1;
    if ((S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)) && path_is_inside(src, p->cwd)) {
        snprintf(g_app.last_action, sizeof g_app.last_action, T(STR_MSG_SELF_COPY));
        return -1;
    }
    if (make_unique_dest(dst, sizeof dst, p->cwd, base, src) != 0) return -1;
    if (g_app.clip_is_cut && rename(src, dst) == 0) return 0;
    memset(&cp, 0, sizeof cp);
    cp.total = S_ISDIR(st.st_mode) ? -1 : calc_size(src, 0);
    cp.last_pct = -1;
    cp.disp = base;
    cp.win = progress_init();
    if (cp.win) show_progress(cp.total == -1 ? -1 : 0, cp.disp, cp.win);
    rc = do_copy_recursive(src, dst, &cp, 0);
    if (cp.win) {
        /* ФИКС (артефакты): после delwin прогресс-окна touchwin(stdscr)
         * лишь помечает главный экран изменённым — рамка окна, которое уже
         * стёрто из памяти ncurses, оставалась на физическом экране.
         * clearok() требует полной перерисовки, а не обновления. */
        delwin(cp.win);
        cp.win = NULL;
        clearok(stdscr, TRUE);
        touchwin(stdscr);
        refresh();
    }
    if (rc != 0) return -1;
    if (g_app.clip_is_cut && fs_delete(src) != 0) return -1;
    return 0;
}

void action_paste(FilePane *p)
{
    int processed = 0, failed = 0, i;
    if (!p) return;
    g_app.last_action[0] = '\0';
    if (g_app.clip_count == 0) {
        message_box(T(STR_MSG_ATTENTION), T(STR_MSG_CLIPBOARD_EMPTY));
        return;
    }
    /* Политика частичного результата: элементы копируются по одному и
     * каждый фиксируется атомарно (rename), поэтому «сколько успело» —
     * это processed, и он же попадает в строку состояния. Откатывать
     * уже вставленные файлы нельзя: они целые, а пользователь мог начать
     * ими пользоваться. Что НЕ остаётся после сбоя — временные файлы:
     * do_copy_file() удаляет свой .vibey.XXXXXX при любом rc != 0. */
    for (i = 0; i < g_app.clip_count; i++) {
        if (paste_one(p, i) != 0) { failed = 1; break; }
        processed = i + 1;
    }
    if (g_app.clip_is_cut && processed > 0) {
        int remaining = g_app.clip_count - processed, k;
        /* ЭТАП 1: переносим УКАЗАТЕЛИ (раньше копировалось по 4 КБ на
         * элемент) и освобождаем обработанные. Каждый указатель либо
         * уезжает вниз, либо освобождается — ровно один раз. */
        for (k = 0; k < processed; k++) {
            free(g_app.clip_paths[k]);
            g_app.clip_paths[k] = NULL;
        }
        for (k = 0; k < remaining; k++)
            g_app.clip_paths[k] = g_app.clip_paths[processed + k];
        for (k = remaining; k < g_app.clip_count; k++)
            g_app.clip_paths[k] = NULL;
        g_app.clip_count = remaining;
        if (g_app.clip_count == 0) g_app.clip_is_cut = 0;
    }
    if (!failed)
        snprintf(g_app.last_action, sizeof g_app.last_action, T(STR_MSG_PASTED_FILES), processed);
    else if (!g_app.last_action[0])
        message_box(T(STR_MSG_ERROR), T(STR_MSG_COPY_FAILED));
    else
        message_box(T(STR_MSG_ERROR), g_app.last_action);
    reload_cwd(p->cwd);
    touchwin(stdscr);
    refresh();
}

void action_delete(FilePane *p)
{
    char **paths;
    int count, i, failed = 0, ok = 0;
    char msg[512];
    if (!p) return;
    paths = alloc_path_vec(MAX_CLIP);
    if (!paths) return;
    count = get_selected_or_current(p, paths, MAX_CLIP);
    if (count == 0) goto cleanup;
    if (count == 1) {
        const char *base = strrchr(paths[0], '/');
        snprintf(msg, sizeof msg, T(STR_DELETE_CONFIRM_FILE), base ? base + 1 : paths[0]);
    } else {
        snprintf(msg, sizeof msg, T(STR_DELETE_CONFIRM_MANY), count);
    }
    if (!dialog_confirm(T(STR_DELETE_TITLE), msg)) goto cleanup;
    for (i = 0; i < count; i++) {
        if (fs_delete(paths[i]) == 0) ok++;
        else failed++;
    }
    if (failed == 0)
        snprintf(g_app.last_action, sizeof g_app.last_action, T(STR_MSG_DELETED_OK), ok);
    else
        snprintf(g_app.last_action, sizeof g_app.last_action, T(STR_MSG_DELETED_FAILED), ok, failed);
    reload_cwd(p->cwd);
cleanup:
    free_path_vec(paths, count);   /* освобождает и строки, и вектор */
}

void action_rename(FilePane *p)
{
    Entry *e;
    char new_name[MAX_NAME];
    char src[FM_PATH_MAX], dst[FM_PATH_MAX];
    struct stat st;
    e = selected_entry(p);
    if (!e || e->is_parent) return;
    copy_str(new_name, sizeof new_name, e->name);
    if (!dialog_input(T(STR_RENAME_TITLE), T(STR_RENAME_PROMPT), new_name, sizeof new_name)) return;
    if (!name_ok(new_name)) { bad_name(); return; }
    if (!strcmp(new_name, e->name)) return;
    if (make_path(src, sizeof src, p->cwd, e->name) != 0 || make_path(dst, sizeof dst, p->cwd, new_name) != 0) return;
    if (lstat(dst, &st) == 0 && !dialog_confirm(T(STR_RENAME_TITLE), T(STR_RENAME_CONFIRM)))
        return;
    if (rename(src, dst) == 0) {
        snprintf(g_app.last_action, sizeof g_app.last_action, T(STR_MSG_RENAMED), e->name, new_name);
        remember(p, new_name);
        reload_cwd(p->cwd);
    } else {
        snprintf(g_app.last_action, sizeof g_app.last_action, T(STR_MSG_RENAME_FAILED), strerror(errno));
    }
}

void action_pin(FilePane *p)
{
    char **paths;
    int count, i;
    if (!p) return;
    paths = alloc_path_vec(MAX_CLIP);
    if (!paths) return;
    count = get_selected_or_current(p, paths, MAX_CLIP);
    if (count == 0) add_bookmark(p->cwd);
    else for (i = 0; i < count; i++) add_bookmark(paths[i]);
    free_path_vec(paths, count);
    save_config();
    snprintf(g_app.last_action, sizeof g_app.last_action, T(STR_PIN_OK));
}

static void type_descr(mode_t m, char *buf, size_t sz)
{
    if (S_ISREG(m))       copy_str(buf, sz, T(STR_INFO_TYPE_REG));
    else if (S_ISDIR(m))  copy_str(buf, sz, T(STR_INFO_TYPE_DIR));
    else if (S_ISLNK(m))  copy_str(buf, sz, T(STR_INFO_TYPE_LNK));
    else if (S_ISBLK(m))  copy_str(buf, sz, T(STR_INFO_TYPE_BLK));
    else if (S_ISCHR(m))  copy_str(buf, sz, T(STR_INFO_TYPE_CHR));
    else if (S_ISFIFO(m)) copy_str(buf, sz, T(STR_INFO_TYPE_FIFO));
    else if (S_ISSOCK(m)) copy_str(buf, sz, T(STR_INFO_TYPE_SOCK));
    else                  copy_str(buf, sz, T(STR_INFO_TYPE_UNKNOWN));
}

static void build_perm_string(mode_t m, char *perm)
{
    /* ФИКС (clang-tidy, bugprone-narrowing-conversions): в C символьная
     * константа имеет тип int, поэтому 's' : 'x' — это int, а присваивание
     * в char формально «сужение с implementation-defined результатом».
     * Для латиницы значение всегда помещается, но явный (char) снимает
     * вопрос и заодно фиксирует намерение. */
    perm[0] = S_ISDIR(m)  ? 'd' : S_ISLNK(m)  ? 'l' :
              S_ISBLK(m)  ? 'b' : S_ISCHR(m)  ? 'c' :
              S_ISFIFO(m) ? 'p' : S_ISSOCK(m) ? 's' : '-';
    perm[1] = (m & S_IRUSR) ? 'r' : '-';
    perm[2] = (m & S_IWUSR) ? 'w' : '-';
    perm[3] = (char)((m & S_IXUSR) ? ((m & S_ISUID) ? 's' : 'x')
                                   : ((m & S_ISUID) ? 'S' : '-'));
    perm[4] = (m & S_IRGRP) ? 'r' : '-';
    perm[5] = (m & S_IWGRP) ? 'w' : '-';
    perm[6] = (char)((m & S_IXGRP) ? ((m & S_ISGID) ? 's' : 'x')
                                   : ((m & S_ISGID) ? 'S' : '-'));
    perm[7] = (m & S_IROTH) ? 'r' : '-';
    perm[8] = (m & S_IWOTH) ? 'w' : '-';
    perm[9] = (char)((m & S_IXOTH) ? ((m & S_ISVTX) ? 't' : 'x')
                                   : ((m & S_ISVTX) ? 'T' : '-'));
    perm[10] = '\0';
}

static void fmt_time(time_t t, char *out, size_t sz)
{
    struct tm tmv;
    /* strftime() возвращает 0 и в случае «результат не поместился», и когда
     * шаблон дал пустую строку — отличить нельзя, поэтому нулевой результат
     * считаем ошибкой и подставляем "?". Раньше результат игнорировался и
     * при крошечном буфере в info-диалог попадала пустота. */
    if (!localtime_r(&t, &tmv)
        || strftime(out, sz, "%Y-%m-%d %H:%M:%S", &tmv) == 0)
        snprintf(out, sz, "?");
}

void action_info(FilePane *p)
{
    Entry *e;
    char full[FM_PATH_MAX], type_buf[64], perm[11], owner_buf[160];
    char group_buf[160], size_human[32], atime_buf[64], mtime_buf[64];
    char ctime_buf[64], link_target[FM_PATH_MAX], mime_buf[128];
    char info[4096];
    struct stat st, tgt_st;
    struct passwd *pw;
    struct group *gr;
    int pos = 0;
    e = selected_entry(p);
    if (!e || e->is_parent) {
        message_box(T(STR_MSG_ATTENTION), T(STR_MSG_NOTHING_SELECTED));
        return;
    }
    if (make_path(full, sizeof full, p->cwd, e->name) != 0) return;
    if (lstat(full, &st) != 0) return;
    link_target[0] = '\0';
    mime_buf[0] = '\0';
    info[0] = '\0';
    type_descr(st.st_mode, type_buf, sizeof type_buf);
    build_perm_string(st.st_mode, perm);
    pw = getpwuid(st.st_uid);
    gr = getgrgid(st.st_gid);
    if (pw) snprintf(owner_buf, sizeof owner_buf, "%s (UID %u)", pw->pw_name, (unsigned)st.st_uid);
    else    snprintf(owner_buf, sizeof owner_buf, "UID %u", (unsigned)st.st_uid);
    if (gr) snprintf(group_buf, sizeof group_buf, "%s (GID %u)", gr->gr_name, (unsigned)st.st_gid);
    else    snprintf(group_buf, sizeof group_buf, "GID %u", (unsigned)st.st_gid);
    format_size(size_human, sizeof size_human, st.st_size);
    fmt_time(st.st_atime, atime_buf, sizeof atime_buf);
    fmt_time(st.st_mtime, mtime_buf, sizeof mtime_buf);
    fmt_time(st.st_ctime, ctime_buf, sizeof ctime_buf);
    if (S_ISLNK(st.st_mode)) {
        ssize_t ln = readlink(full, link_target, sizeof link_target - 1);
        if (ln < 0) ln = 0;
        link_target[ln] = '\0';
    }
    /* ЭТАП 4: MIME у file(1) спрашиваем только для бинарников — для
     * текста тип уже известен по расширению, а fork+exec за диалогом
     * Info не оправдан. */
    if (stat(full, &tgt_st) == 0 && S_ISREG(tgt_st.st_mode)) {
        if (strncmp(full, "/proc/", 6) != 0 && strncmp(full, "/sys/", 5) != 0
            && fm_content_is_binary(full))
            get_mime_type(full, mime_buf, sizeof mime_buf);
    }

    /* ФИКС: раньше при нехватке места во второстепенных полях функция
     * выходила БЕЗ диалога — пользователь жал клавишу и не видел ничего.
     * Теперь пропускаем то, что не влезло, но диалог показываем всегда. */
    pos = appendf(info, sizeof info, pos,
                  "%-18s %s\n%-18s %s\n%-18s %s\n",
                  T(STR_INFO_NAME), e->name, T(STR_INFO_PATH), full, T(STR_INFO_TYPE), type_buf);
    if (pos < 0) pos = (int)sizeof info - 1;      /* каркас не влез — не падаем */
    if (mime_buf[0] && pos >= 0)
        pos = appendf(info, sizeof info, pos, "%-18s %s\n", T(STR_INFO_MIME), mime_buf);
    if (link_target[0] && pos >= 0)
        pos = appendf(info, sizeof info, pos, "%-18s %s\n", T(STR_INFO_LINK), link_target);
    if (pos < 0) pos = (int)sizeof info - 1;
    pos = appendf(info, sizeof info, pos,
        "\n%-18s %lld %s (%s)\n"
        "%-18s %s  (%04o)\n%-18s %s\n"
        "%-18s %s\n%-18s %ld\n"
        "\n%-18s %llu\n%-18s major=%u, minor=%u\n"
        "\n%-18s %s\n%-18s %s\n%-18s %s\n",
        T(STR_INFO_SIZE), (long long)st.st_size, T(STR_INFO_BYTES), size_human,
        T(STR_INFO_PERMS), perm, (unsigned)(st.st_mode & 07777),
        T(STR_INFO_OWNER), owner_buf,
        T(STR_INFO_GROUP), group_buf,
        T(STR_INFO_LINKS), (long)st.st_nlink,
        T(STR_INFO_INODE), (unsigned long long)st.st_ino,
        T(STR_INFO_DEV), (unsigned)major(st.st_dev), (unsigned)minor(st.st_dev),
        T(STR_INFO_MTIME), mtime_buf,
        T(STR_INFO_ATIME), atime_buf,
        T(STR_INFO_CTIME), ctime_buf);
    if (pos < 0) info[sizeof info - 1] = '\0';
    message_box(T(STR_INFO_TITLE), info);
}

int execute_action(int act, FilePane *p)
{
    switch ((Action)act) {
    case ACT_OPEN: action_open(p); break;
    case ACT_VIEW: action_view(p); break;
    case ACT_OPEN_AS: action_open_as(p); break;
    case ACT_EDIT: action_edit(p); break;
    case ACT_COPY: action_copy(p); break;
    case ACT_CUT: action_cut(p); break;
    case ACT_PASTE: action_paste(p); break;
    case ACT_DELETE: action_delete(p); break;
    case ACT_MKDIR: action_mkdir(p); break;
    case ACT_TOUCH: action_touch(p); break;
    case ACT_TEMPLATE: action_template(p); break;
    case ACT_ARCHIVE: action_archive(p); break;
    case ACT_UNARCHIVE: action_unarchive(p); break;
    case ACT_RENAME: action_rename(p); break;
    case ACT_PIN: action_pin(p); break;
    case ACT_REFRESH: action_refresh(p); break;
    case ACT_QUIT: return 0;
    case ACT_INFO: action_info(p); break;
    default: break;
    }
    return 1;
}
