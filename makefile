# ──────────────────────────────────────────────────────────────
#  VibeyFile — терминальный файловый менеджер
#  Структура: src/ → build/obj/ → build/bin/
# ──────────────────────────────────────────────────────────────
#  make            — release-сборка
#  make test       — unit-тесты
#  make debug      — отладочная сборка
#  make sanitize   — ASan + UBSan + LeakSanitizer
#  make valgrind   — Valgrind на debug-бинарнике
#  make check      — release + test + debug + sanitize
#  make run        — запуск
#  make clean      — очистка
#  make help       — справка
# ──────────────────────────────────────────────────────────────

CC        ?= gcc
SRC_DIR   := src
OBJ_DIR   := build/obj
BIN_DIR   := build/bin
TEST_DIR  := tests

HDR       := $(SRC_DIR)/fm.h
SRC       := $(wildcard $(SRC_DIR)/*.c)
OBJ       := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRC))
DEP       := $(OBJ:.o=.d)
OBJ_NO_MAIN := $(filter-out $(OBJ_DIR)/main.o,$(OBJ))

BIN       := $(BIN_DIR)/VibeyFile
BIN_DBG   := $(BIN_DIR)/VibeyFile-debug
BIN_SAN   := $(BIN_DIR)/VibeyFile-asan
BIN_TEST  := $(BIN_DIR)/test_core

# ncursesw через pkg-config, с fallback на -lncursesw
NCURSES_CFLAGS := $(shell pkg-config --cflags ncursesw 2>/dev/null)
NCURSES_LIBS   := $(or $(shell pkg-config --libs ncursesw 2>/dev/null),-lncursesw)

CPPFLAGS  += -I$(SRC_DIR) $(NCURSES_CFLAGS)
LDLIBS    += $(NCURSES_LIBS)

BASE_CFLAGS := -std=c11 -Wall -Wextra -Werror \
               -Wshadow -Wpointer-arith -Wcast-qual \
               -Wstrict-prototypes -Wmissing-prototypes \
               -Wno-unused-parameter
REL_CFLAGS  := $(BASE_CFLAGS) -O2 -DNDEBUG
DBG_CFLAGS  := $(BASE_CFLAGS) -O0 -g3 -ggdb -DDEBUG
SAN_CFLAGS  := $(BASE_CFLAGS) -O1 -g -fno-omit-frame-pointer \
               -fsanitize=address,undefined,leak \
               -fno-sanitize-recover=all
SAN_LDFLAGS := -fsanitize=address,undefined,leak
# Тесты — мягкие флаги (без -Werror/-Wmissing-prototypes)
TEST_CFLAGS := -std=c11 -Wall -Wextra -O2

.PHONY: all release test debug sanitize valgrind check run clean help

all: release

# ── Release ────────────────────────────────────────────────
release: $(BIN)

$(BIN): $(OBJ) | $(BIN_DIR)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(HDR) | $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(REL_CFLAGS) -MMD -MP -c -o $@ $<

-include $(DEP)

# ── Unit-тесты ─────────────────────────────────────────────
test: $(BIN_TEST)
	./$(BIN_TEST)

$(BIN_TEST): $(TEST_DIR)/test_core.c $(OBJ_NO_MAIN) $(HDR) | $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -o $@ \
		$(TEST_DIR)/test_core.c $(OBJ_NO_MAIN) $(LDFLAGS) $(LDLIBS)

# ── Debug ──────────────────────────────────────────────────
debug: $(BIN_DBG)

$(BIN_DBG): $(SRC) $(HDR) | $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(DBG_CFLAGS) -o $@ $(SRC) $(LDLIBS)

# ── Sanitizers ─────────────────────────────────────────────
sanitize: $(BIN_SAN)

$(BIN_SAN): $(SRC) $(HDR) | $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(SAN_CFLAGS) $(SAN_LDFLAGS) -o $@ $(SRC) $(LDLIBS)

# ── Valgrind ───────────────────────────────────────────────
valgrind: $(BIN_DBG)
	@echo "── Valgrind ──"
	valgrind --leak-check=full --show-leak-kinds=all \
		--track-origins=yes \
		--errors-for-leak-kinds=definite,indirect,possible \
		--error-exitcode=42 ./$(BIN_DBG)

check: clean release test debug sanitize
	@echo
	@echo "✔ release / test / debug / sanitize прошли"
	@echo "  Valgrind — отдельно: make valgrind"

run: release
	./$(BIN)

# ── Каталоги и очистка ─────────────────────────────────────
$(OBJ_DIR) $(BIN_DIR):
	mkdir -p $@

clean:
	rm -rf build
	rm -rf *.dSYM

help:
	@echo "Доступные цели:"
	@echo "  make            — release-сборка → build/bin/VibeyFile"
	@echo "  make test       — unit-тесты    → build/bin/test_core"
	@echo "  make debug      — debug-сборка   → build/bin/VibeyFile-debug"
	@echo "  make sanitize   — ASan/UBSan/LSan→ build/bin/VibeyFile-asan"
	@echo "  make valgrind   — Valgrind на debug"
	@echo "  make check      — release+test+debug+sanitize"
	@echo "  make run        — запуск release"
	@echo "  make clean      — удалить артефакты"
	@echo "  make help       — эта справка"
