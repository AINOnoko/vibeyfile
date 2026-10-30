#include "test.h"

void test_exec(void)
{
    char *argv[8];

    int n = tokenize_cmd("ls -l /tmp", argv, 8);
    expect(n == 3, "tokenize count");
    expect(strcmp(argv[0], "ls") == 0, "token 1");
    expect(strcmp(argv[1], "-l") == 0, "token 2");
    expect(strcmp(argv[2], "/tmp") == 0, "token 3");
    expect(argv[3] == NULL, "token null terminator");
    free_argv(argv, n);

    n = tokenize_cmd("", argv, 8);
    expect(n == 0, "tokenize empty");
    expect(argv[0] == NULL, "token empty null");
    free_argv(argv, n);

    n = tokenize_cmd("   echo   hello   ", argv, 8);
    expect(n == 2, "tokenize spaces count");
    expect(strcmp(argv[0], "echo") == 0, "token spaces 1");
    expect(strcmp(argv[1], "hello") == 0, "token spaces 2");
    free_argv(argv, n);
}
