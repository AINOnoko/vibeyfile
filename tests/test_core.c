#include "fm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (cond) return;
    fprintf(stderr, "FAIL: %s\n", msg);
    g_fail++;
}

static void test_path_display(void)
{
    char buf[PATH_MAX];
    const char *old = getenv("HOME");
    setenv("HOME", "/home/user", 1);
    path_display(buf, sizeof buf, "/home/user/Загрузки");
    expect(strcmp(buf, "~/Загрузки") == 0, "tilde child");
    path_display(buf, sizeof buf, "/home/user");
    expect(strcmp(buf, "~") == 0, "tilde home");
    path_display(buf, sizeof buf, "/home/username/Загрузки");
    expect(strcmp(buf, "/home/username/Загрузки") == 0, "tilde prefix");
    path_display(buf, sizeof buf, "/etc");
    expect(strcmp(buf, "/etc") == 0, "tilde outside");
    setenv("HOME", "/home/user/", 1);
    path_display(buf, sizeof buf, "/home/user/Документы/a");
    expect(strcmp(buf, "~/Документы/a") == 0, "tilde nested");
    if (old) setenv("HOME", old, 1);
    else unsetenv("HOME");
}

static void test_paths(void)
{
    char buf[PATH_MAX];
    char incomplete[] = { (char)0xE0, '\0' };

    expect(name_ok("файл.txt"), "utf8 name");
    expect(!name_ok(""), "empty name");
    expect(!name_ok("."), "dot");
    expect(!name_ok(".."), "dotdot");
    expect(!name_ok("a/b"), "slash");
    expect(!name_ok("a\\b"), "backslash");
    expect(make_path(buf, sizeof buf, "/", "a") == 0, "make root");
    expect(strcmp(buf, "/a") == 0, "root path");
    expect(make_path(buf, sizeof buf, "/tmp/", "a") == 0, "make trim");
    expect(strcmp(buf, "/tmp/a") == 0, "trimmed path");
    expect(path_parent(buf, sizeof buf, "/tmp/a") == 0, "parent ok");
    expect(strcmp(buf, "/tmp") == 0, "parent value");
    expect(path_parent(buf, sizeof buf, "/") != 0, "root parent");
    expect(path_is_inside("/foo", "/foo/bar"), "inside");
    expect(!path_is_inside("/foo", "/foobar"), "prefix trap");
    expect(path_is_inside("/", "/tmp"), "root contains");
    expect(utf8_prefix_bytes(incomplete, 5) == 1, "incomplete utf8");
    expect(utf8_strlen("привет") == 6, "utf8 len");
}

static void test_formatting(void)
{
    char buf[64];

    // format_size
    format_size(buf, sizeof buf, 0);
    expect(strcmp(buf, "0B") == 0, "size 0");
    format_size(buf, sizeof buf, 512);
    expect(strcmp(buf, "512B") == 0, "size 512");
    format_size(buf, sizeof buf, 1024);
    expect(strcmp(buf, "1.0K") == 0, "size 1K");
    format_size(buf, sizeof buf, 1024 * 1024 * 2);
    expect(strcmp(buf, "2.0M") == 0, "size 2M");

    // format_mode (без типа файла)
    format_mode(buf, sizeof buf, 0755);
    expect(strcmp(buf, "rwxr-xr-x") == 0, "mode 755");
    format_mode(buf, sizeof buf, 0644);
    expect(strcmp(buf, "rw-r--r--") == 0, "mode 644");
    format_mode(buf, sizeof buf, 0700);
    expect(strcmp(buf, "rwx------") == 0, "mode 700");

    // build_padded
    build_padded(buf, sizeof buf, "abc", 5, 0); // left align
    expect(strcmp(buf, "abc  ") == 0, "pad left");
    build_padded(buf, sizeof buf, "abc", 5, 1); // right align
    expect(strcmp(buf, "  abc") == 0, "pad right");
    build_padded(buf, sizeof buf, "abcdef", 3, 0); // truncate
    expect(strcmp(buf, "abc") == 0, "pad truncate");
}

static void test_exec(void)
{
    char *argv[8];

    // tokenize
    int n = tokenize_cmd("ls -l /tmp", argv, 8);
    expect(n == 3, "tokenize count");
    expect(strcmp(argv[0], "ls") == 0, "token 1");
    expect(strcmp(argv[1], "-l") == 0, "token 2");
    expect(strcmp(argv[2], "/tmp") == 0, "token 3");
    expect(argv[3] == NULL, "token null terminator");
    free_argv(argv, n);

    // empty
    n = tokenize_cmd("", argv, 8);
    expect(n == 0, "tokenize empty");
    expect(argv[0] == NULL, "token empty null");
    free_argv(argv, n);

    // spaces
    n = tokenize_cmd("   echo   hello   ", argv, 8);
    expect(n == 2, "tokenize spaces count");
    expect(strcmp(argv[0], "echo") == 0, "token spaces 1");
    expect(strcmp(argv[1], "hello") == 0, "token spaces 2");
    free_argv(argv, n);
}

static void test_theme(void)
{
    // theme_color_ok
    expect(theme_color_ok("#FFFFFF") == 1, "color upper");
    expect(theme_color_ok("#ffffff") == 1, "color lower");
    expect(theme_color_ok("#FFF") == 0, "color short");
    expect(theme_color_ok("FFFFFF") == 0, "color no hash");
    expect(theme_color_ok("#GGGGGG") == 0, "color invalid hex");
    expect(theme_color_ok(NULL) == 0, "color null");
    expect(binding_str_to_key("F1") == KEY_F(1), "bind F1");
    expect(binding_str_to_key("UP") == KEY_UP, "bind up");

    // color_to_256 (проверяем, что возвращает валидный индекс, а не белый по умолчанию)
    int c = color_to_256("#000000");
    expect(c == 16, "color black to 256"); // 16 — черный в xterm-256

    c = color_to_256("#FF0000");
    expect(c == 196, "color red to 256"); // 196 — чистый красный
}

static void test_fs(const char *root)
{
    char src[PATH_MAX], dst[PATH_MAX], sub[PATH_MAX], cwd[PATH_MAX];
    char exe[PATH_MAX], copied[PATH_MAX], nested[PATH_MAX];
    FilePane pane;
    struct stat st;
    FILE *f;
    int i;

    expect(getcwd(cwd, sizeof cwd) != NULL, "getcwd");
    snprintf(src, sizeof src, "%s/src.txt", root);
    snprintf(dst, sizeof dst, "%s/dst.txt", root);
    snprintf(exe, sizeof exe, "%s/tool", root);
    snprintf(copied, sizeof copied, "%s/tool.copy", root);
    snprintf(sub, sizeof sub, "%s/dir", root);
    snprintf(nested, sizeof nested, "%s/dir/child", root);

    f = fopen(src, "w");
    expect(f != NULL, "create src");
    if (f) { fputs("hello", f); fclose(f); }
    f = fopen(dst, "w");
    expect(f != NULL, "create dst");
    if (f) { fputs("keep", f); fclose(f); }
    f = fopen(exe, "w");
    expect(f != NULL, "create exe");
    if (f) { fputs("#!/bin/sh\n", f); fclose(f); }
    chmod(exe, 0755);
    mkdir(sub, 0755);
    f = fopen(nested, "w");
    expect(f != NULL, "create nested");
    if (f) { fputs("n", f); fclose(f); }

    expect(do_copy_recursive(src, dst, NULL, 0) != 0, "no overwrite");
    f = fopen(dst, "r");
    expect(f != NULL, "dst still there");
    if (f) {
        char b[8] = {0};
        fread(b, 1, 4, f);
        fclose(f);
        expect(strcmp(b, "keep") == 0, "dst contents");
    }
    expect(do_copy_recursive(exe, copied, NULL, 1) == 0, "copy exe");
    expect(stat(copied, &st) == 0, "stat copy");
    expect((st.st_mode & 0777) == 0755, "mode preserved");

    expect(do_copy_recursive(sub, nested, NULL, 1) != 0, "no copy into self");
    expect(access(nested, F_OK) == 0, "child survived");

    memset(&pane, 0, sizeof pane);
    copy_str(pane.cwd, sizeof pane.cwd, root);
    expect(load_dir(&pane) == 0, "load_dir");
    expect(pane.list.count >= 2, "has entries");
    expect(pane.list.items[0].is_parent, "parent first");
    for (i = 1; i < (int)pane.list.count; i++)
        expect(!pane.list.items[i].is_parent, "only one parent");

    expect(enter_dir(sub, &pane) == 0, "enter sub");
    expect(strstr(pane.cwd, "/dir") != NULL, "entered sub path");
    {
        char parent[PATH_MAX], back[PATH_MAX];
        expect(path_parent(parent, sizeof parent, pane.cwd) == 0, "parent of sub");
        copy_str(back, sizeof back, pane.cwd);
        expect(enter_dir(parent, &pane) == 0, "enter parent");
        expect(strcmp(pane.cwd, parent) == 0 || strstr(back, pane.cwd) == back,
               "parent is pane parent");
        expect(strstr(pane.cwd, "/dir") == NULL, "left subdir");
    }
    {
        char now[PATH_MAX];
        expect(getcwd(now, sizeof now) != NULL, "getcwd after enter");
        expect(strcmp(now, cwd) == 0, "process cwd unchanged");
    }
    expect(fs_delete("/") != 0, "refuse root");
    expect(fs_delete(src) == 0, "delete file");
    expect(access(src, F_OK) != 0, "file gone");
    expect(fs_delete(sub) == 0, "delete tree");
    expect(access(sub, F_OK) != 0, "tree gone");
    free_list(&pane.list);
}

static int has_bookmark(const char *path)
{
    int i;
    for (i = 0; i < g_app.bookmark_count; i++)
        if (strcmp(g_app.bookmarks[i], path) == 0) return 1;
    return 0;
}

static int touch_file(const char *path)
{
    int fd = open(path, O_CREAT | O_WRONLY | O_EXCL, 0600);
    if (fd < 0) return -1;
    return close(fd) == 0 ? 0 : -1;
}

static void expect_place(const char *home, const char *name, int present)
{
    char path[PATH_MAX];
    expect(make_path(path, sizeof path, home, name) == 0, name);
    expect(has_bookmark(path) == present, name);
}

static int write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    if (fputs(text, f) < 0) {
        fclose(f);
        return -1;
    }
    return fclose(f) == 0 ? 0 : -1;
}

static int home_child(char *out, size_t outsz, const char *home, const char *name)
{
    return make_path(out, outsz, home, name);
}

static void home_mkdir(const char *home, const char *name)
{
    char path[PATH_MAX];
    expect(home_child(path, sizeof path, home, name) == 0, name);
    expect(mkdir(path, 0700) == 0, name);
}

static void prepare_home_tree(const char *home)
{
    char path[PATH_MAX];
    home_mkdir(home, "Документы");
    expect(home_child(path, sizeof path, home, "Видео") == 0, "video");
    expect(touch_file(path) == 0, "video file");
    home_mkdir(home, "real-pics");
    expect(home_child(path, sizeof path, home, "Изображения") == 0, "pics");
    expect(symlink("real-pics", path) == 0, "symlink pics");
    home_mkdir(home, "Рабочий стол");
}

static void expect_named_places(const char *home)
{
    expect_place(home, "Документы", 1);
    expect_place(home, "Изображения", 1);
    expect_place(home, "Рабочий стол", 1);
    expect_place(home, "Видео", 0);
    expect_place(home, "Музыка", 0);
}

static void write_user_dirs(const char *home)
{
    char cfg[PATH_MAX];
    expect(home_child(cfg, sizeof cfg, home, ".config") == 0, "cfg");
    expect(mkdir(cfg, 0700) == 0, "mkdir cfg");
    home_mkdir(home, "custom-dl");
    expect(home_child(cfg, sizeof cfg, home, ".config/user-dirs.dirs") == 0, "ud");
    expect(write_text(cfg,
                      "XDG_DOWNLOAD_DIR=\"$HOME/custom-dl\"\n"
                      "XDG_VIDEOS_DIR=\"$HOME\"\n"
                      "XDG_MUSIC_DIR=\"$HOME/missing\"\n") == 0, "write ud");
}

static void test_named_places(const char *home)
{
    char desk[PATH_MAX];
    int n;
    prepare_home_tree(home);
    expect(home_child(desk, sizeof desk, home, "Рабочий стол") == 0, "desk");
    add_bookmark(desk);
    n = g_app.bookmark_count;
    add_home_places(home);
    expect(g_app.bookmark_count == n + 2, "two new places");
    expect_named_places(home);
}

static void test_xdg_places(const char *home)
{
    write_user_dirs(home);
    add_home_places(home);
    expect_place(home, "custom-dl", 1);
    expect_place(home, "missing", 0);
    expect(!has_bookmark(home), "disabled xdg dir skipped");
}

static void restore_xdg(const char *old)
{
    if (old) setenv("XDG_CONFIG_HOME", old, 1);
    else unsetenv("XDG_CONFIG_HOME");
}

static void test_home_places(void)
{
    char tmpl[] = "/tmp/vibey-home-XXXXXX";
    char *home;
    const char *old = getenv("XDG_CONFIG_HOME");
    home = mkdtemp(tmpl);
    expect(home != NULL, "home mkdtemp");
    if (!home) return;
    setenv("XDG_CONFIG_HOME", "", 1);
    g_app.bookmark_count = 0;
    add_home_places(home);
    expect(g_app.bookmark_count == 0, "empty home adds nothing");
    test_named_places(home);
    test_xdg_places(home);
    restore_xdg(old);
    fs_delete(home);
}

int main(void)
{
    char tmpl[] = "/tmp/vibey-test-XXXXXX";
    char *root;

    setlocale(LC_ALL, "C.UTF-8");

    test_paths();
    test_path_display();
    test_formatting();
    test_exec();
    test_theme();
    g_app.bookmark_count = 0;
    add_bookmark("/tmp/ok");
    expect(g_app.bookmark_count == 1, "bookmark added");
    add_bookmark("/tmp/bad\n[EXECUTION]");
    expect(g_app.bookmark_count == 1, "newline bookmark rejected");
    test_home_places();

    root = mkdtemp(tmpl);
    expect(root != NULL, "mkdtemp");
    if (root) {
        test_fs(root);
        fs_delete(root);
    }

    if (g_fail) {
        fprintf(stderr, "%d failure(s)\n", g_fail);
        return 1;
    }
    puts("ok");
    return 0;
}
