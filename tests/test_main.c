#include "test.h"

int g_test_fail;

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
    test_bookmark_validation();
    test_home_places();

    test_audit_type_name_capacity();
    test_audit_utf8_truncate();
    test_audit_locale_formats();
    test_audit_shipped_locale_clean();
    test_audit_spawn_messages();
    test_audit_single_sniffer();
    test_audit_tokenize_truncation();
    test_audit_locale_default();
    test_audit_t_fallback_readable();
    test_audit_name_ok_portable();
    test_audit_theme_field_table();
    test_audit_history_numbers();
    test_audit_long_home();
    test_audit_fmt_arity_zero();

    root = mkdtemp(tmpl);
    expect(root != NULL, "mkdtemp");
    if (root) {
        test_fs(root);
        test_audit_enter_dir_long_path(root);
        fs_delete(root);
    }

    if (g_test_fail) {
        fprintf(stderr, "%d failure(s)\n", g_test_fail);
        return 1;
    }
    puts("ok");
    return 0;
}
