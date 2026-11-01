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
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
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


/* A listener on loopback, ephemeral port; returns fd and the port. */
static int listener_on(int *port) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    if (bind(s, (struct sockaddr *)&a, sizeof a) != 0) { close(s); return -1; }
    socklen_t alen = sizeof a;
    if (getsockname(s, (struct sockaddr *)&a, &alen) != 0) { close(s); return -1; }
    if (listen(s, 4) != 0) { close(s); return -1; }
    *port = ntohs(a.sin_port);
    return s;
}

static int try_connect(int port) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -errno;
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int rc = connect(s, (struct sockaddr *)&a, sizeof a);
    int err = errno;
    close(s);
    return rc == 0 ? 0 : -err;
}

/* Egress: one port allowed, another refused. Testing only the refusal would pass
 * just as well if the whole network stack were broken. */
static int test_network(const char *ws) {
    int allowed_port = 0, denied_port = 0;
    int la = listener_on(&allowed_port);
    int ld = listener_on(&denied_port);
    if (la < 0 || ld < 0) {
        printf("  [SKIP] cannot open loopback listeners\n");
        if (la >= 0) close(la);
        if (ld >= 0) close(ld);
        return 0;
    }
    char ports[32];
    snprintf(ports, sizeof ports, "%d", allowed_port);
    setenv("BELYA_SANDBOX_ALLOW_PORTS", ports, 1);

    pid_t pid = fork();
    if (pid == 0) {
        if (sandbox_apply(ws) != 0) _exit(20);
        if (try_connect(allowed_port) != 0) _exit(21);   /* allowed port refused */
        int rc = try_connect(denied_port);
        if (rc == 0) _exit(22);                          /* denied port let through */
        if (rc != -EACCES && rc != -EPERM) _exit(23);    /* failed for the wrong reason */
        _exit(0);
    }
    close(la);
    close(ld);
    unsetenv("BELYA_SANDBOX_ALLOW_PORTS");

    int status = 0;
    waitpid(pid, &status, 0);
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    if (code == 0) {
        printf("  [ok] allowed port reachable, other ports refused by the kernel\n");
        return 0;
    }
    printf("  [FAIL] network case exited %d (20=apply 21=allowed port blocked "
           "22=denied port reachable 23=wrong errno)\n", code);
    return 1;
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

    fails += test_network(ws);

    char inside[256];
    snprintf(inside, sizeof inside, "%s/probe.txt", ws);
    unlink(inside);
    rmdir(ws);

    printf("  sandbox: %s (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
    return fails ? 1 : 0;
}
