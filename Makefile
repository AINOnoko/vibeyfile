# ──────────────────────────────────────────────────────────────
#  VibeyFile — терминальный файловый менеджер
#  Ориентирован на i386 / low-RAM embedded Linux.
# ──────────────────────────────────────────────────────────────

CC        ?= gcc
SRC_DIR   := src
OBJ_DIR   := build/obj
BIN_DIR   := build/bin
TEST_DIR  := tests

PREFIX    ?= /usr
ETCDIR    ?= /etc/vibeyfile
BINDIR    := $(DESTDIR)$(PREFIX)/bin

HDR       := $(SRC_DIR)/fm.h
SRC       := $(wildcard $(SRC_DIR)/*.c)
OBJ       := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRC))
DEP       := $(OBJ:.o=.d)
OBJ_NO_MAIN := $(filter-out $(OBJ_DIR)/main.o,$(OBJ))

# fuzz.c — отдельный бинарник со своим main: он не должен попадать
# в общий список, иначе линковка падает на двух main.
FUZZ_SRC  := $(TEST_DIR)/fuzz.c
TEST_SRC  := $(filter-out $(FUZZ_SRC),$(wildcard $(TEST_DIR)/*.c))
TEST_HDR  := $(wildcard $(TEST_DIR)/*.h)

BIN       := $(BIN_DIR)/VibeyFile
BIN_DBG   := $(BIN_DIR)/VibeyFile-debug
BIN_SAN   := $(BIN_DIR)/VibeyFile-asan
BIN_TEST  := $(BIN_DIR)/test_core

NCURSES_CFLAGS := $(shell pkg-config --cflags ncursesw 2>/dev/null)
NCURSES_LIBS   := $(or $(shell pkg-config --libs ncursesw 2>/dev/null),-lncursesw)

CPPFLAGS  += -I$(SRC_DIR) $(NCURSES_CFLAGS)
LDLIBS    += $(NCURSES_LIBS)

BASE_CFLAGS := -std=c11 -Wall -Wextra -Werror \
               -Wshadow -Wpointer-arith -Wcast-qual \
               -Wstrict-prototypes -Wmissing-prototypes \
               -Wno-unused-parameter

# ── ЭТАП 5: размер вместо скорости ─────────────────────────────────────────
#  -Os        : оптимизация по размеру; на i386 с 8-32 МБ ОЗУ важнее I-cache
#               и отсутствие лишних прологов/эпилогов, чем -O2.
#  -ffunction-sections -fdata-sections : каждая функция/данные в своей секции,
#               чтобы линковщик мог выбросить неиспользуемое.
#  -fno-asynchronous-unwind-tables : .eh_frame для C не нужен (бэктрейсы —
#               в debug-сборке), экономит 5-10% .text.
#  -fno-ident  : убирает .comment с версией GCC (воспроизводимость сборки).
REL_CFLAGS  := $(BASE_CFLAGS) -Os -DNDEBUG \
               -ffunction-sections -fdata-sections \
               -fno-asynchronous-unwind-tables -fno-ident
REL_LDFLAGS := -Wl,--gc-sections -Wl,--build-id=none -Wl,-O1

DBG_CFLAGS  := $(BASE_CFLAGS) -O0 -g3 -ggdb -DDEBUG
SAN_CFLAGS  := $(BASE_CFLAGS) -O1 -g -fno-omit-frame-pointer \
               -fsanitize=address,undefined,leak \
               -fno-sanitize-recover=all
SAN_LDFLAGS := -fsanitize=address,undefined,leak
TEST_CFLAGS := -std=c11 -Wall -Wextra -O2   # тесты гоняем на -O2: скорость

.PHONY: all release size test debug sanitize valgrind check run clean help \
        install setup-local

all: release

# ── Release ────────────────────────────────────────────────
release: $(BIN)

$(BIN): $(OBJ) | $(BIN_DIR)
	$(CC) $(LDFLAGS) $(REL_LDFLAGS) -o $@ $(OBJ) $(LDLIBS)
	@# ЭТАП 5: снимаем оставшиеся символы для embedded-образа
	@-strip --strip-unneeded $@ 2>/dev/null || true
	@size $@ 2>/dev/null || true

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(HDR) | $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(REL_CFLAGS) -MMD -MP -c -o $@ $<

-include $(DEP)

# ── Установка ──────────────────────────────────────────────
install: release
	@echo "── Установка в $(DESTDIR)$(PREFIX) и $(DESTDIR)$(ETCDIR) ──"
	mkdir -p $(BINDIR)
	mkdir -p $(DESTDIR)$(ETCDIR)/local
	install -m 755 $(BIN) $(BINDIR)/VibeyFile
	cp -r defaultconfig/local/* $(DESTDIR)$(ETCDIR)/local/
	install -m 644 defaultconfig/config $(DESTDIR)$(ETCDIR)/config

setup-local: release
	@echo "── Настройка локального конфига в ~/.config/vibeyfile ──"
	mkdir -p $(HOME)/.config/vibeyfile/local
	cp -r defaultconfig/local/* $(HOME)/.config/vibeyfile/local/
	test -f $(HOME)/.config/vibeyfile/config || cp defaultconfig/config $(HOME)/.config/vibeyfile/config
	@echo "Готово."

# ── Unit-тесты ─────────────────────────────────────────────
# compile_commands.json для clang-tidy / clangd: проекта на CMake нет,
# поэтому базу генерирует сам make — из тех же флагов, что и release.
CC_DB_DIR := cc
CC_DB     := $(CC_DB_DIR)/compile_commands.json

cc-db:
	@mkdir -p $(CC_DB_DIR)
	@python3 tools/gen_cc_db.py > $(CC_DB) && echo "  cc-db           — $(CC_DB)"

test: $(BIN_TEST)
	./$(BIN_TEST)

$(BIN_TEST): $(TEST_SRC) $(TEST_HDR) $(OBJ_NO_MAIN) $(HDR) | $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -I$(TEST_DIR) -o $@ $(TEST_SRC) $(OBJ_NO_MAIN) $(LDFLAGS) $(LDLIBS)

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

run: release
	./$(BIN)

# ── Каталоги и очистка ─────────────────────────────────────
$(OBJ_DIR) $(BIN_DIR):
	mkdir -p $@

# ── Размер бинарника (ЭТАП 5) ──────────────────────────────
size: release
	@echo "── размер секций $(BIN) ──"
	@size -A $(BIN) 2>/dev/null || size $(BIN)
	@echo
	@echo "── топ-20 самых больших функций ──"
	@nm --size-sort -S $(BIN) 2>/dev/null | tail -20 || true

clean:
	rm -rf build
	rm -rf *.dSYM

help:
	@echo "Доступные цели:"
	@echo "  make            — release-сборка → build/bin/VibeyFile"
	@echo "  make setup-local— копирование дефолтного конфига в ~/.config/vibeyfile"
	@echo "  make install    — установка в /usr/bin и /etc/vibeyfile (нужен sudo)"
	@echo "  make test       — unit-тесты    → build/bin/test_core"
	@echo "  make debug      — debug-сборка   → build/bin/VibeyFile-debug"
	@echo "  make sanitize   — ASan/UBSan/LSan→ build/bin/VibeyFile-asan"
	@echo "  make valgrind   — Valgrind на debug"
	@echo "  make check      — release+test+debug+sanitize"
	@echo "  make run        — запуск release"
	@echo "  make size       — размер секций release-бинарника"
	@echo "  make clean      — удалить артефакты"
	@echo "  make help       — эта справка"
