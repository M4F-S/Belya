/* syscall() and O_PATH are GNU extensions; the project builds strict C99. */
#define _GNU_SOURCE
#include "sandbox.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/landlock.h>
#include <stdint.h>
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
 * break git, curl and package managers, so 80/443 stay open unless the operator
 * overrides it. Inbound listening is deliberately not handled: the agent starts
 * local servers on purpose and that is not an egress risk. */
#define SB_MAX_ALLOW_PORTS 8
#define SB_DEFAULT_PORTS "80,443"

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
