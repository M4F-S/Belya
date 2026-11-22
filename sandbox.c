/* syscall() and O_PATH are GNU extensions; the project builds strict C99. */
#define _GNU_SOURCE
#include "sandbox.h"

#ifdef __linux__

#include <errno.h>
#include <fcntl.h>
#include <linux/landlock.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef SYS_landlock_create_ruleset
#define SYS_landlock_create_ruleset 444
#endif
#ifndef SYS_landlock_add_rule
#define SYS_landlock_add_rule 445
#endif
#ifndef SYS_landlock_restrict_self
#define SYS_landlock_restrict_self 446
#endif

/* Rights that let a process change the filesystem. Reads and exec are absent on
 * purpose: a compiler still needs to read /usr/include, and blocking that would
 * break every build to gain nothing. */
#define SB_WRITE_RIGHTS                                                        \
    (LANDLOCK_ACCESS_FS_WRITE_FILE | LANDLOCK_ACCESS_FS_REMOVE_DIR |           \
     LANDLOCK_ACCESS_FS_REMOVE_FILE | LANDLOCK_ACCESS_FS_MAKE_CHAR |           \
     LANDLOCK_ACCESS_FS_MAKE_DIR | LANDLOCK_ACCESS_FS_MAKE_REG |               \
     LANDLOCK_ACCESS_FS_MAKE_SOCK | LANDLOCK_ACCESS_FS_MAKE_FIFO |             \
     LANDLOCK_ACCESS_FS_MAKE_BLOCK | LANDLOCK_ACCESS_FS_MAKE_SYM |             \
     LANDLOCK_ACCESS_FS_REFER | LANDLOCK_ACCESS_FS_TRUNCATE)

/* Landlock ABI 4 added TCP connect control. Denying every port outright would
 * break git, curl and package managers, so 22/80/443 stay open unless the
 * operator overrides it. 22 is included because the agent pushes over SSH with
 * a deploy key; excluding it would silently break every deploy path while 443
 * remains available for exfiltration anyway. Inbound listening is deliberately not handled: the agent starts
 * local servers on purpose and that is not an egress risk. */
#define SB_MAX_ALLOW_PORTS 8
#define SB_DEFAULT_PORTS "22,80,443"

static size_t parse_ports(unsigned short *ports, size_t max) {
    const char *env = getenv("BELYA_SANDBOX_ALLOW_PORTS");
    if (!env || !*env) env = SB_DEFAULT_PORTS;
    size_t n = 0;
    const char *p = env;
    while (*p && n < max) {
        char *end = NULL;
        long v = strtol(p, &end, 10);
        if (end == p) break;
        if (v > 0 && v < 65536) ports[n++] = (unsigned short)v;
        while (*end == ',' || *end == ' ') end++;
        p = end;
    }
    return n;
}

static int ll_create_ruleset(const struct landlock_ruleset_attr *attr, size_t size, uint32_t flags) {
    return (int)syscall(SYS_landlock_create_ruleset, attr, size, flags);
}

static int ll_add_rule(int fd, enum landlock_rule_type type, const void *attr, uint32_t flags) {
    return (int)syscall(SYS_landlock_add_rule, fd, type, attr, flags);
}

static int ll_restrict(int fd, uint32_t flags) {
    return (int)syscall(SYS_landlock_restrict_self, fd, flags);
}

int sandbox_supported(void) {
    int abi = ll_create_ruleset(NULL, 0, LANDLOCK_CREATE_RULESET_VERSION);
    return abi > 0;
}

/* The ABI the kernel actually offers, or -1 when the syscall is absent or
 * blocked. Only probed on the failure path, so the confined case pays nothing. */
static int ll_abi(void) {
    int abi = ll_create_ruleset(NULL, 0, LANDLOCK_CREATE_RULESET_VERSION);
    return abi > 0 ? abi : -1;
}

int sandbox_allow_unconfined(int apply_rc, int abi) {
    if (apply_rc == 0) return 0; /* confined */
    if (apply_rc > 0) return 1;  /* no boundary requested, or none possible */
    /* Refused by the kernel: tolerable only where Landlock does not exist. */
    return abi > 0 ? 0 : 1;
}

void sandbox_confine_child(const char *workspace) {
    int rc = sandbox_apply(workspace);
    if (rc == 0) return;

    int abi = ll_abi();
    if (sandbox_allow_unconfined(rc, abi)) {
        /* rc > 0: no boundary was requested or possible; -1 with no ABI: nothing
           to enforce. Stay quiet here: this runs in the child with stderr wired
           to the tool's output pipe, so a line per command would land in every
           tool result. The harness reports the missing boundary once at startup. */
        return;
    }

    fprintf(stderr,
            "[sandbox] FATAL: Landlock (ABI %d) is available but refused to confine "
            "this child; refusing to run it unconfined\n",
            abi);
    _exit(126);
}

/* Plan mode. Read everywhere, write only /tmp and /dev/null.
 *
 * History: this used to live in belya_harness.c with every landlock_* return
 * value ignored and LANDLOCK_ACCESS_FS_TRUNCATE missing from the ruleset, so
 * `truncate(2)` on any readable file succeeded while the code believed the
 * process was sandboxed. Measured: create/rename/unlink refused, truncate
 * allowed. Each step is checked now and the caller refuses to run on -1. */
int sandbox_apply_plan_mode(void) {
    int abi = ll_abi();
    struct landlock_ruleset_attr ra;
    memset(&ra, 0, sizeof ra);
    ra.handled_access_fs =
        LANDLOCK_ACCESS_FS_EXECUTE | LANDLOCK_ACCESS_FS_WRITE_FILE |
        LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR |
        LANDLOCK_ACCESS_FS_REMOVE_DIR | LANDLOCK_ACCESS_FS_REMOVE_FILE |
        LANDLOCK_ACCESS_FS_MAKE_CHAR | LANDLOCK_ACCESS_FS_MAKE_DIR |
        LANDLOCK_ACCESS_FS_MAKE_REG | LANDLOCK_ACCESS_FS_MAKE_SOCK |
        LANDLOCK_ACCESS_FS_MAKE_FIFO | LANDLOCK_ACCESS_FS_MAKE_BLOCK |
        LANDLOCK_ACCESS_FS_MAKE_SYM;
#if defined(LANDLOCK_ACCESS_FS_TRUNCATE)
    /* ABI 3 added TRUNCATE. Asking for it on an older kernel makes
     * landlock_create_ruleset() fail with EINVAL, so gate it on the ABI. */
    if (abi >= 3) ra.handled_access_fs |= LANDLOCK_ACCESS_FS_TRUNCATE;
#else
    if (abi >= 3) return -1; /* kernel enforces TRUNCATE, headers cannot express it */
#endif
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) return -1;

    int rfd = ll_create_ruleset(&ra, sizeof ra, 0);
    if (rfd < 0) return -1;

    int root_fd = open("/", O_PATH | O_CLOEXEC);
    if (root_fd < 0) { close(rfd); return -1; }
    struct landlock_path_beneath_attr root_ro = {
        .allowed_access = LANDLOCK_ACCESS_FS_READ_FILE |
                          LANDLOCK_ACCESS_FS_READ_DIR |
                          LANDLOCK_ACCESS_FS_EXECUTE,
        .parent_fd = root_fd,
    };
    int rc = ll_add_rule(rfd, LANDLOCK_RULE_PATH_BENEATH, &root_ro, 0);
    close(root_fd);
    if (rc != 0) { close(rfd); return -1; }

    int tmp_fd = open("/tmp", O_PATH | O_CLOEXEC);
    if (tmp_fd < 0) { close(rfd); return -1; }
    struct landlock_path_beneath_attr tmp_rw = {
        .allowed_access = ra.handled_access_fs,
        .parent_fd = tmp_fd,
    };
    rc = ll_add_rule(rfd, LANDLOCK_RULE_PATH_BENEATH, &tmp_rw, 0);
    close(tmp_fd);
    if (rc != 0) { close(rfd); return -1; }

    int null_fd = open("/dev/null", O_PATH | O_CLOEXEC);
    if (null_fd >= 0) {
        struct landlock_path_beneath_attr null_rw = {
            .allowed_access = LANDLOCK_ACCESS_FS_READ_FILE |
                              LANDLOCK_ACCESS_FS_WRITE_FILE,
            .parent_fd = null_fd,
        };
        (void)ll_add_rule(rfd, LANDLOCK_RULE_PATH_BENEATH, &null_rw, 0);
        close(null_fd);
    }

    if (ll_restrict(rfd, 0) != 0) { close(rfd); return -1; }
    close(rfd);
    return 0;
}

static int allow_path(int ruleset_fd, const char *path, uint64_t rights) {
    int fd = open(path, O_PATH | O_CLOEXEC);
    if (fd < 0) return -1;
    struct landlock_path_beneath_attr pb;
    memset(&pb, 0, sizeof pb);
    pb.allowed_access = rights;
    pb.parent_fd = fd;
    int rc = ll_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH, &pb, 0);
    close(fd);
    return rc;
}

int sandbox_apply(const char *workspace) {
    const char *off = getenv("BELYA_SANDBOX");
    if (off && off[0] == '0') return 1;

    int abi = ll_create_ruleset(NULL, 0, LANDLOCK_CREATE_RULESET_VERSION);

    struct landlock_ruleset_attr attr;
    memset(&attr, 0, sizeof attr);
    attr.handled_access_fs = SB_WRITE_RIGHTS;
#ifdef LANDLOCK_ACCESS_NET_CONNECT_TCP
    int handle_net = (abi >= 4);
    if (handle_net) attr.handled_access_net = LANDLOCK_ACCESS_NET_CONNECT_TCP;
#else
    int handle_net = 0;
#endif

    int ruleset = ll_create_ruleset(&attr, sizeof attr, 0);
    if (ruleset < 0 && handle_net) {
        /* Kernel headers newer than the running kernel: fall back to fs-only
         * rather than refusing to run the command at all. */
        attr.handled_access_net = 0;
        handle_net = 0;
        ruleset = ll_create_ruleset(&attr, sizeof attr, 0);
    }
    if (ruleset < 0) {
        /* ENOSYS/EOPNOTSUPP: old kernel or a container without the LSM. The
         * caller still runs the command - say so rather than pretend. */
        return (errno == ENOSYS || errno == EOPNOTSUPP || errno == EINVAL) ? 1 : -1;
    }

    if (workspace && *workspace) allow_path(ruleset, workspace, SB_WRITE_RIGHTS);
    allow_path(ruleset, "/tmp", SB_WRITE_RIGHTS);
    /* /dev/null and friends are opened for write by ordinary shell redirection. */
    allow_path(ruleset, "/dev", LANDLOCK_ACCESS_FS_WRITE_FILE);

#ifdef LANDLOCK_ACCESS_NET_CONNECT_TCP
    if (handle_net) {
        unsigned short ports[SB_MAX_ALLOW_PORTS];
        size_t np = parse_ports(ports, SB_MAX_ALLOW_PORTS);
        for (size_t i = 0; i < np; i++) {
            struct landlock_net_port_attr na;
            memset(&na, 0, sizeof na);
            na.allowed_access = LANDLOCK_ACCESS_NET_CONNECT_TCP;
            na.port = ports[i];
            /* A port rule the kernel refuses just stays closed. */
            (void)ll_add_rule(ruleset, LANDLOCK_RULE_NET_PORT, &na, 0);
        }
    }
#endif

    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        close(ruleset);
        return -1;
    }
    if (ll_restrict(ruleset, 0) != 0) {
        close(ruleset);
        return -1;
    }
    close(ruleset);
    return 0;
}

#else /* Non-Linux (macOS, BSD, Windows) */

int sandbox_supported(void) {
    return 0;
}

int sandbox_apply(const char *workspace) {
    (void)workspace;
    return 1; /* 1 = unsupported (caller continues unconfined) */
}

/* No Linux kernel boundary here; the caller keeps its own platform sandbox. */
int sandbox_apply_plan_mode(void) {
    return 1;
}

int sandbox_allow_unconfined(int apply_rc, int abi) {
    (void)abi;
    return apply_rc != 0 ? 1 : 0;
}

void sandbox_confine_child(const char *workspace) {
    (void)workspace; /* no kernel boundary on this platform; nothing to refuse */
}

#endif
