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

    expect(do_copy_recursive(src, dst, NULL) != 0, "no overwrite");
    f = fopen(dst, "r");
    expect(f != NULL, "dst still there");
    if (f) {
        char b[8] = {0};
        fread(b, 1, 4, f);
        fclose(f);
        expect(strcmp(b, "keep") == 0, "dst contents");
    }
    expect(do_copy_recursive(exe, copied, NULL) == 0, "copy exe");
    expect(stat(copied, &st) == 0, "stat copy");
    expect((st.st_mode & 0777) == 0755, "mode preserved");

    expect(do_copy_recursive(sub, nested, NULL) != 0, "no copy into self");
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

int main(void)
{
    char tmpl[] = "/tmp/vibey-test-XXXXXX";
    char *root;
    setlocale(LC_ALL, "C.UTF-8");
    test_paths();
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
