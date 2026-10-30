#!/usr/bin/env python3
"""Генерирует compile_commands.json из тех же флагов, что и `make release`.

Проект собирается Makefile'ом, CMake нет, поэтому `clang-tidy -p=build`
не работает (и `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` — не аргумент
clang-tidy, он отвечает «Unknown command line argument»). Этот скрипт
даёт анализатору ровно ту же командную строку, что и настоящая сборка:
без этого clang-tidy компилирует файлы без -I и с другим набором
-D, и часть диагностик просто не воспроизводится.
"""
import glob
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FLAGS = [
    "-std=c11", "-Wall", "-Wextra",
    "-Wshadow", "-Wpointer-arith", "-Wcast-qual",
    "-Wstrict-prototypes", "-Wmissing-prototypes",
    "-Wno-unused-parameter",
    "-I" + os.path.join(ROOT, "src"),
    "-D_DEFAULT_SOURCE", "-D_XOPEN_SOURCE=600",
]

def main() -> int:
    entries = []
    for src in sorted(glob.glob(os.path.join(ROOT, "src", "*.c"))):
        entries.append({
            "directory": ROOT,
            "file": src,
            "arguments": ["cc", *FLAGS, "-c", src, "-o", "/dev/null"],
        })
    json.dump(entries, sys.stdout, indent=1)
    sys.stdout.write("\n")
    return 0

if __name__ == "__main__":
    sys.exit(main())
