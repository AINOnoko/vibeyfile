#include "test.h"

void test_path_display(void)
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

void test_paths(void)
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

void test_formatting(void)
{
    char buf[64];

    format_size(buf, sizeof buf, 0);
    expect(strcmp(buf, "0B") == 0, "size 0");
    format_size(buf, sizeof buf, 512);
    expect(strcmp(buf, "512B") == 0, "size 512");
    format_size(buf, sizeof buf, 1024);
    expect(strcmp(buf, "1.0K") == 0, "size 1K");
    format_size(buf, sizeof buf, 1024 * 1024 * 2);
    expect(strcmp(buf, "2.0M") == 0, "size 2M");

    format_mode(buf, sizeof buf, 0755);
    expect(strcmp(buf, "rwxr-xr-x") == 0, "mode 755");
    format_mode(buf, sizeof buf, 0644);
    expect(strcmp(buf, "rw-r--r--") == 0, "mode 644");
    format_mode(buf, sizeof buf, 0700);
    expect(strcmp(buf, "rwx------") == 0, "mode 700");

    build_padded(buf, sizeof buf, "abc", 5, 0);
    expect(strcmp(buf, "abc  ") == 0, "pad left");
    build_padded(buf, sizeof buf, "abc", 5, 1);
    expect(strcmp(buf, "  abc") == 0, "pad right");
    build_padded(buf, sizeof buf, "abcdef", 3, 0);
    expect(strcmp(buf, "abc") == 0, "pad truncate");
}
