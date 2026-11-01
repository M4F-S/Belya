/* tests/test_sandbox.c - Landlock confinement of tool subprocesses.
 *
 * The classifier answers "should this run"; this answers "what can it touch".
 * Test forks a child that confines itself, then checks the two things that
 * matter: a write inside the workspace succeeds, a write outside it is refused
 * by the kernel rather than by string matching.
 */
#include "sandbox.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int try_write(const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return -errno;
    if (write(fd, "x", 1) != 1) {
        close(fd);
        return -EIO;
    }
    close(fd);
    return 0;
}

int main(void) {
    int fails = 0;
    printf("[Test] Landlock write confinement\n");

    if (!sandbox_supported()) {
        printf("  [SKIP] Landlock unavailable on this kernel\n");
        return 0;
    }

    char ws[] = "/tmp/belya_sandbox_wsXXXXXX";
    if (!mkdtemp(ws)) {
        printf("  [FAIL] cannot create workspace\n");
        return 1;
    }

    pid_t pid = fork();
    if (pid == 0) {
        int rc = sandbox_apply(ws);
        if (rc != 0) _exit(10);

        char inside[256];
        snprintf(inside, sizeof inside, "%s/probe.txt", ws);
        if (try_write(inside) != 0) _exit(11);

        char outside[] = "/tmp/../etc/belya_sandbox_probe.txt";
        if (try_write(outside) == 0) _exit(12);

        if (try_write("/root/belya_sandbox_probe.txt") == 0) _exit(13);

        /* reads must still work: a build needs headers */
        int fd = open("/etc/hostname", O_RDONLY);
        if (fd >= 0) close(fd);
        _exit(0);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    if (code == 0) printf("  [ok] workspace write allowed, outside writes refused\n");
    else {
        printf("  [FAIL] child exited %d (11=ws write blocked 12=/etc write allowed "
               "13=/root write allowed 10=apply failed)\n", code);
        fails++;
    }

    char inside[256];
    snprintf(inside, sizeof inside, "%s/probe.txt", ws);
    unlink(inside);
    rmdir(ws);

    printf("  sandbox: %s (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
    return fails ? 1 : 0;
}
