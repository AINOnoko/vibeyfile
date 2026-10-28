#include "fm.h"

int tokenize_cmd(const char *cmd, char **argv, int max_args)
{
    char *work, *save = NULL, *tok;
    int n = 0;
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

static void close_extra_fds(void)
{
    int fd;
    long max = sysconf(_SC_OPEN_MAX);
    if (max < 0 || max > 1024) max = 1024;
    for (fd = 3; fd < (int)max; fd++) close(fd);
}

static void child_default_signals(void)
{
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGHUP, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGPIPE, SIG_DFL);
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
        devnull = open("/dev/null", O_RDWR);
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

int get_mime_type(const char *path, char *out, size_t outsz)
{
    int fds[2], devnull;
    pid_t pid;
    ssize_t total = 0;
    if (!path || !out || outsz < 2) return -1;
    out[0] = '\0';
    if (pipe(fds) != 0) return -1;
    pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return -1; }
    if (pid == 0) {
        close(fds[0]);
        if (dup2(fds[1], STDOUT_FILENO) < 0) _exit(127);
        if (fds[1] > 2) close(fds[1]);
        devnull = open("/dev/null", O_WRONLY);
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
