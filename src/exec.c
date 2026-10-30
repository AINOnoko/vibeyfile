#include "fm.h"

/* Возвращает число разобранных аргументов или 0.
 * ВАЖНО: если аргументов в строке больше, чем помещается (max_args - 1 —
 * место под завершающий NULL), лишние МОЛЧА отбрасываются. Вызывающие
 * (action_open_as, action_edit) обязаны ловить это: иначе команда
 * выполнится в усечённом виде. См. fm_argv_truncated() ниже. */
static int argv_truncated;
int fm_argv_truncated(void) { return argv_truncated; }

int tokenize_cmd(const char *cmd, char **argv, int max_args)
{
    char *work, *save = NULL, *tok;
    int n = 0;
    argv_truncated = 0;
    if (!argv) return 0;

    argv[0] = NULL;

    if (!cmd || !*cmd || max_args < 2) return 0;
    work = strdup(cmd);
    if (!work) return 0;
    tok = strtok_r(work, " \t\n\r", &save);
    while (tok && n < max_args - 1) {
        argv[n] = strdup(tok);
        if (!argv[n]) {
            free_argv(argv, n);
            free(work);
            argv[0] = NULL;
            return 0;
        }
        n++;
        tok = strtok_r(NULL, " \t\n\r", &save);
    }
    if (tok != NULL) argv_truncated = 1;    /* остались необработанные токены */
    free(work);
    argv[n] = NULL;
    return n;
}

void free_argv(char **argv, int n)
{
    int i;
    if (!argv || n <= 0) return;
    for (i = 0; i < n; i++) free(argv[i]);
}

/* ФИКС: вызывающие думали, что flushinp() — это tcflush(STDIN, TCIFLUSH),
 * и беспокоились про EINTR. На деле ncurses сбрасывает свой буфер ввода и
 * возвращает ERR, если это не удалось (например, тип терминала не тот).
 * Обёртка делает намерение явным и повторяет попытку один раз. */
void fm_flush_input(void)
{
    if (flushinp() == ERR) flushinp();
}

static void close_extra_fds(void)
{
    int fd;
    long max = sysconf(_SC_OPEN_MAX);
    if (max < 0 || max > 1024) max = 1024;
    for (fd = 3; fd < (int)max; fd++) close(fd);
}

/* В ребёнке перед execvp(). ФИКС (clang-tidy, cert-err33-c): signal()
 * возвращает предыдущий обработчик, а не код ошибки, и вернуть -1 может
 * только на некорректном номере сигнала — проверять нечего, но и молчать
 * не стоит: если SIG_DFL не встал, дочерний процесс унаследует наши
 * обработчики и, например, проигнорирует Ctrl-C. Поэтому результат
 * сверяется с SIG_ERR и, в случае отказа, ребёнок всё равно идёт на
 * execvp: унаследованный обработчик хуже, чем ничего, но отменять запуск
 * из-за этого нельзя (пользователь ждёт редактор, а не диагностику). */
static void child_default_signals(void)
{
    static const int sigs[] = { SIGINT, SIGTERM, SIGHUP,
                                SIGQUIT, SIGTSTP, SIGPIPE };
    size_t i;
    for (i = 0; i < sizeof sigs / sizeof sigs[0]; i++)
        (void)signal(sigs[i], SIG_DFL);
}

void spawn_detached_argv(char *const argv[])
{
    pid_t pid, pid2;
    int devnull;
    /* Двойной fork + setsid: внук не остаётся зомби и отвязан от tty. */
    if (!argv || !argv[0]) return;
    pid = fork();
    if (pid < 0) return;
    if (pid == 0) {
        pid2 = fork();
        if (pid2 < 0) _exit(1);
        if (pid2 > 0) _exit(0);
        setsid();
        child_default_signals();
        devnull = open("/dev/null", O_RDWR | O_CLOEXEC);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > 2) close(devnull);
        }
        close_extra_fds();
        execvp(argv[0], argv);
        _exit(127);
    }
    while (waitpid(pid, NULL, 0) < 0 && errno == EINTR)
        continue;
}

static const int k_job_sigs[] = {
    SIGINT, SIGQUIT, SIGTSTP, SIGTTOU, SIGTTIN
};
#define JOB_SIG_N ((int)(sizeof k_job_sigs / sizeof k_job_sigs[0]))

static void job_sigs_ignore(struct sigaction saved[])
{
    struct sigaction ign;
    int i;
    memset(&ign, 0, sizeof ign);
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    for (i = 0; i < JOB_SIG_N; i++)
        sigaction(k_job_sigs[i], &ign, &saved[i]);
}

static void job_sigs_restore(const struct sigaction saved[])
{
    int i;
    for (i = 0; i < JOB_SIG_N; i++)
        sigaction(k_job_sigs[i], &saved[i], NULL);
}

static void give_tty(pid_t pid)
{
    if (isatty(STDIN_FILENO))
        tcsetpgrp(STDIN_FILENO, pid);
}

static int child_result(int status)
{
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return -2;
    return -1;
}

static void resume_tty(int restore_tty)
{
    if (!restore_tty) return;
    reset_prog_mode();
    refresh();
}

int spawn_sync(const char *file, char *const argv[], int restore_tty, const char *workdir)
{
    struct sigaction saved[JOB_SIG_N];
    pid_t pid;
    int status = 0;
    const char *bin;

    if (!argv || !argv[0]) return -1;
    bin = argv[0][0] ? argv[0] : file;
    if (!bin || !bin[0]) return -1;
    if (restore_tty) {
        def_prog_mode();
        endwin();
    }
    pid = fork();
    if (pid < 0) {
        resume_tty(restore_tty);
        return -1;
    }
    if (pid == 0) {
        setpgid(0, 0);
        give_tty(getpid());
        child_default_signals();
        if (workdir && *workdir && chdir(workdir) != 0) _exit(127);
        /* ФИКС (утечка fd): spawn_detached_argv всегда закрывал лишние
         * дескрипторы, а spawn_sync — нет, и редактор / архиватор /
         * xdg-open наследовали всё, что было открыто в менеджере:
         * ncurses-ввод, дескриптор каталога из opendir, файлы превью.
         * Ребёнок с открытым fd терминала вдобавок мешал отпустить tty. */
        close_extra_fds();
        execvp(bin, argv);
        _exit(127);
    }
    job_sigs_ignore(saved);
    setpgid(pid, pid);
    give_tty(pid);
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            give_tty(getpgrp());
            job_sigs_restore(saved);
            resume_tty(restore_tty);
            return -1;
        }
    }
    give_tty(getpgrp());
    job_sigs_restore(saved);
    resume_tty(restore_tty);
    return child_result(status);
}

/* ═══════════════════════════════════════════════════════════════════════
 * ЭТАП 4: определение «текст / бинарник» по первым 512 байтам — БЕЗ fork.
 *
 * Ключевой момент: наличие NUL не всегда означает «бинарник». UTF-16/UTF-32
 * текст тоже полон нулей, и именно для него раньше вызывался
 * `file --mime-type`. Поэтому сниффер:
 *   1) проверяет BOM (FF FE / FE FF / FF FE 00 00 / 00 00 FE FF);
 *   2) без BOM ищет «структурные» нули (только на чётных/каждой 4-й позиции);
 *   3) и лишь если ни то, ни другое — объявляет файл бинарным.
 * Стоимость: 1 open + 1-2 read + 1 close. Никаких pipe(), fork(), waitpid().
 * ═══════════════════════════════════════════════════════════════════════ */
#define MIME_SNIFF_BYTES 512

/* ФИКС: раньше LE и BE давали одно значение SNIFF_UTF16, хотя комментарии
 * обещали разное. Теперь порядок байт виден в типе: сегодня он нужен только
 * для решения «бинарник/нет», но при транскодировании в просмотрщике различие
 * понадобится, и его не придётся вводить задним числом. */
typedef enum {
    SNIFF_TEXT = 0,
    SNIFF_UTF16_LE, SNIFF_UTF16_BE,
    SNIFF_UTF32_LE, SNIFF_UTF32_BE,
    SNIFF_BINARY
} SniffKind;

static SniffKind sniff_bytes(const unsigned char *b, size_t n)
{
    size_t i, bad = 0, nul = 0;
    if (n == 0) return SNIFF_TEXT;                 /* пустой файл = текст */
    if (n >= 4 && b[0] == 0xFF && b[1] == 0xFE && b[2] == 0x00 && b[3] == 0x00)
        return SNIFF_UTF32_LE;
    if (n >= 4 && b[0] == 0x00 && b[1] == 0x00 && b[2] == 0xFE && b[3] == 0xFF)
        return SNIFF_UTF32_BE;
    if (n >= 2 && b[0] == 0xFF && b[1] == 0xFE) return SNIFF_UTF16_LE;
    if (n >= 2 && b[0] == 0xFE && b[1] == 0xFF) return SNIFF_UTF16_BE;
    for (i = 0; i < n; i++) {
        unsigned char c = b[i];
        if (c == 0) { nul++; continue; }
        if (c == 0x7F || (c < 0x20 && c != '\t' && c != '\n' &&
                          c != '\v' && c != '\f' && c != '\r'))
            bad++;
    }
    if (nul == 0) return (bad * 5 > n) ? SNIFF_BINARY : SNIFF_TEXT;
    if (bad * 5 > n) return SNIFF_BINARY;          /* и мусор, и нули */
    /* Файл из одних нулей (sparse-образ, dd if=/dev/zero) — это НЕ UTF-32,
     * это бинарник: у настоящего UTF-16/32 всегда есть ненулевые байты. */
    if (nul == n) return SNIFF_BINARY;
    /* Нули строго на каждой 2-й или 4-й позиции → UTF-16/32 без BOM */
    {
        size_t k, z_odd = 0, z_even = 0, z_4 = 0;
        for (k = 0; k < n; k++) {
            if (b[k]) continue;
            if (k & 1) z_odd++; else z_even++;
            if ((k & 3) == 3) z_4++;
        }
        /* Нули на нечётных позициях → младший байт первый → LE.
         * На чётных → BE. Ветки больше не дублируются: они дают РАЗНЫЕ
         * значения, поэтому схлопывать их в одно условие нельзя. */
        if (z_odd && z_even == 0) return SNIFF_UTF16_LE;
        if (z_even && z_odd == 0) return SNIFF_UTF16_BE;
        if (z_4 && z_4 * 2 >= n / 2) return SNIFF_UTF32_LE;
    }
    return SNIFF_BINARY;
}

int fm_content_is_binary(const char *path)
{
    unsigned char buf[MIME_SNIFF_BYTES];
    size_t n = 0;
    ssize_t got;
    int fd;
    if (!path) return 1;
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 1;
    while (n < sizeof buf) {
        got = read(fd, buf + n, sizeof buf - n);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        n += (size_t)got;
    }
    close(fd);
    return sniff_bytes(buf, n) == SNIFF_BINARY;
}

int get_mime_type(const char *path, char *out, size_t outsz)
{
    int fds[2], devnull;
    pid_t pid;
    ssize_t total = 0;
    if (!path || !out || outsz < 2) return -1;
    out[0] = '\0';
    /* ФИКС (clang-tidy, android-cloexec-pipe — здесь это не «android», а
     * общий смысл): труба создаётся ДО fork(), и без O_CLOEXEC оба конца
     * наследуются любым процессом, который мы запустим параллельно.
     * Тогда читатель на другом конце не получает EOF, пока жив этот
     * процесс, — классическая взаимоблокировка «ждём вывод, который сам
     * себя держит». */
    if (pipe2(fds, O_CLOEXEC) != 0) return -1;
    pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return -1; }
    if (pid == 0) {
        close(fds[0]);
        if (dup2(fds[1], STDOUT_FILENO) < 0) _exit(127);
        if (fds[1] > 2) close(fds[1]);
        /* O_CLOEXEC: dup2() продублирует на fd 2, а сам дескриптор
         * закроется при exec — иначе у ребёнка оставался лишний дескриптор
         * на /dev/null (на i386 с малым ulimit -n это заметно). */
        devnull = open("/dev/null", O_WRONLY | O_CLOEXEC);
        if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }

        child_default_signals();

        execlp("file", "file", "-b", "--mime-type", "--", path, (char *)NULL);
        _exit(127);
    }
    close(fds[1]);
    while (total < (ssize_t)outsz - 1) {
        ssize_t n = read(fds[0], out + total, outsz - 1 - (size_t)total);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        total += n;
    }
    /* Если буфер забит, дочитываем pipe до EOF, иначе child зависнет на write. */
    if (total >= (ssize_t)outsz - 1) {
        char drain[256];
        ssize_t n;
        do {
            n = read(fds[0], drain, sizeof drain);
        } while (n > 0 || (n < 0 && errno == EINTR));
    }
    close(fds[0]);
    while (waitpid(pid, NULL, 0) < 0 && errno == EINTR)
        continue;
    if (total <= 0) return -1;
    out[total] = '\0';
    while (total > 0 && (out[total - 1] == '\n' || out[total - 1] == '\r'))
        out[--total] = '\0';
    return 0;
}
