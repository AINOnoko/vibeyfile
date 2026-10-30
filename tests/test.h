#ifndef TEST_H
#define TEST_H

#include "fm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern int g_test_fail;

#define expect(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg); \
        g_test_fail++; \
    } \
} while (0)

void test_paths(void);
void test_path_display(void);
void test_formatting(void);

void test_exec(void);

void test_theme(void);

void test_fs(const char *root);

void test_bookmark_validation(void);
void test_home_places(void);

void test_audit_type_name_capacity(void);
void test_audit_utf8_truncate(void);
void test_audit_locale_formats(void);
void test_audit_enter_dir_long_path(const char *root);
void test_audit_shipped_locale_clean(void);
void test_audit_spawn_messages(void);
void test_audit_single_sniffer(void);
void test_audit_tokenize_truncation(void);
void test_audit_locale_default(void);
void test_audit_t_fallback_readable(void);
void test_audit_name_ok_portable(void);
void test_audit_theme_field_table(void);
void test_audit_history_numbers(void);
void test_audit_long_home(void);
void test_audit_fmt_arity_zero(void);

#endif
