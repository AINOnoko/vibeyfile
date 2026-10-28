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

void launch_path(const char *name, const char *full)
{
    char fullbuf[PATH_MAX];
    const char *dot;
    int i;
    if (!name || !full) return;
    copy_str(fullbuf, sizeof fullbuf, full);
    dot = strrchr(name, '.');
    if (dot && dot != name && dot[1]) {
        const char *ext = dot + 1;
        for (i = 0; i < g_app.assoc_count; i++) {
            if (strcasecmp(g_app.associations[i].ext, ext) == 0) {
                char *argv[MAX_ARGS];
                int n = tokenize_cmd(g_app.associations[i].cmd, argv, MAX_ARGS);
                if (n > 0 && n < MAX_ARGS - 1) {
                    argv[n] = fullbuf;
                    argv[n + 1] = NULL;
                    spawn_detached_argv(argv);
                    free_argv(argv, n);
                    snprintf(g_app.last_action, sizeof g_app.last_action,
                             "Открыто: %s (%s)", name,
                             g_app.associations[i].cmd);
                    return;
                }
                free_argv(argv, n);
            }
        }
    }
    if ((is_text_file(name) || is_plain_name(name))
        && !is_binary_content(fullbuf)) {
        view_file(fullbuf);
        return;
    }
    {
        char *xdg_argv[] = { "xdg-open", fullbuf, NULL };
        spawn_detached_argv(xdg_argv);
    }
    snprintf(g_app.last_action, sizeof g_app.last_action,
             "Открыто: %s", name);
}

void action_parent(FilePane *p)
{
    char prev[PATH_MAX], parent[PATH_MAX];
    const char *base;
    size_t i;
    int err;
    if (!p || !p->cwd[0] || !strcmp(p->cwd, "/")) return;
    copy_str(prev, sizeof prev, p->cwd);
    if (path_parent(parent, sizeof parent, prev) != 0) return;
    if (enter_dir(parent, p) != 0) {
        err = errno;
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 "Не удалось перейти вверх: %s", strerror(err));
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
    char full[PATH_MAX];
    int err;
    e = selected_entry(p);
    if (!e) {
        message_box("Внимание", "Ничего не выбрано");
        return;
    }
    if (e->is_parent) { action_parent(p); return; }
    if (make_path(full, sizeof full, p->cwd, e->name) != 0) {
        message_box("Ошибка", "Путь слишком длинный");
        return;
    }
    if (e->is_dir) {
        if (enter_dir(full, p) != 0) {
            err = errno;
            snprintf(g_app.last_action, sizeof g_app.last_action,
                     "Не удалось открыть: %s", strerror(err));
        }
        return;
    }
    launch_path(e->name, full);
}

void action_view(FilePane *p)
{
    Entry *e;
    char full[PATH_MAX];
    e = selected_entry(p);
    if (!e) {
        message_box("Внимание", "Ничего не выбрано");
        return;
    }
    if (e->is_parent) {
        message_box("Внимание", "Нельзя просмотреть родительский каталог");
        return;
    }
    if (e->is_dir) {
        message_box("Внимание", "Это каталог, а не файл");
        return;
    }
    if (make_path(full, sizeof full, p->cwd, e->name) != 0) {
        message_box("Ошибка", "Путь слишком длинный");
        return;
    }
    /* view_file сам отсеивает бинарники, спецфайлы и слишком большие файлы */
    view_file(full);
    snprintf(g_app.last_action, sizeof g_app.last_action,
             "Просмотр: %s", e->name);
}

void action_open_as(FilePane *p)
{
    char cmd_buf[256] = "xdg-open";
    Entry *e;
    char target[PATH_MAX];
    char *argv[MAX_ARGS];
    int n;
    e = selected_entry(p);
    if (!e || e->is_parent) return;
    if (!dialog_input("Открыть как", "Команда (без shell):",
                      cmd_buf, sizeof cmd_buf)) return;
    if (!cmd_buf[0]) return;
    if (make_path(target, sizeof target, p->cwd, e->name) != 0) return;
    n = tokenize_cmd(cmd_buf, argv, MAX_ARGS);
    if (n > 0 && n < MAX_ARGS - 1) {
        argv[n] = target;
        argv[n + 1] = NULL;
        spawn_detached_argv(argv);
    } else if (n > 0) {
        message_box("Ошибка", "Некорректная команда");
    }
    free_argv(argv, n);
}

static const char *find_editor(void)
{
    const char *env_ed = getenv("EDITOR");
    static const char *editors[] = { "nvim", "vim", "vi", "nano" };
    const char *path;
    char *path_copy, *save, *dir;
    const char *ed = NULL;
    int i;
    if (env_ed && *env_ed) return env_ed;
    path = getenv("PATH");
    if (!path) return NULL;
    for (i = 0; i < 4 && !ed; i++) {
        path_copy = strdup(path);
        if (!path_copy) break;
        save = NULL;
        dir = strtok_r(path_copy, ":", &save);
        while (dir) {
            char full[PATH_MAX];
            snprintf(full, sizeof full, "%s/%s", dir, editors[i]);
            if (access(full, X_OK) == 0) { ed = editors[i]; break; }
            dir = strtok_r(NULL, ":", &save);
        }
        free(path_copy);
    }
    return ed;
}

void action_edit(FilePane *p)
{
    Entry *e;
    const char *ed;
    char full[PATH_MAX], keep[MAX_NAME];
    char *argv[MAX_ARGS];
    struct stat st;
    int n;
    e = selected_entry(p);
    if (!e || e->is_dir || e->is_parent) return;
    if (make_path(full, sizeof full, p->cwd, e->name) != 0) return;
    if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) {
        message_box("Ошибка", "Это не обычный файл");
        return;
    }
    ed = find_editor();
    if (!ed) { message_box("Ошибка", "Редактор не найден"); return; }
    n = tokenize_cmd(ed, argv, MAX_ARGS);
    if (n <= 0 || n >= MAX_ARGS - 1) {
        free_argv(argv, n);
        message_box("Ошибка", "Некорректный EDITOR");
        return;
    }
    argv[n] = full;
    argv[n + 1] = NULL;
    copy_str(keep, sizeof keep, e->name);
    run_foreground(argv, NULL);
    free_argv(argv, n);
    remember(p, keep);
    reload_cwd(p->cwd);
}

void action_refresh(FilePane *p)
{
    if (!p) return;
    if (load_dir(p) != 0)
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 "Не удалось обновить: %s", strerror(errno));
}

static void bad_name(void)
{
    message_box("Ошибка", "Недопустимое имя");
}

static FILE *open_new_file(const char *path, mode_t mode)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, mode);
    FILE *f;
    if (fd < 0) return NULL;
    f = fdopen(fd, "w");
    if (!f) { close(fd); unlink(path); }
    return f;
}

void action_mkdir(FilePane *p)
{
    char name[MAX_NAME] = "Новая папка";
    char target[PATH_MAX];
    int err;
    if (!p) return;
    if (!dialog_input("Создать папку", "Имя папки:", name, sizeof name))
        return;
    if (!name_ok(name)) { bad_name(); return; }
    if (make_path(target, sizeof target, p->cwd, name) != 0) return;
    if (mkdir(target, 0755) == 0) {
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 "Создана папка: %s", name);
        remember(p, name);
        reload_cwd(p->cwd);
    } else {
        err = errno;
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 "Ошибка: %s", strerror(err));
    }
}

void action_touch(FilePane *p)
{
    char name[MAX_NAME] = "Новый файл.txt";
    char target[PATH_MAX];
    FILE *f;
    int err;
    if (!p) return;
    if (!dialog_input("Создать файл", "Имя файла:", name, sizeof name))
        return;
    if (!name_ok(name)) { bad_name(); return; }
    if (make_path(target, sizeof target, p->cwd, name) != 0) return;
    f = open_new_file(target, 0644);
    if (f) {
        fclose(f);
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 "Создан файл: %s", name);
        remember(p, name);
        reload_cwd(p->cwd);
    } else {
        err = errno;
        if (err == EEXIST)
            snprintf(g_app.last_action, sizeof g_app.last_action,
                     "Файл уже существует");
        else
            snprintf(g_app.last_action, sizeof g_app.last_action,
                     "Ошибка: %s", strerror(err));
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
    const char *templates[] = { "Пустой txt", "Python (.py)",
                                "C (.c)", "Shell (.sh)" };
    int sel = run_submenu("Шаблоны", templates, 4);
    char name[MAX_NAME];
    char target[PATH_MAX];
    FILE *f;
    mode_t mode = 0644;
    if (!p || sel < 0) return;
    if (sel == 0)      copy_str(name, sizeof name, "новый.txt");
    else if (sel == 1) copy_str(name, sizeof name, "новый.py");
    else if (sel == 2) copy_str(name, sizeof name, "новый.c");
    else               copy_str(name, sizeof name, "новый.sh");
    if (!dialog_input("Создать из шаблона", "Имя файла:",
                      name, sizeof name)) return;
    if (!name_ok(name)) { bad_name(); return; }
    if (make_path(target, sizeof target, p->cwd, name) != 0) return;
    if (sel == 1 || sel == 3) mode = 0755;
    f = open_new_file(target, mode);
    if (!f) {
        if (errno == EEXIST)
            snprintf(g_app.last_action, sizeof g_app.last_action,
                     "Файл уже существует");
        else
            snprintf(g_app.last_action, sizeof g_app.last_action,
                     "Ошибка: %s", strerror(errno));
        return;
    }
    write_template(f, sel);
    fclose(f);
    if (mode & 0111) chmod(target, mode);
    snprintf(g_app.last_action, sizeof g_app.last_action,
             "Создан из шаблона: %s", name);
    remember(p, name);
    reload_cwd(p->cwd);
}

void action_archive(FilePane *p)
{
    char paths[MAX_CLIP][PATH_MAX];
    int count;
    const char *formats[] = { ".zip", ".tar.gz", ".tar.xz" };
    int fmt;
    char archname[MAX_NAME] = "archive";
    char full_arch[PATH_MAX];
    char *argv[MAX_CLIP + 8];
    int ai = 0, i, limit;
    if (!p) return;
    count = get_selected_or_current(p, paths, MAX_CLIP);
    if (count == 0) return;
    fmt = run_submenu("Формат", formats, 3);
    if (fmt < 0) return;
    if (count == 1) {
        const char *base = strrchr(paths[0], '/');
        copy_str(archname, sizeof archname, base ? base + 1 : paths[0]);
    }
    if (strlen(archname) + strlen(formats[fmt]) >= sizeof archname) {
        bad_name();
        return;
    }
    strcat(archname, formats[fmt]);
    if (!dialog_input("Архивация", "Имя архива:",
                      archname, sizeof archname)) return;
    if (!name_ok(archname)) { bad_name(); return; }
    if (make_path(full_arch, sizeof full_arch, p->cwd, archname) != 0)
        return;
    if (access(full_arch, F_OK) == 0 &&
        !dialog_confirm("Архивация", "Файл уже существует. Заменить?"))
        return;
    if (fmt == 0) {
        argv[ai++] = "zip"; argv[ai++] = "-r";
        argv[ai++] = full_arch; argv[ai++] = "--";
    } else if (fmt == 1) {
        argv[ai++] = "tar"; argv[ai++] = "-czf";
        argv[ai++] = full_arch; argv[ai++] = "--";
    } else {
        argv[ai++] = "tar"; argv[ai++] = "-cJf";
        argv[ai++] = full_arch; argv[ai++] = "--";
    }
    limit = (int)(sizeof argv / sizeof argv[0]) - 1;
    if (ai + count > limit) {
        message_box("Ошибка", "Слишком много файлов для одной команды");
        return;
    }
    for (i = 0; i < count; i++) {
        char *base = strrchr(paths[i], '/');
        argv[ai++] = base ? base + 1 : paths[i];
    }
    argv[ai] = NULL;
    if (run_foreground(argv, p->cwd) == 0) {
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 "Создан архив: %s", archname);
        remember(p, archname);
        reload_cwd(p->cwd);
    } else {
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 "Ошибка архивации");
    }
}

void action_unarchive(FilePane *p)
{
    Entry *e;
    const char *dot;
    char full[PATH_MAX], keep[MAX_NAME];
    char *argv[8];
    int ai = 0, rc;
    e = selected_entry(p);
    if (!e || e->is_parent || e->is_dir) return;
    dot = strrchr(e->name, '.');
    if (!dot) return;
    if (make_path(full, sizeof full, p->cwd, e->name) != 0) return;
    if (!strcasecmp(dot, ".zip")) {
        argv[ai++] = "unzip"; argv[ai++] = "-o";
        argv[ai++] = "--"; argv[ai++] = full;
    } else if (!strcasecmp(dot, ".tgz") || !strcasecmp(dot, ".taz")
               || !strcasecmp(dot, ".gz")) {
        argv[ai++] = "tar"; argv[ai++] = "-xzf";
        argv[ai++] = "--"; argv[ai++] = full;
    } else if (!strcasecmp(dot, ".xz")) {
        argv[ai++] = "tar"; argv[ai++] = "-xJf";
        argv[ai++] = "--"; argv[ai++] = full;
    } else if (!strcasecmp(dot, ".bz2")) {
        argv[ai++] = "tar"; argv[ai++] = "-xjf";
        argv[ai++] = "--"; argv[ai++] = full;
    } else if (!strcasecmp(dot, ".tar")) {
        argv[ai++] = "tar"; argv[ai++] = "-xf";
        argv[ai++] = "--"; argv[ai++] = full;
    } else {
        message_box("Ошибка", "Неизвестный формат");
        return;
    }
    argv[ai] = NULL;
    copy_str(keep, sizeof keep, e->name);
    rc = run_foreground(argv, p->cwd);
    remember(p, keep);
    reload_cwd(p->cwd);
    if (rc == 0)
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 "Разархивировано: %s", keep);
    else
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 "Ошибка разархивации");
}

void action_copy(FilePane *p)
{
    g_app.clip_count = get_selected_or_current(p, g_app.clip_paths, MAX_CLIP);
    if (g_app.clip_count > 0) {
        g_app.clip_is_cut = 0;
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 "Скопировано файлов: %d", g_app.clip_count);
    } else {
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 "Нечего копировать");
    }
}

void action_cut(FilePane *p)
{
    g_app.clip_count = get_selected_or_current(p, g_app.clip_paths, MAX_CLIP);
    if (g_app.clip_count > 0) {
        g_app.clip_is_cut = 1;
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 "Вырезано файлов: %d", g_app.clip_count);
    } else {
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 "Нечего вырезать");
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
            if (snprintf(name, sizeof name, "%s_copy", base) >= (int)sizeof name)
                return -1;
        } else if (n > 1) {
            if (snprintf(name, sizeof name, "%s_copy%d", base, n)
                >= (int)sizeof name)
                return -1;
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
    char dst[PATH_MAX];
    CopyProgress cp;
    struct stat st;
    int rc;
    base = base ? base + 1 : src;
    if (!name_ok(base) || lstat(src, &st) != 0) return -1;
    if ((S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode))
        && path_is_inside(src, p->cwd)) {
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 "Нельзя копировать каталог в самого себя");
        return -1;
    }
    if (make_unique_dest(dst, sizeof dst, p->cwd, base, src) != 0)
        return -1;
    if (g_app.clip_is_cut && rename(src, dst) == 0)
        return 0;
    memset(&cp, 0, sizeof cp);
    cp.total = calc_size(src, 0);
    cp.last_pct = -1;
    cp.disp = base;
    cp.win = progress_init();
    if (cp.win) show_progress(0, cp.disp, cp.win);
    rc = do_copy_recursive(src, dst, &cp);
    if (cp.win) {
        delwin(cp.win);
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
        message_box("Внимание", "Буфер пуст");
        return;
    }
    for (i = 0; i < g_app.clip_count; i++) {
        if (paste_one(p, i) != 0) { failed = 1; break; }
        processed = i + 1;
    }
    if (g_app.clip_is_cut && processed > 0) {
        int remaining = g_app.clip_count - processed, k;
        for (k = 0; k < remaining; k++)
            copy_str(g_app.clip_paths[k], PATH_MAX,
                     g_app.clip_paths[processed + k]);
        g_app.clip_count = remaining;
        if (g_app.clip_count == 0) g_app.clip_is_cut = 0;
    }
    if (!failed)
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 "Вставлено файлов: %d", processed);
    else if (!g_app.last_action[0])
        message_box("Ошибка", "Не удалось скопировать файл.");
    else
        message_box("Ошибка", g_app.last_action);
    reload_cwd(p->cwd);
    touchwin(stdscr);
    refresh();
}

void action_delete(FilePane *p)
{
    char paths[MAX_CLIP][PATH_MAX];
    int count, i, failed = 0, ok = 0;
    char msg[512];
    if (!p) return;
    count = get_selected_or_current(p, paths, MAX_CLIP);
    if (count == 0) return;
    if (count == 1) {
        const char *base = strrchr(paths[0], '/');
        snprintf(msg, sizeof msg, "Удалить %.200s?",
                 base ? base + 1 : paths[0]);
    } else {
        snprintf(msg, sizeof msg, "Удалить %d объектов?", count);
    }
    if (!dialog_confirm("Удаление", msg)) return;
    for (i = 0; i < count; i++) {
        if (fs_delete(paths[i]) == 0) ok++;
        else failed++;
    }
    if (failed == 0)
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 "Удалено объектов: %d", ok);
    else
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 "Удалено %d, ошибок %d", ok, failed);
    reload_cwd(p->cwd);
}

void action_rename(FilePane *p)
{
    Entry *e;
    char new_name[MAX_NAME];
    char src[PATH_MAX], dst[PATH_MAX];
    struct stat st;
    e = selected_entry(p);
    if (!e || e->is_parent) return;
    copy_str(new_name, sizeof new_name, e->name);
    if (!dialog_input("Переименование", "Новое имя:",
                      new_name, sizeof new_name)) return;
    if (!name_ok(new_name)) { bad_name(); return; }
    if (!strcmp(new_name, e->name)) return;
    if (make_path(src, sizeof src, p->cwd, e->name) != 0
        || make_path(dst, sizeof dst, p->cwd, new_name) != 0) return;
    if (lstat(dst, &st) == 0 &&
        !dialog_confirm("Переименование", "Имя занято. Заменить?"))
        return;
    if (rename(src, dst) == 0) {
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 "Переименовано: %s -> %s", e->name, new_name);
        remember(p, new_name);
        reload_cwd(p->cwd);
    } else {
        snprintf(g_app.last_action, sizeof g_app.last_action,
                 "Ошибка переименования: %s", strerror(errno));
    }
}

void action_pin(FilePane *p)
{
    char paths[MAX_CLIP][PATH_MAX];
    int count, i;
    if (!p) return;
    count = get_selected_or_current(p, paths, MAX_CLIP);
    if (count == 0) add_bookmark(p->cwd);
    else for (i = 0; i < count; i++) add_bookmark(paths[i]);
    save_config();
    snprintf(g_app.last_action, sizeof g_app.last_action,
             "Добавлено в избранное");
}

static void type_descr(mode_t m, char *buf, size_t sz)
{
    if (S_ISREG(m))       snprintf(buf, sz, "Обычный файл");
    else if (S_ISDIR(m))  snprintf(buf, sz, "Каталог");
    else if (S_ISLNK(m))  snprintf(buf, sz, "Символическая ссылка");
    else if (S_ISBLK(m))  snprintf(buf, sz, "Блочное устройство");
    else if (S_ISCHR(m))  snprintf(buf, sz, "Символьное устройство");
    else if (S_ISFIFO(m)) snprintf(buf, sz, "FIFO (pipe)");
    else if (S_ISSOCK(m)) snprintf(buf, sz, "Сокет");
    else                  snprintf(buf, sz, "Неизвестный тип");
}

static void build_perm_string(mode_t m, char *perm)
{
    perm[0] = S_ISDIR(m)  ? 'd' : S_ISLNK(m)  ? 'l' :
              S_ISBLK(m)  ? 'b' : S_ISCHR(m)  ? 'c' :
              S_ISFIFO(m) ? 'p' : S_ISSOCK(m) ? 's' : '-';
    perm[1] = (m & S_IRUSR) ? 'r' : '-';
    perm[2] = (m & S_IWUSR) ? 'w' : '-';
    perm[3] = (m & S_IXUSR) ? ((m & S_ISUID) ? 's' : 'x')
                            : ((m & S_ISUID) ? 'S' : '-');
    perm[4] = (m & S_IRGRP) ? 'r' : '-';
    perm[5] = (m & S_IWGRP) ? 'w' : '-';
    perm[6] = (m & S_IXGRP) ? ((m & S_ISGID) ? 's' : 'x')
                            : ((m & S_ISGID) ? 'S' : '-');
    perm[7] = (m & S_IROTH) ? 'r' : '-';
    perm[8] = (m & S_IWOTH) ? 'w' : '-';
    perm[9] = (m & S_IXOTH) ? ((m & S_ISVTX) ? 't' : 'x')
                            : ((m & S_ISVTX) ? 'T' : '-');
    perm[10] = '\0';
}

static void fmt_time(time_t t, char *out, size_t sz)
{
    struct tm tmv;
    if (localtime_r(&t, &tmv)) strftime(out, sz, "%Y-%m-%d %H:%M:%S", &tmv);
    else                       snprintf(out, sz, "?");
}

void action_info(FilePane *p)
{
    Entry *e;
    char full[PATH_MAX], type_buf[64], perm[11], owner_buf[160];
    char group_buf[160], size_human[32], atime_buf[64], mtime_buf[64];
    char ctime_buf[64], link_target[PATH_MAX], mime_buf[128];
    char info[4096];
    struct stat st, tgt_st;
    struct passwd *pw;
    struct group *gr;
    int pos = 0;
    e = selected_entry(p);
    if (!e || e->is_parent) {
        message_box("Внимание", "Ничего не выбрано");
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
    if (pw) snprintf(owner_buf, sizeof owner_buf, "%s (UID %u)",
                     pw->pw_name, (unsigned)st.st_uid);
    else    snprintf(owner_buf, sizeof owner_buf, "UID %u",
                     (unsigned)st.st_uid);
    if (gr) snprintf(group_buf, sizeof group_buf, "%s (GID %u)",
                     gr->gr_name, (unsigned)st.st_gid);
    else    snprintf(group_buf, sizeof group_buf, "GID %u",
                     (unsigned)st.st_gid);
    format_size(size_human, sizeof size_human, st.st_size);
    fmt_time(st.st_atime, atime_buf, sizeof atime_buf);
    fmt_time(st.st_mtime, mtime_buf, sizeof mtime_buf);
    fmt_time(st.st_ctime, ctime_buf, sizeof ctime_buf);
    if (S_ISLNK(st.st_mode)) {
        ssize_t ln = readlink(full, link_target, sizeof link_target - 1);
        if (ln < 0) ln = 0;
        link_target[ln] = '\0';
    }
    if (stat(full, &tgt_st) == 0 && S_ISREG(tgt_st.st_mode))
        get_mime_type(full, mime_buf, sizeof mime_buf);
    pos = appendf(info, sizeof info, pos,
                  "Имя:            %s\nПолный путь:    %s\nТип:            %s\n",
                  e->name, full, type_buf);
    if (pos < 0) return;
    if (mime_buf[0]) {
        pos = appendf(info, sizeof info, pos, "MIME:           %s\n", mime_buf);
        if (pos < 0) return;
    }
    if (link_target[0]) {
        pos = appendf(info, sizeof info, pos, "Ссылка на:      %s\n",
                      link_target);
        if (pos < 0) return;
    }
    pos = appendf(info, sizeof info, pos,
        "\nРазмер:         %lld байт (%s)\n"
        "Права:          %s  (%04o)\nВладелец:       %s\n"
        "Группа:         %s\nЖёстких ссылок: %ld\n"
        "\nInode:          %llu\nУстройство:     major=%u, minor=%u\n"
        "\nИзменён:        %s\nОткрыт:         %s\nСтатус изменён: %s\n",
        (long long)st.st_size, size_human, perm,
        (unsigned)(st.st_mode & 07777), owner_buf, group_buf,
        (long)st.st_nlink, (unsigned long long)st.st_ino,
        (unsigned)major(st.st_dev), (unsigned)minor(st.st_dev),
        mtime_buf, atime_buf, ctime_buf);
    if (pos < 0) info[sizeof info - 1] = '\0';
    message_box("Свойства файла", info);
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
