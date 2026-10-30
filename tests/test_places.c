#include "test.h"

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
    if (fputs(text, f) < 0) { fclose(f); return -1; }
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

void test_bookmark_validation(void)
{
    g_app.bookmark_count = 0;
    add_bookmark("/tmp/ok");
    expect(g_app.bookmark_count == 1, "bookmark added");
    add_bookmark("/tmp/bad\n[EXECUTION]");
    expect(g_app.bookmark_count == 1, "newline bookmark rejected");
}

void test_home_places(void)
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
