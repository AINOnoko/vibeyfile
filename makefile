# ──────────────────────────────────────────────────────────────
#  fm — терминальный файловый менеджер
# ──────────────────────────────────────────────────────────────
#  Обычная сборка:      make
#  Debug (с -g):        make debug
#  Санитайзеры:         make sanitize
#  Проверка Valgrind:   make valgrind
#  Всё сразу:           make check
#  Запуск:              make run
#  Очистка:             make clean
#  Справка:             make help
# ──────────────────────────────────────────────────────────────

CC       ?= gcc
SRC      := fm.c
BIN      := fm
BIN_DBG  := fm-debug
BIN_SAN  := fm-asan

# Базовые флаги, общие для всех режимов
BASE_CFLAGS := -std=c11 -Wall -Wextra -Werror \
               -Wshadow -Wpointer-arith -Wcast-qual \
               -Wstrict-prototypes -Wmissing-prototypes \
               -Wno-unused-parameter

# Release
REL_CFLAGS  := $(BASE_CFLAGS) -O2 -DNDEBUG

# Debug: отладочные символы, без оптимизации, с assert()
DBG_CFLAGS  := $(BASE_CFLAGS) -O0 -g3 -ggdb -DDEBUG

# Санитайзеры: Address + Undefined Behavior + Leak
SAN_CFLAGS  := $(BASE_CFLAGS) -O1 -g -fno-omit-frame-pointer \
               -fsanitize=address,undefined,leak \
               -fno-sanitize-recover=all
SAN_LDFLAGS := -fsanitize=address,undefined,leak

LDLIBS      := -lncursesw

# ──────────────────────────────────────────────────────────────

.PHONY: all release debug sanitize valgrind check run clean help

all: release

release: $(BIN)

$(BIN): $(SRC)
	$(CC) $(REL_CFLAGS) -o $@ $< $(LDLIBS)

debug: $(BIN_DBG)

$(BIN_DBG): $(SRC)
	$(CC) $(DBG_CFLAGS) -o $@ $< $(LDLIBS)

sanitize: $(BIN_SAN)

$(BIN_SAN): $(SRC)
	$(CC) $(SAN_CFLAGS) $(SAN_LDFLAGS) -o $@ $< $(LDLIBS)

valgrind: $(BIN_DBG)
	@echo "── Valgrind ──"
	valgrind \
		--leak-check=full \
		--show-leak-kinds=all \
		--track-origins=yes \
		--errors-for-leak-kinds=definite,indirect,possible \
		--error-exitcode=42 \
		./$(BIN_DBG)

check: clean release debug sanitize valgrind
	@echo
	@echo "✔ Все проверки пройдены"

run: release
	./$(BIN)

clean:
	rm -f $(BIN) $(BIN_DBG) $(BIN_SAN)
	rm -rf *.dSYM

help:
	@echo "Доступные цели:"
	@echo "  make            — release-сборка (-O2, без отладки)"
	@echo "  make debug      — отладочная сборка (-O0 -g3 -DDEBUG)"
	@echo "  make sanitize   — сборка с ASan + UBSan + LeakSanitizer"
	@echo "  make valgrind   — запуск Valgrind на debug-бинарнике"
	@echo "  make check      — clean + release + debug + sanitize + valgrind"
	@echo "  make run        — запуск release-бинарника"
	@echo "  make clean      — удалить артефакты"
	@echo "  make help       — эта справка"
