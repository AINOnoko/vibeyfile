#include "fm.h"

int tokenize_cmd(const char *cmd, char **argv, int max_args)
{
    char *work, *save = NULL, *tok;
    int n = 0;
    if (!argv || !cmd || !*cmd || max_args < 2) return 0;
    work = strdup(cmd);
    if (!work) return 0;
    tok = strtok_r(work, " \t\n\r", &save);
    while (tok && n < max_args - 1) {
        argv[n] = strdup(tok);
        if (!argv[n]) break;
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

static void ignore_signal(int sig, struct sigaction *old)
{
    struct sigaction ign;
    memset(&ign, 0, sizeof ign);
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    sigaction(sig, &ign, old);
}

int run_foreground(char *const argv[], const char *workdir)
{
    pid_t pid;
    int status = 0;
    struct sigaction old_int, old_term, old_hup, old_quit, old_tstp;
    if (!argv || !argv[0]) return -1;
    ignore_signal(SIGINT, &old_int);
    ignore_signal(SIGTERM, &old_term);
    ignore_signal(SIGHUP, &old_hup);
    ignore_signal(SIGQUIT, &old_quit);
    ignore_signal(SIGTSTP, &old_tstp);
    def_prog_mode();
    endwin();
    pid = fork();
    if (pid < 0) {
        sigaction(SIGINT, &old_int, NULL);
        sigaction(SIGTERM, &old_term, NULL);
        sigaction(SIGHUP, &old_hup, NULL);
        sigaction(SIGQUIT, &old_quit, NULL);
        sigaction(SIGTSTP, &old_tstp, NULL);
        reset_prog_mode();
        refresh();
        return -1;
    }
    if (pid == 0) {
        setpgid(0, 0);
        if (isatty(STDIN_FILENO))
            tcsetpgrp(STDIN_FILENO, getpid());
        child_default_signals();
        if (workdir && *workdir && chdir(workdir) != 0) _exit(127);
        execvp(argv[0], argv);
        _exit(127);
    }
    setpgid(pid, pid);
    if (isatty(STDIN_FILENO))
        tcsetpgrp(STDIN_FILENO, pid);
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        continue;
    if (isatty(STDIN_FILENO))
        tcsetpgrp(STDIN_FILENO, getpgrp());
    sigaction(SIGINT, &old_int, NULL);
    sigaction(SIGTERM, &old_term, NULL);
    sigaction(SIGHUP, &old_hup, NULL);
    sigaction(SIGQUIT, &old_quit, NULL);
    sigaction(SIGTSTP, &old_tstp, NULL);
    reset_prog_mode();
    refresh();
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
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
    close(fds[0]);
    while (waitpid(pid, NULL, 0) < 0 && errno == EINTR)
        continue;
    if (total <= 0) return -1;
    out[total] = '\0';
    while (total > 0 && (out[total - 1] == '\n' || out[total - 1] == '\r'))
        out[--total] = '\0';
    return 0;
}
