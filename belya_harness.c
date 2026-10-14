#include "common.h"
#include "belya_harness.h"
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <dirent.h>
#include <signal.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <fnmatch.h>
#include <strings.h>
#include <regex.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <curl/curl.h>

#ifdef __linux__
#include <sys/syscall.h>
#include <linux/landlock.h>
#include <sys/prctl.h>
#ifndef landlock_create_ruleset
static inline int landlock_create_ruleset(const struct landlock_ruleset_attr *const attr, const size_t size, const __u32 flags) {
    return (int)syscall(__NR_landlock_create_ruleset, attr, size, flags);
}
static inline int landlock_add_rule(const int ruleset_fd, const enum landlock_rule_type rule_type, const void *const rule_attr, const __u32 flags) {
    return (int)syscall(__NR_landlock_add_rule, ruleset_fd, rule_type, rule_attr, flags);
}
static inline int landlock_restrict_self(const int ruleset_fd, const __u32 flags) {
    return (int)syscall(__NR_landlock_restrict_self, ruleset_fd, flags);
}
#endif
#endif

static BelyaHarness *g_harness = NULL;
const char *g_active_custom_script_path = NULL;

#ifdef __APPLE__
extern int sandbox_init(const char *profile, uint64_t flags, char **errorbuf);
extern void sandbox_free_error(char *errorbuf);
#endif

static void apply_plan_mode_kernel_sandbox(void) {
#ifdef __linux__
#if defined(LANDLOCK_ACCESS_FS_EXECUTE) && defined(PR_SET_NO_NEW_PRIVS)
    prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
    struct landlock_ruleset_attr ruleset_attr = {
        .handled_access_fs =
            LANDLOCK_ACCESS_FS_EXECUTE | LANDLOCK_ACCESS_FS_WRITE_FILE |
            LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR |
            LANDLOCK_ACCESS_FS_REMOVE_DIR | LANDLOCK_ACCESS_FS_REMOVE_FILE |
            LANDLOCK_ACCESS_FS_MAKE_CHAR | LANDLOCK_ACCESS_FS_MAKE_DIR |
            LANDLOCK_ACCESS_FS_MAKE_REG | LANDLOCK_ACCESS_FS_MAKE_SOCK |
            LANDLOCK_ACCESS_FS_MAKE_FIFO | LANDLOCK_ACCESS_FS_MAKE_BLOCK |
            LANDLOCK_ACCESS_FS_MAKE_SYM
    };
    int ruleset_fd = landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
    if (ruleset_fd >= 0) {
        int root_fd = open("/", O_PATH | O_CLOEXEC);
        if (root_fd >= 0) {
            struct landlock_path_beneath_attr root_ro = {
                .allowed_access = LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR | LANDLOCK_ACCESS_FS_EXECUTE,
                .parent_fd = root_fd,
            };
            landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH, &root_ro, 0);
            close(root_fd);
        }
        int tmp_fd = open("/tmp", O_PATH | O_CLOEXEC);
        if (tmp_fd >= 0) {
            struct landlock_path_beneath_attr tmp_rw = {
                .allowed_access = ruleset_attr.handled_access_fs,
                .parent_fd = tmp_fd,
            };
            landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH, &tmp_rw, 0);
            close(tmp_fd);
        }
        int null_fd = open("/dev/null", O_PATH | O_CLOEXEC);
        if (null_fd >= 0) {
            struct landlock_path_beneath_attr null_rw = {
                .allowed_access = LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_WRITE_FILE,
                .parent_fd = null_fd,
            };
            landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH, &null_rw, 0);
            close(null_fd);
        }
        landlock_restrict_self(ruleset_fd, 0);
        close(ruleset_fd);
    }
#endif
#elif defined(__APPLE__)
    char *errbuf = NULL;
    const char *profile =
        "(version 1)(allow default)(deny file-write*)"
        "(allow file-write* (subpath \"/tmp\") (subpath \"/private/tmp\") (literal \"/dev/null\"))";
    if (sandbox_init(profile, 0, &errbuf) != 0 && errbuf) {
        sandbox_free_error(errbuf);
    }
#endif
}

static void safe_pipe_write(int fd, const char *data, size_t len) {
    while (len > 0) {
        ssize_t w = write(fd, data, len);
        if (w <= 0) break;
        data += w;
        len -= (size_t)w;
    }
}

static inline bool is_safe_shell_path(const char *p) {
    if (!p || p[0] == '\0') return false;
    for (const char *s = p; *s; s++) {
        if (*s == '"' || *s == '\'' || *s == '$' || *s == '`' ||
            *s == ';' || *s == '&' || *s == '|' || *s == '\n' || *s == '\r') {
            return false;
        }
    }
    return true;
}

static char *preflight_syntax_check(const char *path) {
    if (!path || !is_safe_shell_path(path)) return NULL;
    const char *ext = strrchr(path, '.');
    if (!ext) return NULL;
    if (strcmp(ext, ".c") != 0 && strcmp(ext, ".h") != 0 &&
        strcmp(ext, ".cpp") != 0 && strcmp(ext, ".cc") != 0) {
        return NULL;
    }

    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "gcc -fsyntax-only -std=c99 -Wall \"%s\" 2>&1", path);

    FILE *p = popen(cmd, "r");
    if (!p) return NULL;

    DynString raw_out = dyn_str_new();
    char buf[512];
    while (fgets(buf, sizeof(buf), p)) {
        dyn_str_append(&raw_out, buf);
        if (raw_out.len > 8000) break;
    }
    int status = pclose(p);

    if (status == 0 || raw_out.len == 0) {
        dyn_str_free(&raw_out);
        return NULL;
    }

    DynString diag = dyn_str_new();
    dyn_str_appendf(&diag, "%s\n\n[Structured Compiler Diagnostics]\n{\n  \"file\": \"%s\",\n  \"status\": \"error\",\n  \"raw_output\": ", raw_out.data, path);
    dyn_str_append_escaped(&diag, raw_out.data);
    dyn_str_append(&diag, "\n}");
    dyn_str_free(&raw_out);

    return diag.data;
}

bool belya_harness_is_tool_whitelisted(const char *whitelist, const char *tool_name) {
    if (!tool_name) return false;
    if (!whitelist || strlen(whitelist) == 0 || strcmp(whitelist, "*") == 0 || strcmp(whitelist, "all") == 0) {
        return true;
    }

    char *wl_copy = xstrdup(whitelist);
    if (!wl_copy) return false;

    bool matched = false;
    char *token_ctx = NULL;
    char *tok = strtok_r(wl_copy, ",; \t\r\n", &token_ctx);
    while (tok) {
        while (*tok == ' ' || *tok == '\t') tok++;
        char *end = tok + strlen(tok) - 1;
        while (end >= tok && isspace((unsigned char)*end)) *end-- = '\0';

        if (strcasecmp(tok, tool_name) == 0) {
            matched = true;
            break;
        }
        // Common tool aliases
        if ((strcasecmp(tok, "grep_search") == 0 && strcmp(tool_name, "search_files") == 0) ||
            (strcasecmp(tok, "find_by_name") == 0 && (strcmp(tool_name, "list_dir") == 0 || strcmp(tool_name, "search_files") == 0)) ||
            (strcasecmp(tok, "replace_file_content") == 0 && strcmp(tool_name, "edit_file") == 0)) {
            matched = true;
            break;
        }
        tok = strtok_r(NULL, ",; \t\r\n", &token_ctx);
    }
    free(wl_copy);
    return matched;
}

// Built-in Native Tools

static char *tool_bash(BelyaAgent *agent, const JsonValue *args) {
    (void)agent;
    const char *cmd = json_obj_get_str(args, "command");
    if (!cmd) return xstrdup("Error: Missing command argument.");

    // Plan Mode Read-Only Shell Guard
    if (g_harness && g_harness->plan_mode) {
        if (strstr(cmd, "rm ") || strstr(cmd, "mv ") || strstr(cmd, "cp ") ||
            strstr(cmd, "mkdir ") || strstr(cmd, "git commit") || strstr(cmd, "git push") ||
            strstr(cmd, "sed -i") || strstr(cmd, ">") || strstr(cmd, ">>") ||
            strstr(cmd, "chmod ") || strstr(cmd, "chown ")) {
            return xstrdup("Blocked: Plan Mode is active (read-only execution). Shell commands with write/delete mutations are prohibited. Call exit_plan_mode first.");
        }
    }

    // 2026 Competitive Parity Phase 5: Persistent PTY Background Task Execution
    const JsonValue *bg_val = json_obj_get(args, "background");
    bool background = (bg_val && bg_val->type == JSON_BOOL) ? bg_val->u.boolean : false;
    if (background && g_harness) {
        int tid = belya_harness_spawn_task(g_harness, cmd);
        if (tid > 0) {
            DynString res = dyn_str_new();
            dyn_str_appendf(&res, "Command launched in background PTY subshell [Task ID: %d]. Use manage_task(action=\"status\", id=%d) to monitor output.", tid, tid);
            return res.data;
        } else {
            return xstrdup("Error: Failed to spawn background task (task table full or PTY allocation failed).");
        }
    }

    // Tester Restricted Execution Guard: allow only build/test/read/verify targets.
    // H3 fix: substring/prefix matching was bypassable (e.g. "echo;rm -rf /",
    // "ls&&curl x|sh", any command containing "make test"). Now: reject shell
    // metacharacters, then exact-match argv[0] against a read-only allowlist.
    if (g_harness && g_harness->bash_restricted) {
        const char *t = cmd;
        while (*t == ' ' || *t == '\t') t++;
        bool allowed = false;
        if (strpbrk(t, ";|&$`\\<>(){}") == NULL && strchr(t, '\n') == NULL) {
            static const char *allowed_cmds[] = {
                "make", "./belya_test", "./almaz_test", "gcc", "clang", "ctest",
                "git", "cat", "ls", "pwd", "echo", "grep", "find", "wc", "diff",
                "head", "tail", "sort", "uniq", "md5sum", "sha256sum", "file", "stat",
                NULL
            };
            char first[128];
            size_t n = 0;
            while (t[n] && !isspace((unsigned char)t[n]) && n < sizeof(first) - 1) {
                first[n] = t[n];
                n++;
            }
            first[n] = '\0';
            for (int i = 0; allowed_cmds[i]; i++) {
                if (strcmp(first, allowed_cmds[i]) == 0) {
                    allowed = true;
                    break;
                }
            }
        }
        if (!allowed) {
            DynString err = dyn_str_new();
            dyn_str_appendf(&err, "Error: Tester role bash execution is restricted to build/test validation targets (e.g. make test, make, ./belya_test). Unauthorized command rejected: '%s'", cmd);
            return err.data;
        }
    }

    // 2026 Competitive Parity Phase 7: Stdin-Piped Modular Remote Execution Backend (Docker / SSH)
    if (g_harness && g_harness->exec_backend != BELYA_BACKEND_LOCAL) {
        int exit_code = 0;
        char *remote_res = belya_harness_exec_remote(g_harness, cmd, &exit_code);
        return remote_res;
    }

    // Handle cd built-in directly to maintain working directory persistence across tool invocations.
    // Also supports compound commands starting with cd (e.g. "cd /dir && make test" or "cd /dir; git status").
    while (cmd && (strncmp(cmd, "cd ", 3) == 0 || strcmp(cmd, "cd") == 0)) {
        const char *after_cd = strlen(cmd) > 3 ? cmd + 3 : "";
        while (*after_cd == ' ') after_cd++;

        // Find separator (&& or ;) outside of quotes
        const char *sep = NULL;
        size_t sep_len = 0;
        char quote = 0;
        for (const char *p = after_cd; *p; p++) {
            if (*p == '\'' || *p == '"') {
                if (quote == 0) quote = *p;
                else if (quote == *p) quote = 0;
            } else if (quote == 0) {
                if (*p == ';') {
                    sep = p;
                    sep_len = 1;
                    break;
                } else if (*p == '&' && *(p + 1) == '&') {
                    sep = p;
                    sep_len = 2;
                    break;
                }
            }
        }

        char target_dir[4096];
        if (sep) {
            size_t tlen = sep - after_cd;
            if (tlen >= sizeof(target_dir)) tlen = sizeof(target_dir) - 1;
            memcpy(target_dir, after_cd, tlen);
            target_dir[tlen] = '\0';
        } else {
            snprintf(target_dir, sizeof(target_dir), "%s", after_cd);
        }

        // Trim trailing whitespace from target
        size_t t_end = strlen(target_dir);
        while (t_end > 0 && isspace((unsigned char)target_dir[t_end - 1])) {
            target_dir[--t_end] = '\0';
        }

        const char *dest = target_dir;
        if (strlen(dest) == 0) dest = getenv("HOME");
        if (!dest) dest = ".";

        char clean_path[4096];
        size_t dlen = strlen(dest);
        if (dlen >= 2 && ((dest[0] == '"' && dest[dlen-1] == '"') || (dest[0] == '\'' && dest[dlen-1] == '\''))) {
            size_t copy_len = (dlen - 2 < sizeof(clean_path) - 1) ? (dlen - 2) : (sizeof(clean_path) - 1);
            memcpy(clean_path, dest + 1, copy_len);
            clean_path[copy_len] = '\0';
        } else {
            snprintf(clean_path, sizeof(clean_path), "%s", dest);
        }

        char resolved[8192];
        if (clean_path[0] == '/' || !g_harness || strlen(g_harness->cwd) == 0) {
            snprintf(resolved, sizeof(resolved), "%s", clean_path);
        } else {
            snprintf(resolved, sizeof(resolved), "%s/%s", g_harness->cwd, clean_path);
        }

        if (chdir(resolved) != 0) {
            DynString err_ds = dyn_str_new();
            dyn_str_appendf(&err_ds, "Error: Failed to change directory to '%s': %s", clean_path, strerror(errno));
            return err_ds.data;
        }

        if (g_harness) {
            if (!getcwd(g_harness->cwd, sizeof(g_harness->cwd))) {
                snprintf(g_harness->cwd, sizeof(g_harness->cwd), "%.*s", (int)(sizeof(g_harness->cwd)-1), resolved);
            }
        }

        // If this was a standalone cd (no separator or empty remainder), return directory change notice
        if (!sep) {
            if (g_harness && strlen(g_harness->cwd) > 0) {
                DynString res = dyn_str_new();
                dyn_str_appendf(&res, "Changed working directory to: %s", g_harness->cwd);
                return res.data;
            }
            return xstrdup("Directory changed successfully.");
        }

        // Advance past separator and whitespace for the remaining command
        const char *remainder = sep + sep_len;
        while (*remainder == ' ') remainder++;
        if (strlen(remainder) == 0) {
            DynString res = dyn_str_new();
            dyn_str_appendf(&res, "Changed working directory to: %s", g_harness ? g_harness->cwd : resolved);
            return res.data;
        }

        cmd = remainder;
    }

    DynString full_cmd_ds = dyn_str_new();
    dyn_str_append(&full_cmd_ds, "export PAGER=cat SYSTEMD_PAGER=cat LESS=-R && ");
    if (g_harness && strlen(g_harness->cwd) > 0) {
        dyn_str_appendf(&full_cmd_ds, "cd \"%s\" && %s", g_harness->cwd, cmd);
    } else {
        dyn_str_append(&full_cmd_ds, cmd);
    }

    DynString out = dyn_str_new();
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        dyn_str_free(&full_cmd_ds);
        return xstrdup("Error: Pipe creation failed.");
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        dyn_str_free(&full_cmd_ds);
        return xstrdup("Error: Subprocess fork failed.");
    }

    if (pid == 0) {
        // Child process
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        if (g_harness && g_harness->plan_mode) {
            apply_plan_mode_kernel_sandbox();
        }
        execl("/bin/sh", "sh", "-c", full_cmd_ds.data, (char *)NULL);
        _exit(127);
    }

    // Parent process
    dyn_str_free(&full_cmd_ds);
    close(pipefd[1]);

    // Set non-blocking read on pipe
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

    struct timeval start, now;
    gettimeofday(&start, NULL);
    bool timed_out = false;

    const char *to_env = getenv("BASH_TIMEOUT");
    double max_timeout = (to_env && atof(to_env) > 0) ? atof(to_env) : 30.0;

    while (1) {
        char buf[512];
        ssize_t bytes = read(pipefd[0], buf, sizeof(buf) - 1);
        if (bytes > 0) {
            buf[bytes] = '\0';
            dyn_str_append(&out, buf);
            if (out.len > 100000) {
                dyn_str_append(&out, "\n[Output Truncated by Belya Harness (100KB buffer limit)]");
                /* H6: reap the child instead of leaking a zombie bypassing timeout. */
                kill(pid, SIGKILL);
                waitpid(pid, NULL, 0);
                break;
            }
        }

        int status;
        pid_t res = waitpid(pid, &status, WNOHANG);
        if (res == pid) {
            while ((bytes = read(pipefd[0], buf, sizeof(buf) - 1)) > 0) {
                buf[bytes] = '\0';
                dyn_str_append(&out, buf);
                if (out.len > 100000) break;
            }
            break;
        }

        gettimeofday(&now, NULL);
        double elapsed = (now.tv_sec - start.tv_sec) + (now.tv_usec - start.tv_usec) / 1000000.0;
        if (elapsed > max_timeout) {
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
            timed_out = true;
            break;
        }

        struct timespec ts = {0, 10000000L};
        nanosleep(&ts, NULL);
    }
    close(pipefd[0]);

    if (timed_out) {
        dyn_str_appendf(&out, "\n[Process killed: Execution exceeded %.0fs timeout]", max_timeout);
    }

    if (out.len == 0) dyn_str_append(&out, "Command executed with no output.");
    return out.data;
}

static char g_workspace_root[4096];
static bool g_workspace_root_set = false;

/* Secrets and operator state that file tools must never expose or mutate. */
static bool is_secret_path(const char *path) {
    if (!path || path[0] == '\0') return false;
    if (strstr(path, "/.ssh/") != NULL || strstr(path, "/.ssh") != NULL ||
        strncmp(path, ".ssh/", 5) == 0 || strcmp(path, ".ssh") == 0 ||
        strstr(path, "/.git/") != NULL || strstr(path, "/.git") != NULL ||
        strncmp(path, ".git/", 5) == 0 || strcmp(path, ".git") == 0 ||
        strstr(path, "/.aws/") != NULL || strstr(path, "/.aws") != NULL ||
        strncmp(path, ".aws/", 5) == 0 || strcmp(path, ".aws") == 0 ||
        strstr(path, "/.config/gcloud/") != NULL || strncmp(path, ".config/gcloud/", 15) == 0) {
        return true;
    }
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (strcmp(base, ".env") == 0 || strncmp(base, ".env.", 5) == 0) return true;
    if (strcmp(base, "budget_state.txt") == 0 || strcmp(base, ".deploy_state") == 0) return true;
    if (strcmp(base, "id_rsa") == 0 || strcmp(base, "id_ed25519") == 0 || strcmp(base, "id_ecdsa") == 0) return true;
    if (strcmp(base, "credentials.json") == 0 || strcmp(base, "service_account.json") == 0) return true;
    if (strcmp(base, "belya_memory.sqlite") == 0 || strcmp(base, "almaz_memory.sqlite") == 0 ||
        strcmp(base, "belya_memory.sqlite-wal") == 0 || strcmp(base, "almaz_memory.sqlite-wal") == 0) {
        return true;
    }
    return false;
}

bool belya_harness_is_secret_path(const char *path) {
    return is_secret_path(path);
}

/* H2: names used to build file paths (custom tools, skills) must be safe
   single path components: [A-Za-z0-9_-]{1,64}. */
static bool valid_ident(const char *s) {
    if (!s) return false;
    size_t n = 0;
    for (; *s; s++, n++) {
        unsigned char c = (unsigned char)*s;
        if (!(isalnum(c) || c == '_' || c == '-')) return false;
        if (n >= 64) return false;
    }
    return n > 0 && n <= 64;
}

static bool whitelist_match(const char *resolved, const char *wl) {
    size_t wlen = strlen(wl);
    return strncmp(resolved, wl, wlen) == 0 && (resolved[wlen] == '\0' || resolved[wlen] == '/');
}

bool is_path_jailed(const char *path, const char *workspace_root, bool is_write) {
    if (!path || path[0] == '\0') return false;

    // Reject paths with explicit directory traversal
    if (strstr(path, "..") != NULL) return false;

    // Never expose or write secrets/operator state through file tools
    if (is_secret_path(path)) return false;

    // Determine and canonicalize workspace root
    char root_buf[4096];
    char canonical_root[4096];
    if (workspace_root && workspace_root[0] != '\0') {
        strncpy(root_buf, workspace_root, sizeof(root_buf) - 1);
        root_buf[sizeof(root_buf) - 1] = '\0';
    } else if (g_workspace_root_set) {
        strncpy(root_buf, g_workspace_root, sizeof(root_buf) - 1);
        root_buf[sizeof(root_buf) - 1] = '\0';
    } else {
        if (!getcwd(root_buf, sizeof(root_buf))) return false;
    }

    if (!realpath(root_buf, canonical_root)) {
        snprintf(canonical_root, sizeof(canonical_root), "%s", root_buf);
    }
    size_t root_len = strlen(canonical_root);

    // If writing: strictly forbidden outside workspace root
    if (is_write) {
        char resolved[4096];
        if (realpath(path, resolved)) {
            if (strncmp(resolved, canonical_root, root_len) == 0 &&
                (resolved[root_len] == '/' || resolved[root_len] == '\0')) {
                return true;
            }
            return false;
        }

        // File doesn't exist yet: verify its parent directory is within workspace root
        char temp_path[4096];
        strncpy(temp_path, path, sizeof(temp_path) - 1);
        temp_path[sizeof(temp_path) - 1] = '\0';

        char *last_slash = strrchr(temp_path, '/');
        if (!last_slash) {
            // Relative path in current working directory (which is inside workspace root)
            return true;
        }

        if (last_slash == temp_path) {
            // Root directory '/'
            return false;
        }

        *last_slash = '\0';
        if (realpath(temp_path, resolved)) {
            if (strncmp(resolved, canonical_root, root_len) == 0 &&
                (resolved[root_len] == '/' || resolved[root_len] == '\0')) {
                return true;
            }
            return false;
        }

        if (strncmp(temp_path, canonical_root, root_len) == 0 &&
            (temp_path[root_len] == '/' || temp_path[root_len] == '\0')) {
            return true;
        }
        return false;
    }

    // If reading: allow within workspace, or check explicit whitelist
    char resolved[4096];
    if (realpath(path, resolved)) {
        if (strncmp(resolved, canonical_root, root_len) == 0 &&
            (resolved[root_len] == '/' || resolved[root_len] == '\0')) {
            return true;
        }

        static const char *read_whitelist[] = {
            "/tmp/",
            "/private/tmp/",
            "/proc/self/status",
            "/proc/self/cmdline",
            "/proc/cpuinfo",
            "/proc/meminfo",
            "/proc/loadavg",
            "/proc/uptime",
            "/dev/null",
            "/etc/os-release",
            NULL
        };
        for (int i = 0; read_whitelist[i]; i++) {
            if (whitelist_match(resolved, read_whitelist[i])) {
                return true;
            }
        }
        return false;
    }

    // Path does not exist on disk yet
    if (path[0] != '/') {
        return true;
    }

    if (strncmp(path, canonical_root, root_len) == 0 &&
        (path[root_len] == '/' || path[root_len] == '\0')) {
        return true;
    }

    static const char *read_whitelist[] = {
        "/tmp/",
        "/private/tmp/",
        "/proc/self/status",
        "/proc/self/cmdline",
        "/proc/cpuinfo",
        "/proc/meminfo",
        "/proc/loadavg",
        "/proc/uptime",
        "/dev/null",
        "/etc/os-release",
        NULL
    };
    for (int i = 0; read_whitelist[i]; i++) {
        if (whitelist_match(path, read_whitelist[i])) {
            return true;
        }
    }

    return false;
}

bool is_path_safe(const char *path) {
    return is_path_jailed(path, NULL, false);
}

static char *tool_read_file(BelyaAgent *agent, const JsonValue *args) {
    (void)agent;
    const char *path = json_obj_get_str(args, "path");
    if (!path) return xstrdup("Error: Missing file path argument.");
    if (!is_path_jailed(path, NULL, false)) return xstrdup("Error: Path traversal denied.");

    double offset_num = json_obj_get_num(args, "offset", 0);
    double limit_num = json_obj_get_num(args, "limit", 0);

    /* Clamp model-controlled line numbers before casting (M5: avoid UB). */
    if (offset_num < 0) offset_num = 0;
    if (limit_num < 0) limit_num = 0;
    if (offset_num > 100000000) offset_num = 100000000;
    if (limit_num > 1000000) limit_num = 1000000;

    FILE *f = fopen(path, "rb");
    if (!f) return xstrdup("Error: Target file not found or inaccessible.");

    if (offset_num > 0 || limit_num > 0) {
        size_t start_line = offset_num > 0 ? (size_t)offset_num : 1;
        size_t max_lines = limit_num > 0 ? (size_t)limit_num : 200;

        DynString ds = dyn_str_new();
        char line_buf[4096];
        size_t current_line = 1;
        size_t collected = 0;

        while (fgets(line_buf, sizeof(line_buf), f)) {
            if (current_line >= start_line && collected < max_lines) {
                dyn_str_appendf(&ds, "%5zu | %s", current_line, line_buf);
                collected++;
                if (ds.len > 100000) {
                    dyn_str_append(&ds, "\n[Output Truncated by Line Limit]");
                    break;
                }
            }
            current_line++;
            if (current_line >= start_line + max_lines) break;
        }
        fclose(f);

        if (ds.len == 0) {
            dyn_str_appendf(&ds, "No lines found in specified range (%zu to %zu).", start_line, start_line + max_lines - 1);
        }
        return ds.data;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz < 0 || sz > 200000) {
        fclose(f);
        return xstrdup("Error: File exceeds maximum safe context read size (200KB) or cannot determine size. Use offset and limit parameters.");
    }

    char *buf = malloc(sz + 1);
    if (!buf) {
        fclose(f);
        return xstrdup("Error: Memory allocation failed reading file.");
    }
    size_t read_bytes = fread(buf, 1, sz, f);
    buf[read_bytes] = '\0';
    fclose(f);
    return buf;
}

static char *tool_write_file(BelyaAgent *agent, const JsonValue *args) {
    (void)agent;
    if (g_harness && g_harness->plan_mode) {
        return xstrdup("Blocked: Plan Mode is active (read-only execution). Use exit_plan_mode to perform modifications.");
    }
    const char *path = json_obj_get_str(args, "path");
    const char *content = json_obj_get_str(args, "content");
    if (!path || !content) return xstrdup("Error: Missing path or content argument.");
    if (!is_path_jailed(path, NULL, true)) return xstrdup("Error: Path traversal denied.");

    FILE *f = fopen(path, "wb");
    if (!f) return xstrdup("Error: Failed to open path for writing.");

    size_t clen = strlen(content);
    fwrite(content, 1, clen, f);
    fclose(f);

    DynString msg = dyn_str_new();
    dyn_str_append(&msg, "File successfully written to disk.");
    char *diag = preflight_syntax_check(path);
    if (diag) {
        dyn_str_append(&msg, "\n\n⚠️ COMPILER WARNING/ERROR after write:\n");
        dyn_str_append(&msg, diag);
        free(diag);
    }
    return msg.data;
}

static char *tool_edit_file(BelyaAgent *agent, const JsonValue *args) {
    (void)agent;
    if (g_harness && g_harness->plan_mode) {
        return xstrdup("Blocked: Plan Mode is active (read-only execution). Use exit_plan_mode to perform modifications.");
    }
    const char *path = json_obj_get_str(args, "path");
    const char *old_text = json_obj_get_str(args, "old_text");
    const char *new_text = json_obj_get_str(args, "new_text");

    if (!path || !old_text || !new_text) {
        return xstrdup("Error: Missing required parameters (path, old_text, new_text).");
    }
    if (!is_path_jailed(path, NULL, true)) return xstrdup("Error: Path traversal denied.");

    FILE *f = fopen(path, "rb");
    if (!f) return xstrdup("Error: Target file not found or inaccessible.");

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz < 0 || sz > 500000) {
        fclose(f);
        return xstrdup("Error: File exceeds maximum safe edit size (500KB) or cannot determine size.");
    }

    char *content = malloc(sz + 1);
    if (!content) {
        fclose(f);
        return xstrdup("Error: Memory allocation failed.");
    }
    size_t read_bytes = fread(content, 1, sz, f);
    content[read_bytes] = '\0';
    fclose(f);
    char *pos = strstr(content, old_text);
    size_t matched_span_len = strlen(old_text);

    // Fallback: If exact match fails, try whitespace-normalized line matching
    if (!pos) {
        // Split content and old_text into lines and compare stripped lines
        const char *c_ptr = content;
        while (*c_ptr) {
            const char *cand = c_ptr;
            const char *o_ptr = old_text;
            const char *cur_line_file = cand;
            bool full_match = true;

            while (*o_ptr) {
                // Read a stripped line from old_text
                while (*o_ptr == ' ' || *o_ptr == '\t' || *o_ptr == '\r') o_ptr++;
                const char *o_start = o_ptr;
                while (*o_ptr && *o_ptr != '\n') o_ptr++;
                const char *o_end = o_ptr;
                while (o_end > o_start && (*(o_end - 1) == ' ' || *(o_end - 1) == '\t' || *(o_end - 1) == '\r')) o_end--;
                size_t o_len = o_end - o_start;
                if (*o_ptr == '\n') o_ptr++;

                // Read a stripped line from file content candidate
                while (*cur_line_file == ' ' || *cur_line_file == '\t' || *cur_line_file == '\r') cur_line_file++;
                const char *f_start = cur_line_file;
                while (*cur_line_file && *cur_line_file != '\n') cur_line_file++;
                const char *f_end = cur_line_file;
                while (f_end > f_start && (*(f_end - 1) == ' ' || *(f_end - 1) == '\t' || *(f_end - 1) == '\r')) f_end--;
                size_t f_len = f_end - f_start;
                if (*cur_line_file == '\n') cur_line_file++;

                if (o_len != f_len || (o_len > 0 && strncmp(o_start, f_start, o_len) != 0)) {
                    full_match = false;
                    break;
                }
            }

            if (full_match && (cur_line_file > cand)) {
                pos = (char *)cand;
                matched_span_len = cur_line_file - cand;
                break;
            }

            // Advance candidate to next line
            while (*c_ptr && *c_ptr != '\n') c_ptr++;
            if (*c_ptr == '\n') c_ptr++;
        }
    }

    if (!pos) {
        // Extract anchor token or up to first 24 chars for line search
        char sample_token[64] = {0};
        size_t sidx = 0;
        const char *token_p = old_text;
        while (*token_p == ' ' || *token_p == '\t' || *token_p == '\r' || *token_p == '\n') token_p++;
        while (*token_p && *token_p != ' ' && *token_p != '\t' && *token_p != '=' && *token_p != '(' && *token_p != '\n' && sidx < sizeof(sample_token) - 1) {
            sample_token[sidx++] = *token_p++;
        }
        sample_token[sidx] = '\0';

        DynString err = dyn_str_new();
        dyn_str_append(&err, "Error: old_text was not found in the target file.");
        if (sidx >= 3) {
            dyn_str_append(&err, "\nSimilar lines found in file for reference:\n");
            size_t ln = 1;
            int found_count = 0;
            const char *scan = content;
            while (*scan && found_count < 3) {
                const char *ln_start = scan;
                while (*scan && *scan != '\n') scan++;
                size_t cur_ln_len = scan - ln_start;
                char line_buf[256];
                size_t copy_sz = cur_ln_len < sizeof(line_buf) - 1 ? cur_ln_len : sizeof(line_buf) - 1;
                memcpy(line_buf, ln_start, copy_sz);
                line_buf[copy_sz] = '\0';

                if (strstr(line_buf, sample_token)) {
                    dyn_str_appendf(&err, "  Line %zu: %s\n", ln, line_buf);
                    found_count++;
                }
                ln++;
                if (*scan == '\n') scan++;
            }
            if (found_count == 0) {
                dyn_str_append(&err, "  (No lines containing search anchor found. Call read_file to inspect exact current text.)\n");
            }
        }
        free(content);
        return err.data;
    }

    char *second_pos = strstr(pos + matched_span_len, old_text);
    if (second_pos) {
        size_t l1 = 1, l2 = 1;
        for (const char *p = content; p < pos; p++) if (*p == '\n') l1++;
        for (const char *p = content; p < second_pos; p++) if (*p == '\n') l2++;
        free(content);
        DynString err = dyn_str_new();
        dyn_str_appendf(&err, "Error: old_text is ambiguous — matches found at line %zu and line %zu. Provide more surrounding context to disambiguate.", l1, l2);
        return err.data;
    }

    size_t prefix_len = (pos >= content) ? (size_t)(pos - content) : 0;
    size_t old_len = matched_span_len;
    size_t new_len = strlen(new_text);
    size_t suffix_len = (read_bytes >= prefix_len + old_len) ? (read_bytes - (prefix_len + old_len)) : 0;

    DynString updated = dyn_str_new();
    if (prefix_len > 0) dyn_str_append_len(&updated, content, prefix_len);
    if (new_len > 0) dyn_str_append_len(&updated, new_text, new_len);
    if (suffix_len > 0) dyn_str_append_len(&updated, pos + old_len, suffix_len);
    FILE *out_f = fopen(path, "wb");
    if (!out_f) {
        dyn_str_free(&updated);
        free(content);
        return xstrdup("Error: Failed to open target file for writing.");
    }

    fwrite(updated.data, 1, updated.len, out_f);
    fclose(out_f);
    dyn_str_free(&updated);

    DynString msg = dyn_str_new();
    dyn_str_appendf(&msg, "File '%s' successfully edited (replaced %zu bytes with %zu bytes).", path, old_len, new_len);
    char *diag = preflight_syntax_check(path);
    bool verify_compile = json_obj_get_bool(args, "verify_compile", false);

    if (diag) {
        if (verify_compile) {
            // Auto-revert to original content!
            FILE *rev_f = fopen(path, "wb");
            if (rev_f) {
                fwrite(content, 1, read_bytes, rev_f);
                fclose(rev_f);
            }
            dyn_str_free(&msg);
            DynString rev_msg = dyn_str_new();
            dyn_str_appendf(&rev_msg, "⚠️ [Compile-Verify Guard]: Edit rejected because compilation failed (file auto-reverted to original content).\n\n%s", diag);
            free(diag);
            free(content);
            return rev_msg.data;
        }
        dyn_str_append(&msg, "\n\n⚠️ COMPILER WARNING/ERROR after edit:\n");
        dyn_str_append(&msg, diag);
        free(diag);
    }
    free(content);
    return msg.data;
}

static char *tool_apply_patch(BelyaAgent *agent, const JsonValue *args) {
    (void)agent;
    if (g_harness && g_harness->plan_mode) {
        return xstrdup("Blocked: Plan Mode is active (read-only execution). Use exit_plan_mode to perform modifications.");
    }
    const char *path = json_obj_get_str(args, "path");
    const char *patch = json_obj_get_str(args, "patch");

    if (!path || !patch) return xstrdup("Error: Missing path or patch argument.");
    if (!is_path_jailed(path, NULL, true)) return xstrdup("Error: Path traversal denied.");

    FILE *f = fopen(path, "rb");
    if (!f) return xstrdup("Error: Target file not found.");

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz < 0 || sz > 2000000) {
        fclose(f);
        return xstrdup("Error: File exceeds maximum safe patch size (2MB) or cannot determine size.");
    }

    char *orig = malloc(sz + 1);
    if (!orig) { fclose(f); return xstrdup("Error: Out of memory."); }
    size_t r = fread(orig, 1, sz, f);
    orig[r] = '\0';
    fclose(f);

    const char *search_marker = "<<<<<<< SEARCH\n";
    const char *div_marker = "=======\n";
    const char *replace_marker = ">>>>>>> REPLACE";

    if (strstr(patch, search_marker) && strstr(patch, div_marker)) {
        const char *p = patch;
        char *cur_doc = orig;

        while ((p = strstr(p, search_marker)) != NULL) {
            p += strlen(search_marker);
            const char *div = strstr(p, div_marker);
            if (!div) {
                if (cur_doc != orig) free(cur_doc);
                free(orig);
                return xstrdup("Error: Malformed patch block (missing ======= divider).");
            }

            size_t search_len = div - p;
            char *search_str = malloc(search_len + 1);
            if (!search_str) {
                if (cur_doc != orig) free(cur_doc);
                free(orig);
                return xstrdup("Error: Out of memory allocating patch search block.");
            }
            memcpy(search_str, p, search_len);
            search_str[search_len] = '\0';

            const char *rep_start = div + strlen(div_marker);
            const char *rep_end = strstr(rep_start, replace_marker);
            if (!rep_end) {
                free(search_str);
                if (cur_doc != orig) free(cur_doc);
                free(orig);
                return xstrdup("Error: Malformed patch block (missing >>>>>>> REPLACE marker).");
            }

            size_t rep_len = rep_end - rep_start;
            char *rep_str = malloc(rep_len + 1);
            if (!rep_str) {
                free(search_str);
                if (cur_doc != orig) free(cur_doc);
                free(orig);
                return xstrdup("Error: Out of memory allocating patch replace block.");
            }
            memcpy(rep_str, rep_start, rep_len);
            rep_str[rep_len] = '\0';

            char *match = strstr(cur_doc, search_str);
            if (!match) {
                DynString err = dyn_str_new();
                dyn_str_appendf(&err, "Error: Search block mismatch during patch application. Expected content:\n%.200s%s\nThis text was not found in '%s'. Call read_file to verify current content.",
                                search_str, (strlen(search_str) > 200) ? "\n... [truncated]" : "", path);
                free(search_str);
                free(rep_str);
                if (cur_doc != orig) free(cur_doc);
                free(orig);
                return err.data;
            }

            size_t pre_len = match - cur_doc;
            size_t post_len = strlen(match + search_len);

            DynString next_doc = dyn_str_new();
            dyn_str_append_len(&next_doc, cur_doc, pre_len);
            dyn_str_append_len(&next_doc, rep_str, rep_len);
            dyn_str_append_len(&next_doc, match + search_len, post_len);

            if (cur_doc != orig) free(cur_doc);
            cur_doc = next_doc.data;

            free(search_str);
            free(rep_str);
            p = rep_end + strlen(replace_marker);
        }

        FILE *out_f = fopen(path, "wb");
        if (!out_f) {
            if (cur_doc != orig) free(cur_doc);
            free(orig);
            return xstrdup("Error: Failed to open target file for writing.");
        }
        fwrite(cur_doc, 1, strlen(cur_doc), out_f);
        fclose(out_f);
        if (cur_doc != orig) free(cur_doc);
        free(orig);

        DynString msg = dyn_str_new();
        dyn_str_append(&msg, "Patch successfully applied to file.");
        char *diag = preflight_syntax_check(path);
        if (diag) {
            dyn_str_append(&msg, "\n\n⚠️ COMPILER WARNING/ERROR after patch:\n");
            dyn_str_append(&msg, diag);
            free(diag);
        }
        return msg.data;
    }

    free(orig);
    return xstrdup("Error: Unsupported patch format. Use <<<<<<< SEARCH ... ======= ... >>>>>>> REPLACE blocks.");
}

static char *tool_list_dir(BelyaAgent *agent, const JsonValue *args) {
    (void)agent;
    const char *path = json_obj_get_str(args, "path");
    if (!path) path = g_harness ? g_harness->cwd : ".";
    if (!is_path_jailed(path, NULL, false)) return xstrdup("Error: Path traversal denied.");

    DIR *d = opendir(path);
    if (!d) return xstrdup("Error: Unable to open directory path.");

    DynString ds = dyn_str_new();
    struct dirent *dir;
    while ((dir = readdir(d)) != NULL) {
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;
        dyn_str_append(&ds, dir->d_name);
        dyn_str_append(&ds, "\n");
    }
    closedir(d);
    return ds.data;
}

static void search_files_recursive(const char *dir_path, const char *pattern, const char *glob_pat, bool is_regex, regex_t *preg, DynString *out, int *match_count) {
    if (*match_count >= 100) return;
    DIR *d = opendir(dir_path);
    if (!d) return;

    struct dirent *dir;
    while ((dir = readdir(d)) != NULL) {
        if (*match_count >= 100) break;
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;
        if (dir->d_name[0] == '.') continue;
        if (strcmp(dir->d_name, "node_modules") == 0 || strcmp(dir->d_name, "build") == 0) continue;

        char sub_path[4096];
        snprintf(sub_path, sizeof(sub_path), "%s/%s", dir_path, dir->d_name);
        if (belya_harness_is_secret_path(sub_path)) continue;

        struct stat st;
        if (stat(sub_path, &st) == -1) continue;

        if (S_ISDIR(st.st_mode)) {
            search_files_recursive(sub_path, pattern, glob_pat, is_regex, preg, out, match_count);
        } else if (S_ISREG(st.st_mode)) {
            if (glob_pat && strlen(glob_pat) > 0) {
                if (fnmatch(glob_pat, dir->d_name, 0) != 0) continue;
            }

            const char *ext = strrchr(dir->d_name, '.');
            if (ext && (strcmp(ext, ".o") == 0 || strcmp(ext, ".a") == 0 ||
                        strcmp(ext, ".so") == 0 || strcmp(ext, ".dylib") == 0 ||
                        strcmp(ext, ".png") == 0 || strcmp(ext, ".sqlite") == 0)) {
                continue;
            }

            FILE *f = fopen(sub_path, "r");
            if (!f) continue;

            // Binary file probe: skip files containing NUL bytes in first 512 bytes
            unsigned char probe[512];
            size_t n_read = fread(probe, 1, sizeof(probe), f);
            if (n_read > 0 && memchr(probe, '\0', n_read) != NULL) {
                fclose(f);
                continue;
            }
            rewind(f);

            char line[2048];
            size_t line_num = 1;
            int file_matches = 0;
            while (fgets(line, sizeof(line), f)) {
                if (*match_count >= 100) break;
                bool matched = false;
                if (is_regex && preg) {
                    matched = (regexec(preg, line, 0, NULL, 0) == 0);
                } else {
                    matched = (strstr(line, pattern) != NULL);
                }

                if (matched) {
                    if (file_matches >= 15) {
                        dyn_str_appendf(out, "%s: ... [file matches capped at 15]\n", sub_path);
                        break;
                    }
                    line[strcspn(line, "\r\n")] = '\0';
                    dyn_str_appendf(out, "%s:%zu: %s\n", sub_path, line_num, line);
                    (*match_count)++;
                    file_matches++;
                }
                line_num++;
            }
            fclose(f);
        }
    }
    closedir(d);
}

static char *tool_search_files(BelyaAgent *agent, const JsonValue *args) {
    (void)agent;
    const char *pattern = json_obj_get_str(args, "pattern");
    const char *path = json_obj_get_str(args, "path");
    const char *glob_pat = json_obj_get_str(args, "file_glob");
    bool use_regex = json_obj_get_bool(args, "regex", false);

    if (!pattern || strlen(pattern) == 0) {
        return xstrdup("Error: Missing search pattern argument.");
    }
    if (!path || strlen(path) == 0) {
        path = g_harness ? g_harness->cwd : ".";
    }
    if (!is_path_jailed(path, NULL, false)) return xstrdup("Error: Path traversal denied.");

    regex_t reg;
    regex_t *preg = NULL;
    if (use_regex) {
        if (regcomp(&reg, pattern, REG_EXTENDED | REG_NOSUB) != 0) {
            return xstrdup("Error: Invalid regular expression pattern.");
        }
        preg = &reg;
    }

    DynString out = dyn_str_new();
    int match_count = 0;
    search_files_recursive(path, pattern, glob_pat, use_regex, preg, &out, &match_count);

    if (preg) {
        regfree(preg);
    }

    if (match_count == 0) {
        dyn_str_appendf(&out, "No matches found for '%s' in '%s'.", pattern, path);
    } else if (match_count >= 100) {
        dyn_str_append(&out, "\n[Search capped at 100 matches]");
    }

    return out.data;
}

static char *tool_git_status(BelyaAgent *agent, const JsonValue *args) {
    (void)agent;
    const char *path = json_obj_get_str(args, "path");
    const char *dir = (path && strlen(path) > 0) ? path : (g_harness && strlen(g_harness->cwd) > 0 ? g_harness->cwd : ".");
    if (dir && !is_safe_shell_path(dir)) return xstrdup("Error: Invalid characters in directory path.");

    DynString cmd = dyn_str_new();
    dyn_str_appendf(&cmd, "cd \"%s\" 2>&1 && git status", dir);

    FILE *pipe = popen(cmd.data, "r");
    dyn_str_free(&cmd);
    if (!pipe) return xstrdup("Error: Failed to execute git status.");

    DynString out = dyn_str_new();
    char buf[512];
    while (fgets(buf, sizeof(buf), pipe)) {
        dyn_str_append(&out, buf);
        if (out.len > 10000) break;
    }
    pclose(pipe);

    if (out.len == 0) dyn_str_append(&out, "Working tree clean (no changes).");
    return out.data;
}

static char *tool_git_diff(BelyaAgent *agent, const JsonValue *args) {
    (void)agent;
    bool staged = json_obj_get_bool(args, "staged", false);
    const char *path = json_obj_get_str(args, "path");
    if (path && !is_safe_shell_path(path)) return xstrdup("Error: Invalid characters in diff path.");

    DynString cmd = dyn_str_new();
    if (g_harness && strlen(g_harness->cwd) > 0) {
        dyn_str_appendf(&cmd, "cd \"%s\" && git diff %s %s", g_harness->cwd, staged ? "--cached" : "", path ? path : "");
    } else {
        dyn_str_appendf(&cmd, "git diff %s %s", staged ? "--cached" : "", path ? path : "");
    }

    FILE *pipe = popen(cmd.data, "r");
    dyn_str_free(&cmd);
    if (!pipe) return xstrdup("Error: Failed to execute git diff.");

    DynString out = dyn_str_new();
    char buf[512];
    while (fgets(buf, sizeof(buf), pipe)) {
        dyn_str_append(&out, buf);
        if (out.len > 50000) {
            dyn_str_append(&out, "\n[Diff truncated: exceeded 50KB limit]");
            break;
        }
    }
    pclose(pipe);

    if (out.len == 0) dyn_str_append(&out, "No diff detected.");
    return out.data;
}

char *belya_ansi_diff_colorize(const char *raw_diff) {
    if (!raw_diff) return NULL;
    if (getenv("NO_COLOR") != NULL) {
        return xstrdup(raw_diff);
    }
    DynString ds = dyn_str_new();
    const char *line = raw_diff;
    bool in_hunk = false;

    while (*line) {
        const char *next = strchr(line, '\n');
        size_t len = next ? (size_t)(next - line) : strlen(line);

        if (strncmp(line, "diff --git", 10) == 0) {
            in_hunk = false;
            dyn_str_append(&ds, "\033[1;35m");
            dyn_str_append_len(&ds, line, len);
            dyn_str_append(&ds, "\033[0m\n");
        } else if (strncmp(line, "--- ", 4) == 0 && !in_hunk) {
            dyn_str_append(&ds, "\033[1;36m");
            dyn_str_append_len(&ds, line, len);
            dyn_str_append(&ds, "\033[0m\n");
        } else if (strncmp(line, "+++ ", 4) == 0 && !in_hunk) {
            dyn_str_append(&ds, "\033[1;36m");
            dyn_str_append_len(&ds, line, len);
            dyn_str_append(&ds, "\033[0m\n");
        } else if (strncmp(line, "@@ ", 3) == 0) {
            in_hunk = true;
            dyn_str_append(&ds, "\033[2;33m");
            dyn_str_append_len(&ds, line, len);
            dyn_str_append(&ds, "\033[0m\n");
        } else if (in_hunk && *line == '+') {
            dyn_str_append(&ds, "\033[32m");
            dyn_str_append_len(&ds, line, len);
            dyn_str_append(&ds, "\033[0m\n");
        } else if (in_hunk && *line == '-') {
            dyn_str_append(&ds, "\033[31m");
            dyn_str_append_len(&ds, line, len);
            dyn_str_append(&ds, "\033[0m\n");
        } else {
            dyn_str_append_len(&ds, line, len);
            dyn_str_append(&ds, "\n");
        }

        if (!next) break;
        line = next + 1;
    }
    return ds.data;
}

static char *tool_ask_user(BelyaAgent *agent, const JsonValue *args) {
    (void)agent;
    const char *q = json_obj_get_str(args, "question");
    if (!q) return xstrdup("Error: Missing question argument.");

    if (getenv("BELYA_HEADLESS") != NULL || getenv("BELYA_TEST_MODE") != NULL || !isatty(STDIN_FILENO)) {
        DynString ds = dyn_str_new();
        dyn_str_appendf(&ds, "User answered (automated mode): Accepted question '%s'", q);
        return ds.data;
    }

    printf("\n\033[1;36m[Agent Question]\033[0m %s\n", q);
    const JsonValue *opts = json_obj_get(args, "options");
    if (opts && opts->type == JSON_ARRAY) {
        for (size_t i = 0; i < opts->u.array.count; i++) {
            const JsonValue *opt = opts->u.array.items[i];
            if (opt && opt->type == JSON_STRING && opt->u.string) {
                printf("  %zu. %s\n", i + 1, opt->u.string);
            }
        }
    }
    printf("> Response: ");
    fflush(stdout);

    char line[1024];
    if (fgets(line, sizeof(line), stdin)) {
        size_t len = strlen(line);
        if (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[len-1] = '\0';
        DynString ds = dyn_str_new();
        dyn_str_appendf(&ds, "User response: %s", line);
        return ds.data;
    }
    return xstrdup("User provided no response.");
}

static char *tool_save_memory(BelyaAgent *agent, const JsonValue *args) {
    const char *topic = json_obj_get_str(args, "topic");
    const char *content = json_obj_get_str(args, "content");
    if (!topic || !content) return xstrdup("Error: Missing topic or content.");
    belya_agent_persist_memory(agent, topic, content);
    return xstrdup("Knowledge successfully stored in persistent agent SQLite database.");
}

static char *tool_recall_memory(BelyaAgent *agent, const JsonValue *args) {
    const char *query = json_obj_get_str(args, "query");
    if (!query) return xstrdup("Error: Missing query string.");
    return belya_agent_search_memory(agent, query);
}

static char *tool_task_create(BelyaAgent *agent, const JsonValue *args) {
    const char *subject = json_obj_get_str(args, "subject");
    const char *description = json_obj_get_str(args, "description");
    const char *dependencies = json_obj_get_str(args, "dependencies");
    const char *assigned_agent = json_obj_get_str(args, "assigned_agent");
    if (!subject) return xstrdup("Error: Missing 'subject' argument.");
    int64_t tid = belya_agent_task_create(agent, subject, description, dependencies, assigned_agent);
    if (tid < 0) return xstrdup("Error: Failed to create task.");
    DynString ds = dyn_str_new();
    dyn_str_appendf(&ds, "Task #%lld created successfully (status: pending, assigned: %s).",
        (long long)tid, assigned_agent ? assigned_agent : "Belya");
    return ds.data;
}

static char *tool_task_update(BelyaAgent *agent, const JsonValue *args) {
    const JsonValue *id_val = json_obj_get(args, "id");
    int64_t tid = 0;
    if (id_val && id_val->type == JSON_NUMBER) tid = (int64_t)id_val->u.number;
    else if (id_val && id_val->type == JSON_STRING && id_val->u.string) tid = atoll(id_val->u.string);
    if (tid <= 0) return xstrdup("Error: Missing or invalid task 'id'.");
    const char *status = json_obj_get_str(args, "status");
    const char *desc = json_obj_get_str(args, "description");
    if (!status && !desc) return xstrdup("Error: Provide 'status' or 'description' to update.");
    bool ok = belya_agent_task_update(agent, tid, status, desc);
    if (!ok) return xstrdup("Error: Failed to update task (check id and status value: pending|in_progress|completed|failed|blocked).");
    DynString ds = dyn_str_new();
    dyn_str_appendf(&ds, "Task #%lld updated successfully (status: %s).", (long long)tid, status ? status : "unchanged");
    return ds.data;
}

static char *tool_task_list(BelyaAgent *agent, const JsonValue *args) {
    const char *filter = json_obj_get_str(args, "status");
    return belya_agent_task_list(agent, filter);
}

static char *tool_task_get(BelyaAgent *agent, const JsonValue *args) {
    const JsonValue *id_val = json_obj_get(args, "id");
    int64_t tid = 0;
    if (id_val && id_val->type == JSON_NUMBER) tid = (int64_t)id_val->u.number;
    else if (id_val && id_val->type == JSON_STRING && id_val->u.string) tid = atoll(id_val->u.string);
    if (tid <= 0) return xstrdup("Error: Missing or invalid task 'id'.");
    return belya_agent_task_get(agent, tid);
}

static char *tool_cron_create(BelyaAgent *agent, const JsonValue *args) {
    const char *name = json_obj_get_str(args, "name");
    const char *schedule = json_obj_get_str(args, "schedule");
    const char *prompt = json_obj_get_str(args, "prompt");
    if (!name || !schedule || !prompt) return xstrdup("Error: Missing 'name', 'schedule', or 'prompt'.");
    int64_t cid = belya_agent_cron_create(agent, name, schedule, prompt);
    if (cid < 0) return xstrdup("Error: Failed to register cron job.");
    DynString ds = dyn_str_new();
    dyn_str_appendf(&ds, "Cron job '%s' (#%lld) registered with schedule '%s'.", name, (long long)cid, schedule);
    return ds.data;
}

static char *tool_cron_list(BelyaAgent *agent, const JsonValue *args) {
    (void)args;
    return belya_agent_cron_list(agent);
}

static char *tool_cron_tick(BelyaAgent *agent, const JsonValue *args) {
    (void)args;
    DynString logs = dyn_str_new();
    int fired = belya_agent_cron_tick(agent, &logs);
    if (fired == 0) {
        dyn_str_free(&logs);
        return xstrdup("No cron jobs currently due.");
    }
    return logs.data;
}

static char *tool_enter_plan_mode(BelyaAgent *agent, const JsonValue *args) {
    (void)agent;
    (void)args;
    if (g_harness) {
        belya_harness_enter_plan_mode(g_harness);
        return xstrdup("Plan Mode activated. All mutating file and shell operations are locked to read-only until exit_plan_mode is called.");
    }
    return xstrdup("Plan Mode activated.");
}

static char *tool_exit_plan_mode(BelyaAgent *agent, const JsonValue *args) {
    (void)agent;
    (void)args;
    if (g_harness) {
        belya_harness_exit_plan_mode(g_harness);
        return xstrdup("Plan Mode deactivated. Read-write filesystem and shell execution restored.");
    }
    return xstrdup("Plan Mode deactivated.");
}

static char *tool_enter_worktree(BelyaAgent *agent, const JsonValue *args) {
    (void)agent;
    const char *branch = json_obj_get_str(args, "branch");
    if (g_harness) {
        return belya_harness_enter_worktree(g_harness, branch);
    }
    return xstrdup("Error: Harness execution environment unavailable.");
}

static char *tool_leave_worktree(BelyaAgent *agent, const JsonValue *args) {
    (void)agent;
    const JsonValue *keep_val = json_obj_get(args, "keep");
    bool keep = (keep_val && keep_val->type == JSON_BOOL) ? keep_val->u.boolean : false;
    if (g_harness) {
        return belya_harness_leave_worktree(g_harness, keep);
    }
    return xstrdup("Error: Harness execution environment unavailable.");
}

static char *tool_manage_task(BelyaAgent *agent, const JsonValue *args) {
    (void)agent;
    if (!g_harness) return xstrdup("Error: Harness execution environment unavailable.");
    const char *action = json_obj_get_str(args, "action");
    if (!action) return xstrdup("Error: Missing action parameter ('list', 'status', 'send_input', 'kill').");

    if (strcmp(action, "list") == 0) {
        return belya_harness_task_list(g_harness);
    }
    double id_num = json_obj_get_num(args, "id", -1);
    if (id_num < 0) return xstrdup("Error: Missing or invalid task id parameter.");
    int task_id = (int)id_num;

    if (strcmp(action, "status") == 0) {
        return belya_harness_task_status(g_harness, task_id);
    } else if (strcmp(action, "send_input") == 0) {
        const char *input = json_obj_get_str(args, "input");
        if (!input) return xstrdup("Error: Missing input parameter for send_input action.");
        return belya_harness_task_send_input(g_harness, task_id, input);
    } else if (strcmp(action, "kill") == 0) {
        return belya_harness_task_kill(g_harness, task_id);
    }
    return xstrdup("Error: Unrecognized action. Supported: list, status, send_input, kill.");
}

static char *tool_web_search(BelyaAgent *agent, const JsonValue *args) {
    (void)agent;
    if (!g_harness) return xstrdup("Error: Harness execution environment unavailable.");
    const char *query = json_obj_get_str(args, "query");
    const char *provider = json_obj_get_str(args, "provider");
    return belya_harness_web_search(g_harness, query, provider);
}

static char *tool_web_fetch(BelyaAgent *agent, const JsonValue *args) {
    (void)agent;
    if (!g_harness) return xstrdup("Error: Harness execution environment unavailable.");
    const char *url = json_obj_get_str(args, "url");
    const char *mode = json_obj_get_str(args, "extract_mode");
    return belya_harness_web_fetch(g_harness, url, mode);
}

static char *tool_edit_notebook(BelyaAgent *agent, const JsonValue *args) {
    (void)agent;
    if (!g_harness) return xstrdup("Error: Harness execution environment unavailable.");
    const char *path = json_obj_get_str(args, "path");
    const char *action = json_obj_get_str(args, "action");
    int cell_idx = (int)json_obj_get_num(args, "cell_index", -1);
    const char *cell_type = json_obj_get_str(args, "cell_type");
    const char *source = json_obj_get_str(args, "source");
    return belya_harness_edit_notebook(g_harness, path, action, cell_idx, cell_type, source);
}

static char *tool_lsp_definition(BelyaAgent *agent, const JsonValue *args) {
    (void)agent;
    if (!g_harness) return xstrdup("Error: Harness execution environment unavailable.");
    const char *file = json_obj_get_str(args, "file");
    int line = (int)json_obj_get_num(args, "line", 1);
    int character = (int)json_obj_get_num(args, "character", 1);
    const char *symbol = json_obj_get_str(args, "symbol");
    return belya_harness_lsp_definition(g_harness, file, line, character, symbol);
}

static char *tool_lsp_references(BelyaAgent *agent, const JsonValue *args) {
    (void)agent;
    if (!g_harness) return xstrdup("Error: Harness execution environment unavailable.");
    const char *file = json_obj_get_str(args, "file");
    int line = (int)json_obj_get_num(args, "line", 1);
    int character = (int)json_obj_get_num(args, "character", 1);
    const char *symbol = json_obj_get_str(args, "symbol");
    return belya_harness_lsp_references(g_harness, file, line, character, symbol);
}

static char *tool_exec_backend(BelyaAgent *agent, const JsonValue *args) {
    (void)agent;
    if (!g_harness) return xstrdup("Error: No active harness.");

    const char *action = json_obj_get_str(args, "action");
    if (!action) action = "get";

    if (strcmp(action, "set") == 0) {
        const char *backend_str = json_obj_get_str(args, "backend");
        const char *target = json_obj_get_str(args, "target");

        if (!backend_str || strcmp(backend_str, "local") == 0) {
            belya_harness_set_exec_backend(g_harness, BELYA_BACKEND_LOCAL, NULL);
            return xstrdup("Execution backend set to: local");
        } else if (strcmp(backend_str, "docker") == 0) {
            if (!target || strlen(target) == 0) {
                return xstrdup("Error: Missing 'target' container ID or name for Docker backend.");
            }
            belya_harness_set_exec_backend(g_harness, BELYA_BACKEND_DOCKER, target);
            DynString ds = dyn_str_new();
            dyn_str_appendf(&ds, "Execution backend set to: docker (container: %s)", target);
            return ds.data;
        } else if (strcmp(backend_str, "ssh") == 0) {
            if (!target || strlen(target) == 0) {
                return xstrdup("Error: Missing 'target' host for SSH backend.");
            }
            belya_harness_set_exec_backend(g_harness, BELYA_BACKEND_SSH, target);
            DynString ds = dyn_str_new();
            dyn_str_appendf(&ds, "Execution backend set to: ssh (host: %s)", target);
            return ds.data;
        } else {
            return xstrdup("Error: Invalid backend. Supported: 'local', 'docker', 'ssh'.");
        }
    } else {
        DynString ds = dyn_str_new();
        if (g_harness->exec_backend == BELYA_BACKEND_DOCKER) {
            dyn_str_appendf(&ds, "Execution backend: docker (container: %s)", g_harness->docker_container);
        } else if (g_harness->exec_backend == BELYA_BACKEND_SSH) {
            dyn_str_appendf(&ds, "Execution backend: ssh (host: %s)", g_harness->ssh_host);
        } else {
            dyn_str_append(&ds, "Execution backend: local");
        }
        return ds.data;
    }
}

static char *tool_spawn_subagent(BelyaAgent *agent, const JsonValue *args) {
    const char *task = json_obj_get_str(args, "task");
    const char *instructions = json_obj_get_str(args, "instructions");
    double max_turns_num = json_obj_get_num(args, "max_turns", 5);

    if (!task) return xstrdup("Error: Missing subagent task argument.");

    DynString sys = dyn_str_new();
    dyn_str_append(&sys, "You are a specialized subagent worker. Execute the assigned task and provide a concise, direct answer.");
    if (g_harness && strlen(g_harness->cwd) > 0) {
        dyn_str_appendf(&sys, "\n\nWorkspace Root: %s\nIMPORTANT: Use current directory or relative paths for all file tools.\n", g_harness->cwd);
    }
    if (instructions && strlen(instructions) > 0) {
        dyn_str_append(&sys, "\nTask instructions:\n");
        dyn_str_append(&sys, instructions);
    }

    ModelGateway *sub_gw = model_gateway_init(agent->gateway->endpoint, agent->gateway->api_key, agent->gateway->model);
    if (!sub_gw) {
        dyn_str_free(&sys);
        return xstrdup("Error: Out of memory initializing subagent gateway.");
    }
    sub_gw->streaming = false; // Run silent to avoid stdout collision

    BelyaAgent *sub_agent = belya_agent_init(sub_gw, ":memory:", sys.data);
    if (!sub_agent) {
        model_gateway_free(sub_gw);
        dyn_str_free(&sys);
        return xstrdup("Error: Out of memory initializing subagent.");
    }
    dyn_str_free(&sys);

    BelyaHarness *saved_harness = g_harness;
    BelyaHarness *sub_harness = belya_harness_init(sub_agent);
    if (saved_harness && strlen(saved_harness->cwd) > 0) {
        snprintf(sub_harness->cwd, sizeof(sub_harness->cwd), "%s", saved_harness->cwd);
    }
    belya_agent_add_message(sub_agent, "user", task);

    int turns = (int)max_turns_num;
    if (turns <= 0 || turns > 10) turns = 5;

    DynString tool_log = dyn_str_new();
    DynString final_ans = dyn_str_new();
    bool running = true;
    size_t tool_executions = 0;

    while (running && turns-- > 0) {
        ModelGatewayResponse resp = belya_agent_step(sub_agent);
        if (!resp.has_tool_call) {
            if (resp.content) dyn_str_append(&final_ans, resp.content);
            running = false;
        } else {
            for (size_t i = 0; i < resp.tool_call_count; i++) {
                ModelParsedToolCall *tc = &resp.tool_calls[i];
                BelyaRegisteredTool *matched = NULL;
                for (size_t t = 0; t < sub_harness->tool_count; t++) {
                    if (strcmp(sub_harness->tools[t].name, tc->name) == 0 &&
                        strcmp(tc->name, "spawn_subagent") != 0) {
                        matched = &sub_harness->tools[t];
                        break;
                    }
                }
                if (matched && matched->callback) {
                    JsonValue *p_args = json_parse(tc->arguments_json);
                    char *obs = matched->callback(sub_agent, p_args);
                    json_free(p_args);
                    belya_agent_add_tool_result(sub_agent, tc->id, tc->name, obs);

                    dyn_str_appendf(&tool_log, "• [%s](%s) => %s\n", tc->name, tc->arguments_json ? tc->arguments_json : "", obs ? obs : "");
                    tool_executions++;
                    if (obs) free(obs);
                } else {
                    belya_agent_add_tool_result(sub_agent, tc->id, tc->name, "Tool not available in subagent sandbox.");
                }
            }
        }
        model_gateway_response_free(&resp);
    }

    belya_harness_free(sub_harness);
    g_harness = saved_harness;
    if (saved_harness && strlen(saved_harness->cwd) > 0) {
        if (chdir(saved_harness->cwd) != 0) { /* best-effort restore */ }
    }
    model_gateway_free(sub_gw);

    DynString envelope = dyn_str_new();
    dyn_str_appendf(&envelope, "=== Subagent Task Execution Envelope ===\nTask: %s\nTools Executed: %zu\n\n", task, tool_executions);
    if (tool_log.len > 0) {
        dyn_str_append(&envelope, "Execution Trace:\n");
        dyn_str_append(&envelope, tool_log.data);
        dyn_str_append(&envelope, "\n");
    }
    dyn_str_appendf(&envelope, "Final Output:\n%s", final_ans.len > 0 ? final_ans.data : "(Completed without final text)");
    dyn_str_free(&tool_log);
    dyn_str_free(&final_ans);

    return envelope.data;
}

// ==================== Belya Agency Subagent Dispatch, Rollback Guard & Triage ====================

static bool str_contains_ci(const char *haystack, const char *needle) {
    if (!haystack || !needle) return false;
    size_t hlen = strlen(haystack);
    size_t nlen = strlen(needle);
    if (nlen > hlen) return false;
    for (size_t i = 0; i <= hlen - nlen; i++) {
        if (strncasecmp(haystack + i, needle, nlen) == 0) return true;
    }
    return false;
}

char *belya_agency_dispatch_subagent(BelyaHarness *parent_harness, const char *role_name, const char *task, const char *extra_context) {
    if (!parent_harness || !parent_harness->agent || !role_name || !task) {
        return xstrdup("Error: Invalid arguments for subagent dispatch.");
    }

    // 1. Resolve agent manifest
    BelyaAgentManifest *manifest = belya_agent_get_manifest(parent_harness->agent, role_name);
    if (!manifest) {
        manifest = belya_agent_route_manifest(parent_harness->agent, role_name);
    }

    const char *whitelist = manifest ? manifest->tools : "read_file";
    int max_turns = (manifest && manifest->max_turns > 0) ? manifest->max_turns : 5;
    const char *inst = manifest ? manifest->instructions : "You are a specialized autonomous engineering subagent.";
    const char *model = (manifest && manifest->model && strcmp(manifest->model, "inherit") != 0 && strlen(manifest->model) > 0) 
                        ? manifest->model : parent_harness->agent->gateway->model;

    // 2. Check if subagent has write permissions
    bool has_write = belya_harness_is_tool_whitelisted(whitelist, "write_file") ||
                     belya_harness_is_tool_whitelisted(whitelist, "edit_file") ||
                     belya_harness_is_tool_whitelisted(whitelist, "apply_patch");

    // 3. Per-Subagent Git Rollback Guard: Snapshot HEAD SHA
    char snapshot_sha[128] = {0};
    bool git_repo_available = false;
    if (has_write) {
        FILE *p = popen("git rev-parse HEAD 2>/dev/null", "r");
        if (p) {
            if (fgets(snapshot_sha, sizeof(snapshot_sha), p)) {
                snapshot_sha[strcspn(snapshot_sha, "\r\n")] = '\0';
                if (strlen(snapshot_sha) == 40) {
                    git_repo_available = true;
                }
            }
            pclose(p);
        }
    }

    // 4. Construct specialized system prompt
    DynString sys = dyn_str_new();
    dyn_str_appendf(&sys, "Role: %s Subagent (%s)\n%s\n\nWorkspace Root: %s\nIMPORTANT: Use current directory or relative paths for all file tools.\n",
                    role_name, manifest ? manifest->role : role_name, inst, parent_harness->cwd);
    if (extra_context && strlen(extra_context) > 0) {
        dyn_str_append(&sys, "\n\nAdditional Technical Context:\n");
        dyn_str_append(&sys, extra_context);
    }

    // 5. Initialize isolated subagent and bounded harness
    ModelGateway *sub_gw = model_gateway_init(parent_harness->agent->gateway->endpoint, parent_harness->agent->gateway->api_key, model);
    if (!sub_gw) {
        if (manifest) belya_agent_manifest_free(manifest);
        return xstrdup("Error: Out of memory initializing subagent gateway.");
    }
    sub_gw->streaming = false; // Run silent to avoid stdout collision

    BelyaAgent *sub_agent = belya_agent_init(sub_gw, ":memory:", sys.data);
    if (!sub_agent) {
        model_gateway_free(sub_gw);
        if (manifest) belya_agent_manifest_free(manifest);
        dyn_str_free(&sys);
        return xstrdup("Error: Out of memory initializing subagent.");
    }
    dyn_str_free(&sys);

    BelyaHarness *saved_harness = g_harness;
    BelyaHarness *sub_harness = belya_harness_init_bounded(sub_agent, whitelist, role_name);
    if (!sub_harness) {
        belya_agent_free(sub_agent);
        return xstrdup("Error: Out of memory creating sub-harness.");
    }
    snprintf(sub_harness->cwd, sizeof(sub_harness->cwd), "%s", parent_harness->cwd);

    belya_agent_add_message(sub_agent, "user", task);

    int turns = max_turns;
    DynString tool_log = dyn_str_new();
    DynString final_ans = dyn_str_new();
    bool running = true;
    size_t tool_executions = 0;
    bool subagent_failed = false;

    while (running && turns-- > 0) {
        ModelGatewayResponse resp = belya_agent_step(sub_agent);
        if (!resp.has_tool_call) {
            if (resp.content) {
                dyn_str_append(&final_ans, resp.content);
                if (strncmp(resp.content, "API Error", 9) == 0 || strncmp(resp.content, "Network Error", 13) == 0) {
                    subagent_failed = true;
                }
            }
            running = false;
        } else {
            for (size_t i = 0; i < resp.tool_call_count; i++) {
                ModelParsedToolCall *tc = &resp.tool_calls[i];
                BelyaRegisteredTool *matched = NULL;
                for (size_t t = 0; t < sub_harness->tool_count; t++) {
                    if (strcmp(sub_harness->tools[t].name, tc->name) == 0 &&
                        strcmp(tc->name, "spawn_subagent") != 0 &&
                        strcmp(tc->name, "dispatch_agent") != 0) {
                        matched = &sub_harness->tools[t];
                        break;
                    }
                }
                if (matched && matched->callback) {
                    JsonValue *p_args = json_parse(tc->arguments_json);
                    char *obs = matched->callback(sub_agent, p_args);
                    json_free(p_args);

                    char *breaker_alert = NULL;
                    bool tripped = belya_harness_record_tool_observation(sub_harness, tc->name, tc->arguments_json, obs, &breaker_alert);
                    const char *final_obs = breaker_alert ? breaker_alert : (obs ? obs : "Success");

                    belya_agent_add_tool_result(sub_agent, tc->id, tc->name, final_obs);
                    dyn_str_appendf(&tool_log, "• [%s](%s) => %s\n", tc->name, tc->arguments_json ? tc->arguments_json : "", final_obs ? final_obs : "");
                    tool_executions++;

                    if (tripped) {
                        subagent_failed = true;
                    }
                    if (breaker_alert) free(breaker_alert);
                    if (obs) free(obs);
                } else {
                    belya_agent_add_tool_result(sub_agent, tc->id, tc->name, "Error: Tool not permitted by role security policy.");
                    dyn_str_appendf(&tool_log, "• [%s] => BLOCKED by role whitelist\n", tc->name);
                }
            }
        }
        model_gateway_response_free(&resp);
    }

    // Failure checks: exhausted turn limit or consecutive failures
    if (running && turns <= 0) {
        subagent_failed = true;
    }
    if (sub_harness->consecutive_tool_failures >= 3) {
        subagent_failed = true;
    }
    if (final_ans.len == 0 && tool_executions == 0) {
        subagent_failed = true;
    }

    // 6. Git Rollback Guard Execution (non-destructive)
    // C2 fix: never `git reset --hard` or `git clean -fd` — that destroys the
    // operator's uncommitted and untracked work (state DBs, skills, trajectories).
    // Restore only tracked files under the subagent's workspace.
    bool rolled_back = false;
    if (has_write && git_repo_available && is_valid_hex_sha(snapshot_sha)) {
        if (subagent_failed) {
            char reset_cmd[1024];
            snprintf(reset_cmd, sizeof(reset_cmd),
                     "git -C \"%.900s\" checkout -- . 2>/dev/null", parent_harness->cwd);
            int ret = system(reset_cmd);
            (void)ret;
            rolled_back = true;

            char log_msg[256];
            snprintf(log_msg, sizeof(log_msg), "Subagent '%s' failed; restored tracked files to snapshot %.8s (untracked preserved)", role_name, snapshot_sha);
            belya_agent_log_timeline(parent_harness->agent, "subagent_rollback", log_msg);
        } else {
            char log_msg[256];
            snprintf(log_msg, sizeof(log_msg), "Subagent '%s' completed successfully; changes preserved", role_name);
            belya_agent_log_timeline(parent_harness->agent, "subagent_commit", log_msg);
        }
    }

    // 7. Format output envelope (Late-2026 ROMA Context Firewall)
    DynString envelope = dyn_str_new();
    dyn_str_appendf(&envelope, "=== Belya Agency Subagent Execution Envelope ===\n");
    dyn_str_appendf(&envelope, "Role: %s | Permitted Tools: [%s]\n", role_name, whitelist);
    dyn_str_appendf(&envelope, "Status: %s\n", subagent_failed ? "FAILED (Rolled Back)" : "SUCCESS");
    dyn_str_appendf(&envelope, "Tools Executed: %zu\n", tool_executions);
    if (rolled_back) {
        dyn_str_appendf(&envelope, "Git Rollback Guard: ACTIVATED (Reverted working tree to snapshot %.8s)\n", snapshot_sha);
    }
    // ROMA Context Firewall: Intermediate tool execution traces are strictly firewalled
    // to prevent parent context contamination and token degradation.
    dyn_str_appendf(&envelope, "ROMA Context Firewall: Child execution trace isolated (%zu bytes scrubbed from parent context)\n", tool_log.len);
    dyn_str_appendf(&envelope, "\nFinal Output:\n%s", final_ans.len > 0 ? final_ans.data : "(Subagent completed without output text)");

    dyn_str_free(&tool_log);
    dyn_str_free(&final_ans);
    belya_harness_free(sub_harness);
    g_harness = saved_harness;
    if (saved_harness && strlen(saved_harness->cwd) > 0) {
        if (chdir(saved_harness->cwd) != 0) { /* best-effort restore */ }
    }
    model_gateway_free(sub_gw);
    if (manifest) belya_agent_manifest_free(manifest);

    return envelope.data;
}

static char *tool_dispatch_agent(BelyaAgent *agent, const JsonValue *args) {
    (void)agent;
    const char *role = json_obj_get_str(args, "role");
    const char *task = json_obj_get_str(args, "task");
    const char *context = json_obj_get_str(args, "context");

    if (!role || !task) return xstrdup("Error: Missing role or task argument for dispatch_agent.");
    if (!g_harness) return xstrdup("Error: Harness execution environment not available.");

    return belya_agency_dispatch_subagent(g_harness, role, task, context);
}

// Late-2026 Phase 3: AST Symbol Graph Navigation & ROMA Task Trees

size_t belya_harness_index_symbols(BelyaHarness *h, const char *dir_path) {
    if (!h || !h->agent || !h->agent->db) return 0;

    sqlite3 *db = h->agent->db;
    const char *create_tbl =
        "CREATE TABLE IF NOT EXISTS symbols ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  file_path TEXT NOT NULL,"
        "  symbol_name TEXT NOT NULL,"
        "  kind TEXT NOT NULL,"
        "  start_line INTEGER NOT NULL,"
        "  end_line INTEGER NOT NULL,"
        "  signature TEXT"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_symbols_name ON symbols(symbol_name);"
        "CREATE INDEX IF NOT EXISTS idx_symbols_kind ON symbols(kind);";
    sqlite3_exec(db, create_tbl, 0, 0, 0);

    const char *target = (dir_path && strlen(dir_path) > 0) ? dir_path : ".";

    sqlite3_stmt *stmt = NULL;
    const char *ins_sql = "INSERT INTO symbols (file_path, symbol_name, kind, start_line, end_line, signature) VALUES (?, ?, ?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db, ins_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return 0;
    }

    size_t count = 0;
    sqlite3_exec(db, "BEGIN TRANSACTION;", 0, 0, 0);

    DIR *d = opendir(target);
    if (!d) {
        sqlite3_finalize(stmt);
        sqlite3_exec(db, "COMMIT;", 0, 0, 0);
        return 0;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        const char *ext = strrchr(ent->d_name, '.');
        if (!ext || (strcmp(ext, ".c") != 0 && strcmp(ext, ".h") != 0)) continue;

        char fpath[1024];
        snprintf(fpath, sizeof(fpath), "%s/%s", target, ent->d_name);

        FILE *f = fopen(fpath, "r");
        if (!f) continue;

        char line[2048];
        int lineno = 0;
        while (fgets(line, sizeof(line), f)) {
            lineno++;
            line[strcspn(line, "\r\n")] = '\0';

            char *trim = line;
            while (*trim == ' ' || *trim == '\t') trim++;
            if (*trim == '\0') continue;

            // 1. MACRO
            if (strncmp(trim, "#define", 7) == 0 && (trim[7] == ' ' || trim[7] == '\t')) {
                char *m = trim + 7;
                while (*m == ' ' || *m == '\t') m++;
                char sym[128] = {0};
                int si = 0;
                while (*m && (isalnum((unsigned char)*m) || *m == '_') && si < 127) {
                    sym[si++] = *m++;
                }
                sym[si] = '\0';
                if (si > 0) {
                    sqlite3_reset(stmt);
                    sqlite3_bind_text(stmt, 1, fpath, -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(stmt, 2, sym, -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(stmt, 3, "MACRO", -1, SQLITE_STATIC);
                    sqlite3_bind_int(stmt, 4, lineno);
                    sqlite3_bind_int(stmt, 5, lineno);
                    sqlite3_bind_text(stmt, 6, trim, -1, SQLITE_TRANSIENT);
                    if (sqlite3_step(stmt) == SQLITE_DONE) count++;
                }
                continue;
            }

            // 2. STRUCT_DEF / TYPEDEF
            if (strncmp(trim, "typedef struct", 14) == 0 || strncmp(trim, "struct ", 7) == 0 || strncmp(trim, "typedef enum", 12) == 0) {
                char *s = trim;
                if (strncmp(s, "typedef struct", 14) == 0) s += 14;
                else if (strncmp(s, "struct", 6) == 0) s += 6;
                else s += 12;
                while (*s == ' ' || *s == '\t') s++;
                char sym[128] = {0};
                int si = 0;
                while (*s && (isalnum((unsigned char)*s) || *s == '_') && si < 127) {
                    sym[si++] = *s++;
                }
                sym[si] = '\0';
                if (si > 0) {
                    sqlite3_reset(stmt);
                    sqlite3_bind_text(stmt, 1, fpath, -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(stmt, 2, sym, -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(stmt, 3, "STRUCT_DEF", -1, SQLITE_STATIC);
                    sqlite3_bind_int(stmt, 4, lineno);
                    sqlite3_bind_int(stmt, 5, lineno);
                    sqlite3_bind_text(stmt, 6, trim, -1, SQLITE_TRANSIENT);
                    if (sqlite3_step(stmt) == SQLITE_DONE) count++;
                }
            }

            // 3. FUNC_DEF
            char *open_p = strchr(trim, '(');
            if (open_p && !strchr(trim, ';') && strchr(trim, '{')) {
                char *fn_end = open_p - 1;
                while (fn_end > trim && (*fn_end == ' ' || *fn_end == '\t')) fn_end--;
                char *fn_start = fn_end;
                while (fn_start > trim && (isalnum((unsigned char)*fn_start) || *fn_start == '_')) fn_start--;
                if (!isalnum((unsigned char)*fn_start) && *fn_start != '_') fn_start++;

                if (fn_end >= fn_start) {
                    char sym[128] = {0};
                    size_t len = (size_t)(fn_end - fn_start + 1);
                    if (len < sizeof(sym)) {
                        memcpy(sym, fn_start, len);
                        sym[len] = '\0';
                        if (strcmp(sym, "if") != 0 && strcmp(sym, "while") != 0 && strcmp(sym, "for") != 0 &&
                            strcmp(sym, "switch") != 0 && strcmp(sym, "catch") != 0) {
                            sqlite3_reset(stmt);
                            sqlite3_bind_text(stmt, 1, fpath, -1, SQLITE_TRANSIENT);
                            sqlite3_bind_text(stmt, 2, sym, -1, SQLITE_TRANSIENT);
                            sqlite3_bind_text(stmt, 3, "FUNC_DEF", -1, SQLITE_STATIC);
                            sqlite3_bind_int(stmt, 4, lineno);
                            sqlite3_bind_int(stmt, 5, lineno);
                            sqlite3_bind_text(stmt, 6, trim, -1, SQLITE_TRANSIENT);
                            if (sqlite3_step(stmt) == SQLITE_DONE) count++;
                        }
                    }
                }
            }

            // 4. CALL extraction
            char *p = trim;
            while ((open_p = strchr(p, '(')) != NULL) {
                char *c_end = open_p - 1;
                while (c_end >= trim && (*c_end == ' ' || *c_end == '\t')) c_end--;
                char *c_start = c_end;
                while (c_start >= trim && (isalnum((unsigned char)*c_start) || *c_start == '_')) c_start--;
                if (c_start < trim || (!isalnum((unsigned char)*c_start) && *c_start != '_')) c_start++;

                if (c_end >= c_start) {
                    char sym[128] = {0};
                    size_t len = (size_t)(c_end - c_start + 1);
                    if (len > 0 && len < sizeof(sym)) {
                        memcpy(sym, c_start, len);
                        sym[len] = '\0';
                        if (strcmp(sym, "if") != 0 && strcmp(sym, "while") != 0 && strcmp(sym, "for") != 0 &&
                            strcmp(sym, "switch") != 0 && strcmp(sym, "sizeof") != 0 && strcmp(sym, "assert") != 0 &&
                            strcmp(sym, "return") != 0) {
                            sqlite3_reset(stmt);
                            sqlite3_bind_text(stmt, 1, fpath, -1, SQLITE_TRANSIENT);
                            sqlite3_bind_text(stmt, 2, sym, -1, SQLITE_TRANSIENT);
                            sqlite3_bind_text(stmt, 3, "CALL", -1, SQLITE_STATIC);
                            sqlite3_bind_int(stmt, 4, lineno);
                            sqlite3_bind_int(stmt, 5, lineno);
                            sqlite3_bind_text(stmt, 6, trim, -1, SQLITE_TRANSIENT);
                            if (sqlite3_step(stmt) == SQLITE_DONE) count++;
                        }
                    }
                }
                p = open_p + 1;
            }
        }
        fclose(f);
    }
    closedir(d);

    sqlite3_finalize(stmt);
    sqlite3_exec(db, "COMMIT;", 0, 0, 0);
    return count;
}

char *belya_harness_goto_definition(BelyaHarness *h, const char *symbol_name) {
    if (!h || !symbol_name || strlen(symbol_name) == 0) {
        return xstrdup("Error: Invalid symbol name.");
    }
    if (!h->agent || !h->agent->db) {
        return xstrdup("Error: Agent database unavailable.");
    }

    sqlite3_stmt *stmt = NULL;
    const char *q = "SELECT file_path, kind, start_line, end_line, signature FROM symbols WHERE symbol_name = ? AND kind != 'CALL' ORDER BY (CASE kind WHEN 'FUNC_DEF' THEN 1 WHEN 'STRUCT_DEF' THEN 2 ELSE 3 END) LIMIT 5;";
    if (sqlite3_prepare_v2(h->agent->db, q, -1, &stmt, NULL) != SQLITE_OK) {
        belya_harness_index_symbols(h, ".");
        if (sqlite3_prepare_v2(h->agent->db, q, -1, &stmt, NULL) != SQLITE_OK) {
            return xstrdup("Error: Failed to query symbols database.");
        }
    }

    sqlite3_bind_text(stmt, 1, symbol_name, -1, SQLITE_TRANSIENT);

    DynString ds = dyn_str_new();
    int matches = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *fpath = (const char *)sqlite3_column_text(stmt, 0);
        const char *kind = (const char *)sqlite3_column_text(stmt, 1);
        int s_line = sqlite3_column_int(stmt, 2);
        int e_line = sqlite3_column_int(stmt, 3);
        const char *sig = (const char *)sqlite3_column_text(stmt, 4);

        if (matches == 0) {
            dyn_str_appendf(&ds, "🎯 Symbol Definition: '%s'\n", symbol_name);
        }
        dyn_str_appendf(&ds, "  [%s] %s:%d-%d\n    Signature: %s\n", kind ? kind : "SYM", fpath ? fpath : "", s_line, e_line, sig ? sig : "");
        matches++;
    }
    sqlite3_finalize(stmt);

    if (matches == 0) {
        dyn_str_free(&ds);
        DynString not_found = dyn_str_new();
        dyn_str_appendf(&not_found, "Symbol '%s' not found in symbols index.", symbol_name);
        return not_found.data;
    }

    return ds.data;
}

char *belya_harness_find_callers(BelyaHarness *h, const char *symbol_name) {
    if (!h || !symbol_name || strlen(symbol_name) == 0) {
        return xstrdup("Error: Invalid symbol name.");
    }
    if (!h->agent || !h->agent->db) {
        return xstrdup("Error: Agent database unavailable.");
    }

    sqlite3_stmt *stmt = NULL;
    const char *q = "SELECT file_path, start_line, signature FROM symbols WHERE symbol_name = ? AND kind = 'CALL' ORDER BY file_path, start_line LIMIT 30;";
    if (sqlite3_prepare_v2(h->agent->db, q, -1, &stmt, NULL) != SQLITE_OK) {
        belya_harness_index_symbols(h, ".");
        if (sqlite3_prepare_v2(h->agent->db, q, -1, &stmt, NULL) != SQLITE_OK) {
            return xstrdup("Error: Failed to query callers database.");
        }
    }

    sqlite3_bind_text(stmt, 1, symbol_name, -1, SQLITE_TRANSIENT);

    DynString ds = dyn_str_new();
    int callers = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *fpath = (const char *)sqlite3_column_text(stmt, 0);
        int line = sqlite3_column_int(stmt, 1);
        const char *sig = (const char *)sqlite3_column_text(stmt, 2);

        if (callers == 0) {
            dyn_str_appendf(&ds, "📞 Callers of Symbol: '%s'\n", symbol_name);
        }
        dyn_str_appendf(&ds, "  • %s:%d => %s\n", fpath ? fpath : "", line, sig ? sig : "");
        callers++;
    }
    sqlite3_finalize(stmt);

    if (callers == 0) {
        dyn_str_free(&ds);
        DynString not_found = dyn_str_new();
        dyn_str_appendf(&not_found, "No callers found for symbol '%s' in symbols index.", symbol_name);
        return not_found.data;
    }
    dyn_str_appendf(&ds, "Total Callers: %d\n", callers);
    return ds.data;
}

// Late-2026 Phase 3: Repo-to-MCP Dynamic Synthesis

bool belya_harness_synthesize_mcp_server(BelyaHarness *h, const char *server_name, const char *tool_name, const char *desc, const char *script_cmd, const char *schema_json) {
    if (!h || !server_name || !tool_name || !script_cmd) return false;

    mkdir(".belya", 0755);
    mkdir(".belya/mcp", 0755);

    char server_path[512];
    snprintf(server_path, sizeof(server_path), ".belya/mcp/%s_server.sh", server_name);

    FILE *f = fopen(server_path, "w");
    if (!f) return false;

    const char *effective_schema = (schema_json && strlen(schema_json) > 0) ? schema_json : "{\"type\":\"object\"}";

    fprintf(f, "#!/bin/sh\n");
    fprintf(f, "# Auto-synthesized Paper2Agent MCP Server: %s\n", server_name);
    fprintf(f, "while IFS= read -r line; do\n");
    fprintf(f, "  [ -z \"$line\" ] && continue\n");
    fprintf(f, "  id=$(echo \"$line\" | sed -n 's/.*\"id\":\\([0-9]*\\).*/\\1/p')\n");
    fprintf(f, "  [ -z \"$id\" ] && id=1\n");
    fprintf(f, "  if echo \"$line\" | grep -q '\"method\":\"initialize\"'; then\n");
    fprintf(f, "    echo \"{\\\"jsonrpc\\\":\\\"2.0\\\",\\\"id\\\":$id,\\\"result\\\":{\\\"protocolVersion\\\":\\\"2024-11-05\\\",\\\"capabilities\\\":{\\\"tools\\\":{}},\\\"serverInfo\\\":{\\\"name\\\":\\\"%s\\\",\\\"version\\\":\\\"1.0.0\\\"}}}\"\n", server_name);
    fprintf(f, "  elif echo \"$line\" | grep -q '\"method\":\"tools/list\"'; then\n");
    fprintf(f, "    echo '{\"jsonrpc\":\"2.0\",\"id\":'$id',\"result\":{\"tools\":[{\"name\":\"%s\",\"description\":\"%s\",\"inputSchema\":%s}]}}'\n", tool_name, desc ? desc : "", effective_schema);
    fprintf(f, "  elif echo \"$line\" | grep -q '\"method\":\"tools/call\"'; then\n");
    fprintf(f, "    output=$(%s 2>&1)\n", script_cmd);
    fprintf(f, "    clean_out=$(printf '%%s' \"$output\" | tr '\\n' ' ' | sed 's/\"/\\\\\"/g')\n");
    fprintf(f, "    echo \"{\\\"jsonrpc\\\":\\\"2.0\\\",\\\"id\\\":$id,\\\"result\\\":{\\\"content\\\":[{\\\"type\\\":\\\"text\\\",\\\"text\\\":\\\"$clean_out\\\"}]}}\"\n");
    fprintf(f, "  elif echo \"$line\" | grep -q '\"method\":\"ping\"'; then\n");
    fprintf(f, "    echo \"{\\\"jsonrpc\\\":\\\"2.0\\\",\\\"id\\\":$id,\\\"result\\\":{}}\"\n");
    fprintf(f, "  fi\n");
    fprintf(f, "done\n");
    fclose(f);
    chmod(server_path, 0755);

    // Validation Gating: Test running with initialize & tools/list
    char test_cmd[1024];
    snprintf(test_cmd, sizeof(test_cmd),
             "printf '{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}\\n' | %s 2>/dev/null | grep -q '\"name\":\"%s\"'",
             server_path, tool_name);
    int rc = system(test_cmd);
    if (rc != 0) {
        unlink(server_path);
        return false;
    }

    return belya_harness_connect_mcp(h, server_path);
}

// Late-2026 Phase 3: Mythos Variant Security Analysis

char *belya_harness_audit_security_variants(BelyaHarness *h, const char *pattern_type, const char *target_path) {
    (void)h;
    const char *path = (target_path && strlen(target_path) > 0) ? target_path : ".";
    const char *pat = (pattern_type && strlen(pattern_type) > 0) ? pattern_type : "all";

    DynString ds = dyn_str_new();
    dyn_str_appendf(&ds, "🛡️ === Mythos Variant Security Sweeper Report ===\n");
    dyn_str_appendf(&ds, "Target Path: %s | Pattern Filter: %s\n\n", path, pat);

    DIR *d = opendir(path);
    if (!d) {
        dyn_str_appendf(&ds, "Error: Target directory '%s' cannot be opened.\n", path);
        return ds.data;
    }

    struct dirent *ent;
    int findings = 0;

    bool check_overflow = (strcmp(pat, "all") == 0 || strstr(pat, "overflow") != NULL || strstr(pat, "unbounded") != NULL);
    bool check_fmt = (strcmp(pat, "all") == 0 || strstr(pat, "format") != NULL);

    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        const char *ext = strrchr(ent->d_name, '.');
        if (!ext || (strcmp(ext, ".c") != 0 && strcmp(ext, ".h") != 0)) continue;

        char fpath[1024];
        snprintf(fpath, sizeof(fpath), "%s/%s", path, ent->d_name);

        FILE *f = fopen(fpath, "r");
        if (!f) continue;

        char line[2048];
        int lineno = 0;
        while (fgets(line, sizeof(line), f)) {
            lineno++;
            line[strcspn(line, "\r\n")] = '\0';
            char *trim = line;
            while (*trim == ' ' || *trim == '\t') trim++;

            if (strncmp(trim, "//", 2) == 0 || strncmp(trim, "/*", 2) == 0) continue;

            if (check_overflow) {
                if (strstr(trim, "strcpy(") != NULL || strstr(trim, "strcat(") != NULL ||
                    strstr(trim, "gets(") != NULL || strstr(trim, "sprintf(") != NULL) {
                    dyn_str_appendf(&ds, "[CWE-120: Buffer Copy without Checking Size of Input]\n");
                    dyn_str_appendf(&ds, "  Location: %s:%d\n  Code:     %s\n  Remedy:   Replace with snprintf() or bounded strncpy().\n\n", fpath, lineno, trim);
                    findings++;
                }
            }

            if (check_fmt) {
                if ((strstr(trim, "printf(") != NULL || strstr(trim, "fprintf(") != NULL) &&
                    strstr(trim, "\"") == NULL && strchr(trim, ',') == NULL) {
                    dyn_str_appendf(&ds, "[CWE-134: Use of Externally-Controlled Format String]\n");
                    dyn_str_appendf(&ds, "  Location: %s:%d\n  Code:     %s\n  Remedy:   Use string literal format: printf(\"%%s\", ...).\n\n", fpath, lineno, trim);
                    findings++;
                }
            }
        }
        fclose(f);
    }
    closedir(d);

    dyn_str_appendf(&ds, "Summary: %d security variant finding(s) detected.\n", findings);
    dyn_str_appendf(&ds, "PoC Verification Command: gcc -fsanitize=address,undefined -O1 -g -o /tmp/audit_test %s/main.c\n", path);

    return ds.data;
}

static char *tool_goto_definition(BelyaAgent *agent, const JsonValue *args) {
    (void)agent;
    if (!g_harness) return xstrdup("Error: Harness runtime not active.");
    const char *sym = json_obj_get_str(args, "symbol");
    if (!sym) sym = json_obj_get_str(args, "name");
    if (!sym) return xstrdup("Error: Missing required argument 'symbol'.");
    return belya_harness_goto_definition(g_harness, sym);
}

static char *tool_find_callers(BelyaAgent *agent, const JsonValue *args) {
    (void)agent;
    if (!g_harness) return xstrdup("Error: Harness runtime not active.");
    const char *sym = json_obj_get_str(args, "symbol");
    if (!sym) sym = json_obj_get_str(args, "name");
    if (!sym) return xstrdup("Error: Missing required argument 'symbol'.");
    return belya_harness_find_callers(g_harness, sym);
}

static char *tool_synthesize_mcp_server(BelyaAgent *agent, const JsonValue *args) {
    (void)agent;
    if (!g_harness) return xstrdup("Error: Harness runtime not active.");
    const char *srv = json_obj_get_str(args, "server_name");
    const char *tl = json_obj_get_str(args, "tool_name");
    const char *desc = json_obj_get_str(args, "description");
    const char *cmd = json_obj_get_str(args, "script_cmd");
    const char *sch = json_obj_get_str(args, "schema_json");

    if (!srv || !tl || !cmd) {
        return xstrdup("Error: Missing required arguments (server_name, tool_name, script_cmd).");
    }

    bool ok = belya_harness_synthesize_mcp_server(g_harness, srv, tl, desc, cmd, sch);
    if (ok) {
        DynString ds = dyn_str_new();
        dyn_str_appendf(&ds, "Successfully synthesized and connected MCP server '%s' exposing tool '%s'.", srv, tl);
        return ds.data;
    }
    return xstrdup("Error: Failed to synthesize or validate MCP server.");
}

static char *tool_audit_security_variants(BelyaAgent *agent, const JsonValue *args) {
    (void)agent;
    if (!g_harness) return xstrdup("Error: Harness runtime not active.");
    const char *pat = json_obj_get_str(args, "pattern_type");
    const char *target = json_obj_get_str(args, "target_path");
    return belya_harness_audit_security_variants(g_harness, pat ? pat : "all", target ? target : ".");
}

bool belya_agency_triage(BelyaHarness *harness, const char *user_input, char ***out_pipeline, size_t *out_count, char **out_direct_reply) {
    if (!harness || !user_input || !out_pipeline || !out_count) return false;
    *out_pipeline = NULL;
    *out_count = 0;
    if (out_direct_reply) *out_direct_reply = NULL;

    const char *p = user_input;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;

    // 1. Conversational Fast-Path: greetings & simple queries
    const char *greetings[] = {
        "hi", "hello", "hey", "good morning", "good evening", "status", "who are you", "help", "/help", NULL
    };
    for (int i = 0; greetings[i]; i++) {
        if (strcasecmp(p, greetings[i]) == 0) {
            if (out_direct_reply) {
                *out_direct_reply = xstrdup("Hello! I am Belya Agency, an autonomous multi-agent engineering harness. I coordinate Architect, Builder, Reviewer, and Tester subagents to solve complex engineering tasks.");
            }
            return true;
        }
    }

    // Jev TypeSafe AI Triage Coprocessor
    if (harness->agent && harness->agent->jev) {
        char *jev_role = jev_decide_triage_role(harness->agent->jev, p);
        if (jev_role) {
            if (strcmp(jev_role, "direct") == 0) {
                if (out_direct_reply) {
                    *out_direct_reply = xstrdup("Hello! I am Belya Agency, an autonomous multi-agent engineering harness. I coordinate Architect, Builder, Reviewer, and Tester subagents to solve complex engineering tasks.");
                }
                free(jev_role);
                return true;
            } else if (strcmp(jev_role, "architect") == 0 ||
                       strcmp(jev_role, "builder") == 0 ||
                       strcmp(jev_role, "tester") == 0 ||
                       strcmp(jev_role, "reviewer") == 0) {
                char **pipeline = malloc(sizeof(char *) * 1);
                if (pipeline) {
                    pipeline[0] = jev_role;
                    *out_pipeline = pipeline;
                    *out_count = 1;
                    return true;
                }
            }
            free(jev_role);
        }
    }

    // 2. Intent Classification for Subagent Pipeline
    bool is_test = (str_contains_ci(p, "test") || str_contains_ci(p, "benchmark") || str_contains_ci(p, "verify"));
    bool is_review = (str_contains_ci(p, "review") || str_contains_ci(p, "audit") || str_contains_ci(p, "diff"));
    bool is_research = (str_contains_ci(p, "explain") || str_contains_ci(p, "how does") || str_contains_ci(p, "what is") || str_contains_ci(p, "investigate") || str_contains_ci(p, "research") || str_contains_ci(p, "find "));

    char **pipeline = NULL;
    size_t count = 0;

    if (is_review && !str_contains_ci(p, "fix") && !str_contains_ci(p, "implement")) {
        count = 1;
        pipeline = malloc(sizeof(char *) * count);
        if (!pipeline) {
            *out_pipeline = NULL;
            *out_count = 0;
            return false;
        }
        pipeline[0] = xstrdup("reviewer");
    } else if (is_test && !str_contains_ci(p, "fix") && !str_contains_ci(p, "implement")) {
        count = 1;
        pipeline = malloc(sizeof(char *) * count);
        if (!pipeline) {
            *out_pipeline = NULL;
            *out_count = 0;
            return false;
        }
        pipeline[0] = xstrdup("tester");
    } else if (is_research && !str_contains_ci(p, "fix") && !str_contains_ci(p, "implement")) {
        count = 1;
        pipeline = malloc(sizeof(char *) * count);
        if (!pipeline) {
            *out_pipeline = NULL;
            *out_count = 0;
            return false;
        }
        pipeline[0] = xstrdup("architect");
    } else {
        // Standard Full Engineering Pipeline: Architect -> Builder -> Tester
        count = 3;
        pipeline = malloc(sizeof(char *) * count);
        if (!pipeline) {
            *out_pipeline = NULL;
            *out_count = 0;
            return false;
        }
        pipeline[0] = xstrdup("architect");
        pipeline[1] = xstrdup("builder");
        pipeline[2] = xstrdup("tester");
    }

    *out_pipeline = pipeline;
    *out_count = count;
    return true;
}

char *belya_agency_execute_pipeline(BelyaHarness *harness, const char *prompt, const char **pipeline, size_t count) {
    if (!harness || !prompt || !pipeline || count == 0) return NULL;

    DynString report = dyn_str_new();
    dyn_str_appendf(&report, "🚀 [Belya Agency Multi-Agent Pipeline: %zu Stages]\nSequence: ", count);
    for (size_t i = 0; i < count; i++) {
        dyn_str_appendf(&report, "%s%s", pipeline[i], (i + 1 < count) ? " -> " : "\n\n");
    }

    DynString accumulated_context = dyn_str_new();
    dyn_str_appendf(&accumulated_context, "Original Mission: %s\n", prompt);

    for (size_t i = 0; i < count; i++) {
        const char *role = pipeline[i];
        printf("\n\033[1;36m[Agency Pipeline Stage %zu/%zu]: Dispatching %s...\033[0m\n", i + 1, count, role);
        dyn_str_appendf(&report, "--- Stage %zu: %s ---\n", i + 1, role);

        char *stage_out = belya_agency_dispatch_subagent(harness, role, prompt, accumulated_context.data);
        if (stage_out) {
            dyn_str_append(&report, stage_out);
            dyn_str_append(&report, "\n\n");

            dyn_str_appendf(&accumulated_context, "\n--- Stage %zu (%s) Output ---\n%s\n", i + 1, role, stage_out);
            free(stage_out);
        }
    }

    dyn_str_free(&accumulated_context);
    return report.data;
}

// ==================== End Belya Agency Functions ====================

static size_t fetch_url_curl_sink(void *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total = size * nmemb;
    DynString *ds = (DynString *)userdata;
    dyn_str_append_len(ds, (const char *)ptr, total);
    return total;
}

/* SSRF guard: reject loopback, link-local, and RFC1918/documented-private
   destinations (including IPv4-mapped IPv6). Called per connection attempt. */
static bool sockaddr_is_private(const struct sockaddr *sa) {
    if (!sa) return false;
    if (sa->sa_family == AF_INET) {
        const struct sockaddr_in *in = (const struct sockaddr_in *)sa;
        uint32_t raw_ip;
        memcpy(&raw_ip, &in->sin_addr, sizeof(raw_ip));
        uint32_t a = ntohl(raw_ip);
        if ((a & 0xFF000000u) == 0x00000000u) return true;   /* 0.0.0.0/8 */
        if ((a & 0xFF000000u) == 0x0A000000u) return true;   /* 10.0.0.0/8 */
        if ((a & 0xFF000000u) == 0x7F000000u) return true;   /* 127.0.0.0/8 */
        if ((a & 0xFFF00000u) == 0xAC100000u) return true;   /* 172.16.0.0/12 */
        if ((a & 0xFFFF0000u) == 0xC0A80000u) return true;   /* 192.168.0.0/16 */
        if ((a & 0xFFFF0000u) == 0xA9FE0000u) return true;   /* 169.254.0.0/16 */
        return false;
    }
    if (sa->sa_family == AF_INET6) {
        const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)sa;
        const uint8_t *b = in6->sin6_addr.s6_addr;
        /* IPv6 Loopback ::1 */
        if (b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 0 &&
            b[4] == 0 && b[5] == 0 && b[6] == 0 && b[7] == 0 &&
            b[8] == 0 && b[9] == 0 && b[10] == 0 && b[11] == 0 &&
            b[12] == 0 && b[13] == 0 && b[14] == 0 && b[15] == 1) {
            return true;
        }
        /* IPv6 Link-local fe80::/10 */
        if (b[0] == 0xfe && (b[1] & 0xc0) == 0x80) return true;
        /* IPv6 Unique Local Address ULA fc00::/7 */
        if ((b[0] & 0xfe) == 0xfc) return true;
        /* IPv4-mapped IPv6 ::ffff:0:0/96 */
        if (b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 0 &&
            b[4] == 0 && b[5] == 0 && b[6] == 0 && b[7] == 0 &&
            b[8] == 0 && b[9] == 0 && b[10] == 0xff && b[11] == 0xff) {
            uint32_t a = ((uint32_t)b[12] << 24) | ((uint32_t)b[13] << 16) |
                         ((uint32_t)b[14] << 8) | (uint32_t)b[15];
            if ((a & 0xFF000000u) == 0x00000000u) return true;   /* 0.0.0.0/8 */
            if ((a & 0xFF000000u) == 0x0A000000u) return true;   /* 10.0.0.0/8 */
            if ((a & 0xFF000000u) == 0x7F000000u) return true;   /* 127.0.0.0/8 */
            if ((a & 0xFFF00000u) == 0xAC100000u) return true;   /* 172.16.0.0/12 */
            if ((a & 0xFFFF0000u) == 0xC0A80000u) return true;   /* 192.168.0.0/16 */
            if ((a & 0xFFFF0000u) == 0xA9FE0000u) return true;   /* 169.254.0.0/16 */
            return false;
        }
        return false;
    }
    return false;
}

bool belya_harness_sockaddr_is_private(const struct sockaddr *sa) {
    return sockaddr_is_private(sa);
}

static curl_socket_t fetch_url_opensocket_cb(void *clientp, curlsocktype purpose,
                                             const struct sockaddr *addr, socklen_t addrlen) {
    (void)clientp; (void)purpose; (void)addrlen;
    if (addr && sockaddr_is_private(addr)) {
        return CURL_SOCKET_BAD; /* refuse the connection */
    }
    return socket(addr ? addr->sa_family : AF_INET, SOCK_STREAM, 0);
}

static char *tool_fetch_url(BelyaAgent *agent, const JsonValue *args) {
    (void)agent;
    const char *url = json_obj_get_str(args, "url");
    if (!url || strlen(url) == 0) return xstrdup("Error: Missing url argument.");

    const char *method = json_obj_get_str(args, "method");
    const char *req_body = json_obj_get_str(args, "body");
    if (!req_body) req_body = json_obj_get_str(args, "data");

    CURL *curl = curl_easy_init();
    if (!curl) return xstrdup("Error: Failed to initialize HTTP client.");

    DynString body = dyn_str_new();
    struct curl_slist *headers = NULL;
    const char *ua_str = (agent && agent->gateway && agent->gateway->user_agent) ?
                         agent->gateway->user_agent : "BelyaAgent/4.0 (Autonomous C99 Engine)";
    char ua_buf[512];
    snprintf(ua_buf, sizeof(ua_buf), "User-Agent: %s", ua_str);
    headers = curl_slist_append(headers, ua_buf);

    // Custom headers
    JsonValue *hdrs_val = json_obj_get(args, "headers");
    if (hdrs_val) {
        if (hdrs_val->type == JSON_OBJECT) {
            for (size_t i = 0; i < hdrs_val->u.object.count; i++) {
                if (hdrs_val->u.object.members[i].value && hdrs_val->u.object.members[i].value->type == JSON_STRING) {
                    char hbuf[1024];
                    snprintf(hbuf, sizeof(hbuf), "%s: %s",
                             hdrs_val->u.object.members[i].key,
                             hdrs_val->u.object.members[i].value->u.string);
                    headers = curl_slist_append(headers, hbuf);
                }
            }
        } else if (hdrs_val->type == JSON_ARRAY) {
            for (size_t i = 0; i < hdrs_val->u.array.count; i++) {
                if (hdrs_val->u.array.items[i] && hdrs_val->u.array.items[i]->type == JSON_STRING) {
                    headers = curl_slist_append(headers, hdrs_val->u.array.items[i]->u.string);
                }
            }
        }
    }

    if (method && (strcasecmp(method, "POST") == 0 || strcasecmp(method, "PUT") == 0 ||
                   strcasecmp(method, "PATCH") == 0 || strcasecmp(method, "DELETE") == 0)) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
        if (req_body) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req_body);
        }
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fetch_url_curl_sink);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    /* C1: only http/https; block file://, gopher, etc. (and on redirects). */
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    /* C1: SSRF guard — refuse loopback/link-local/private destinations unless
       the operator explicitly allows them via FETCH_ALLOW_PRIVATE=1. */
    if (!getenv("FETCH_ALLOW_PRIVATE")) {
        curl_easy_setopt(curl, CURLOPT_OPENSOCKETFUNCTION, fetch_url_opensocket_cb);
    }

    CURLcode code = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (code != CURLE_OK) {
        dyn_str_free(&body);
        DynString err = dyn_str_new();
        dyn_str_appendf(&err, "Error fetching URL (%s): %s", method ? method : "GET", curl_easy_strerror(code));
        return err.data;
    }

    if (body.len > 100000) {
        body.data[100000] = '\0';
        body.len = 100000;
        dyn_str_append(&body, "\n[Content truncated by Belya Harness (100KB buffer limit)]");
    }

    if (body.len == 0) {
        dyn_str_appendf(&body, "URL returned empty response (HTTP %ld).", http_code);
    } else if (http_code >= 400) {
        dyn_str_appendf(&body, "\n[HTTP Error Status: %ld]", http_code);
    }

    return body.data;
}

static char *tool_save_skill(BelyaAgent *agent, const JsonValue *args) {
    const char *name = json_obj_get_str(args, "name");
    const char *trigger = json_obj_get_str(args, "trigger");
    const char *desc = json_obj_get_str(args, "description");
    const char *instructions = json_obj_get_str(args, "instructions");

    if (!name || !instructions) return xstrdup("Error: Missing required arguments (name, instructions).");
    if (!valid_ident(name)) return xstrdup("Error: Invalid skill name (allowed: A-Za-z0-9_- , max 64 characters).");
    if (belya_agent_save_skill(agent, name, trigger, desc, instructions)) {
        DynString res = dyn_str_new();
        dyn_str_appendf(&res, "Skill '%s' successfully saved to procedural memory with trigger '%s'.", name, trigger ? trigger : name);
        return res.data;
    }
    return xstrdup("Error: Failed to save skill to database.");
}

static char *tool_recall_skill(BelyaAgent *agent, const JsonValue *args) {
    const char *query = json_obj_get_str(args, "query");
    return belya_agent_search_skills(agent, query ? query : "");
}

static char *tool_recall_conversation(BelyaAgent *agent, const JsonValue *args) {
    const char *query = json_obj_get_str(args, "query");
    return belya_agent_search_conversations(agent, query ? query : "");
}

// Dynamic Custom Tool Execution Runner
static char *tool_custom_script_runner(BelyaAgent *agent, const JsonValue *args) {
    (void)agent;
    const char *script_path = g_active_custom_script_path;
    if (!script_path && g_harness && g_harness->tool_count > 0) {
        for (int i = (int)g_harness->tool_count - 1; i >= 0; i--) {
            if (g_harness->tools[i].custom_script_path) {
                script_path = g_harness->tools[i].custom_script_path;
                break;
            }
        }
    }
    if (!script_path) return xstrdup("Error: Custom script path not found.");

    char *args_str = args ? json_serialize(args) : xstrdup("{}");

    int in_pipe[2];
    int out_pipe[2];
    if (pipe(in_pipe) == -1 || pipe(out_pipe) == -1) {
        free(args_str);
        return xstrdup("Error: Failed to create pipes for custom tool execution.");
    }

    pid_t pid = fork();
    if (pid == -1) {
        free(args_str);
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        return xstrdup("Error: Failed to fork process for custom tool.");
    }

    const char *first_val_str = NULL;
    if (args && args->type == JSON_OBJECT && args->u.object.count > 0) {
        first_val_str = json_obj_get_str(args, "input");
        if (!first_val_str) first_val_str = json_obj_get_str(args, "arg");
        if (!first_val_str) first_val_str = json_obj_get_str(args, "value");
        if (!first_val_str) first_val_str = json_obj_get_str(args, "text");
        if (!first_val_str) first_val_str = json_obj_get_str(args, "command");
        if (!first_val_str) first_val_str = json_obj_get_str(args, "query");
        if (!first_val_str && args->u.object.members[0].value && args->u.object.members[0].value->type == JSON_STRING) {
            first_val_str = args->u.object.members[0].value->u.string;
        }
    }
    if (!first_val_str) first_val_str = args_str;

    if (pid == 0) {
        // Child
        close(in_pipe[1]);
        dup2(in_pipe[0], STDIN_FILENO);
        close(in_pipe[0]);

        close(out_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(out_pipe[1], STDERR_FILENO);
        close(out_pipe[1]);

        // Explicit Environment Parameter Schema Contract
        setenv("TOOL_ARGS_JSON", args_str, 1);
        if (args && args->type == JSON_OBJECT) {
            for (size_t m = 0; m < args->u.object.count; m++) {
                const char *k = args->u.object.members[m].key;
                JsonValue *v = args->u.object.members[m].value;
                if (k && v) {
                    char env_key1[256];
                    char env_key2[256];
                    snprintf(env_key1, sizeof(env_key1), "PARAM_%s", k);
                    snprintf(env_key2, sizeof(env_key2), "ARG_%s", k);
                    for (size_t c = 0; env_key1[c]; c++) {
                        if (env_key1[c] >= 'a' && env_key1[c] <= 'z') env_key1[c] -= 32;
                    }
                    for (size_t c = 0; env_key2[c]; c++) {
                        if (env_key2[c] >= 'a' && env_key2[c] <= 'z') env_key2[c] -= 32;
                    }
                    char *v_str = (v->type == JSON_STRING) ? xstrdup(v->u.string) : json_serialize(v);
                    if (v_str) {
                        setenv(env_key1, v_str, 1);
                        setenv(env_key2, v_str, 1);
                        free(v_str);
                    }
                }
            }
        }

        execl(script_path, script_path, first_val_str, args_str, (char *)NULL);
        // Fallback to /bin/sh if not directly executable binary
        execl("/bin/sh", "sh", script_path, first_val_str, args_str, (char *)NULL);
        _exit(127);
    }

    // Parent
    close(in_pipe[0]);
    close(out_pipe[1]);

    safe_pipe_write(in_pipe[1], args_str, strlen(args_str));
    safe_pipe_write(in_pipe[1], "\n", 1);
    close(in_pipe[1]);
    free(args_str);

    int flags = fcntl(out_pipe[0], F_GETFL, 0);
    fcntl(out_pipe[0], F_SETFL, flags | O_NONBLOCK);

    DynString out = dyn_str_new();
    struct timeval start, now;
    gettimeofday(&start, NULL);
    bool timed_out = false;

    while (1) {
        char buf[512];
        ssize_t bytes = read(out_pipe[0], buf, sizeof(buf) - 1);
        if (bytes > 0) {
            buf[bytes] = '\0';
            dyn_str_append(&out, buf);
            if (out.len > 100000) {
                /* H6: reap the child instead of leaking a zombie. */
                kill(pid, SIGKILL);
                waitpid(pid, NULL, 0);
                break;
            }
        }

        int status;
        pid_t res = waitpid(pid, &status, WNOHANG);
        if (res == pid) {
            while ((bytes = read(out_pipe[0], buf, sizeof(buf) - 1)) > 0) {
                buf[bytes] = '\0';
                dyn_str_append(&out, buf);
            }
            break;
        }

        gettimeofday(&now, NULL);
        double elapsed = (now.tv_sec - start.tv_sec) + (now.tv_usec - start.tv_usec) / 1000000.0;
        if (elapsed > 15.0) {
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
            timed_out = true;
            break;
        }
        struct timespec ts = {0, 10000000L};
        nanosleep(&ts, NULL);
    }
    close(out_pipe[0]);

    if (timed_out) dyn_str_append(&out, "\n[Custom tool execution timed out after 15s]");
    if (out.len == 0) dyn_str_append(&out, "Custom tool executed successfully with no output.");
    return out.data;
}

// Tool define_tool: Enables BelyaAgent to dynamically add new tools to itself
static char *tool_define_tool(BelyaAgent *agent, const JsonValue *args) {
    (void)agent;
    if (!g_harness) return xstrdup("Error: Harness runtime not active.");

    const char *name = json_obj_get_str(args, "name");
    const char *desc = json_obj_get_str(args, "description");
    JsonValue *params = json_obj_get(args, "parameters");
    const char *script_body = json_obj_get_str(args, "script_body");

    if (!name || !desc || !script_body) {
        return xstrdup("Error: Missing required fields (name, description, script_body).");
    }

    // Clone params if present, or create default object schema
    JsonValue *params_clone = NULL;
    if (params) {
        char *s = json_serialize(params);
        params_clone = json_parse(s);
        free(s);
    } else {
        params_clone = json_create_object();
        json_obj_add(params_clone, "type", json_create_string("object"));
    }

    bool ok = belya_harness_define_custom_tool(g_harness, name, desc, params_clone, script_body);
    if (ok) {
        DynString res = dyn_str_new();
        dyn_str_appendf(&res, "Tool '%s' successfully defined, persisted, and registered into active tool catalog.", name);
        return res.data;
    }
    return xstrdup("Error: Failed to register custom tool (max tool limit reached or disk write error).");
}

bool belya_harness_define_custom_tool(BelyaHarness *h, const char *name, const char *desc, JsonValue *params, const char *script_body) {
    if (!h || !name || !script_body || h->tool_count >= 64) {
        if (params) json_free(params);
        return false;
    }
    if (!valid_ident(name)) {
        if (params) json_free(params);
        return false;
    }

    mkdir(".belya", 0755);
    mkdir(".belya/tools", 0755);

    char script_path[512];
    snprintf(script_path, sizeof(script_path), ".belya/tools/%s.sh", name);

    FILE *sf = fopen(script_path, "wb");
    if (!sf) {
        if (params) json_free(params);
        return false;
    }

    // Write shebang if missing
    if (strncmp(script_body, "#!", 2) != 0) {
        fprintf(sf, "#!/bin/sh\n");
    }
    fprintf(sf, "%s\n", script_body);
    fclose(sf);
    chmod(script_path, 0755);

    // Save JSON metadata for auto-loading on restart
    char meta_path[512];
    snprintf(meta_path, sizeof(meta_path), ".belya/tools/%s.json", name);
    JsonValue *meta = json_create_object();
    json_obj_add(meta, "name", json_create_string(name));
    json_obj_add(meta, "description", json_create_string(desc ? desc : ""));
    json_obj_add(meta, "parameters", params);
    json_obj_add(meta, "script_path", json_create_string(script_path));

    char *meta_str = json_serialize(meta);
    FILE *mf = fopen(meta_path, "wb");
    if (mf) {
        fwrite(meta_str, 1, strlen(meta_str), mf);
        fclose(mf);
    }
    free(meta_str);

    // Detach params before freeing meta wrapper
    meta->u.object.members[2].value = NULL;
    json_free(meta);

    // Register into active harness
    char *name_copy2 = xstrdup(name);
    if (!name_copy2) {
        return false;
    }
    h->tools[h->tool_count].name = name_copy2;
    h->tools[h->tool_count].security = PERM_ALLOW;
    h->tools[h->tool_count].callback = tool_custom_script_runner;
    h->tools[h->tool_count].custom_script_path = xstrdup(script_path);
    h->tools[h->tool_count].mcp_client = NULL;
    h->tool_count++;

    belya_agent_register_schema(h->agent, name, desc ? desc : "", params);
    return true;
}

void belya_harness_load_custom_tools(BelyaHarness *h) {
    if (!h) return;
    DIR *d = opendir(".belya/tools");
    if (!d) return;

    struct dirent *dir;
    while ((dir = readdir(d)) != NULL) {
        const char *ext = strrchr(dir->d_name, '.');
        if (ext && strcmp(ext, ".json") == 0) {
            char fpath[512];
            snprintf(fpath, sizeof(fpath), ".belya/tools/%s", dir->d_name);
            FILE *f = fopen(fpath, "rb");
            if (f) {
                fseek(f, 0, SEEK_END);
                long sz = ftell(f);
                fseek(f, 0, SEEK_SET);
                char *buf = malloc(sz + 1);
                if (buf) {
                    size_t r = fread(buf, 1, sz, f);
                    buf[r] = '\0';
                    JsonValue *meta = json_parse(buf);
                    if (meta) {
                        const char *t_name = json_obj_get_str(meta, "name");
                        const char *t_desc = json_obj_get_str(meta, "description");
                        const char *s_path = json_obj_get_str(meta, "script_path");
                        JsonValue *t_schema = json_obj_get(meta, "parameters");

                        if (t_name && s_path && h->tool_count < 64) {
                            // Check if already registered
                            bool exists = false;
                            for (size_t k = 0; k < h->tool_count; k++) {
                                if (strcmp(h->tools[k].name, t_name) == 0) {
                                    exists = true;
                                    break;
                                }
                            }
                            if (!exists) {
                                char *sch_str = t_schema ? json_serialize(t_schema) : xstrdup("{\"type\":\"object\"}");
                                JsonValue *sch_clone = json_parse(sch_str);
                                free(sch_str);

                                h->tools[h->tool_count].name = xstrdup(t_name);
                                h->tools[h->tool_count].security = PERM_ALLOW;
                                h->tools[h->tool_count].callback = tool_custom_script_runner;
                                h->tools[h->tool_count].custom_script_path = xstrdup(s_path);
                                h->tools[h->tool_count].mcp_client = NULL;
                                h->tool_count++;

                                belya_agent_register_schema(h->agent, t_name, t_desc ? t_desc : "", sch_clone);
                            }
                        }
                        json_free(meta);
                    }
                    free(buf);
                }
                fclose(f);
            }
        }
    }
    closedir(d);
}

// Harness Setup

static JsonValue *build_string_param_schema(const char *prop_name, const char *prop_desc) {
    JsonValue *p = json_create_object();
    json_obj_add(p, "type", json_create_string("object"));
    JsonValue *props = json_create_object();
    JsonValue *item = json_create_object();
    json_obj_add(item, "type", json_create_string("string"));
    json_obj_add(item, "description", json_create_string(prop_desc));
    json_obj_add(props, prop_name, item);
    json_obj_add(p, "properties", props);
    JsonValue *req = json_create_array();
    json_arr_add(req, json_create_string(prop_name));
    json_obj_add(p, "required", req);
    return p;
}

static void harness_on_pre_compact_hook(void *ud) {
    BelyaHarness *bh = (BelyaHarness *)ud;
    if (bh) {
        BelyaHookResult hres = belya_run_lifecycle_hook(bh, BELYA_HOOK_PRE_COMPACT, NULL, NULL, NULL);
        belya_hook_result_free(&hres);
    }
}

BelyaHarness *belya_harness_init_bounded(BelyaAgent *agent, const char *tools_whitelist, const char *role_name) {
    BelyaHarness *h = calloc(1, sizeof(BelyaHarness));
    if (!h) return NULL;
    for (size_t i = 0; i < BELYA_MAX_BACKGROUND_TASKS; i++) {
        h->task_table.tasks[i].master_fd = -1;
        h->task_table.tasks[i].state = BELYA_TASK_STATE_UNUSED;
    }
    h->agent = agent;
    if (agent) {
        agent->user_data = h;
        belya_agent_set_pre_compact_hook(agent, harness_on_pre_compact_hook, h);
    }
    if (role_name) {
        snprintf(h->active_role, sizeof(h->active_role), "%s", role_name);
        if (strcmp(role_name, "tester") == 0) {
            h->bash_restricted = true;
        }
    }
    if (tools_whitelist) {
        snprintf(h->tools_whitelist, sizeof(h->tools_whitelist), "%s", tools_whitelist);
        if (strstr(tools_whitelist, "restricted_bash") != NULL) {
            h->bash_restricted = true;
        }
    }
    if (getcwd(h->cwd, sizeof(h->cwd)) == NULL) {
        snprintf(h->cwd, sizeof(h->cwd), ".");
    }
    /* Auto-configure remote execution backend (Docker / SSH) from environment if set */
    const char *eb_env = getenv("EXEC_BACKEND");
    const char *dc_env = getenv("DOCKER_CONTAINER");
    const char *ssh_env = getenv("SSH_HOST");
    if (eb_env && strcasecmp(eb_env, "docker") == 0) {
        belya_harness_set_exec_backend(h, BELYA_BACKEND_DOCKER, (dc_env && strlen(dc_env) > 0) ? dc_env : "belya-sandbox");
    } else if (eb_env && strcasecmp(eb_env, "ssh") == 0 && ssh_env && strlen(ssh_env) > 0) {
        belya_harness_set_exec_backend(h, BELYA_BACKEND_SSH, ssh_env);
    }
    /* Capture the workspace root exactly once, at first harness init.
       The jail must NOT follow later chdir() calls from tool_bash. */
    if (!g_workspace_root_set) {
        char root_tmp[4096];
        if (getcwd(root_tmp, sizeof(root_tmp))) {
            if (!realpath(root_tmp, g_workspace_root)) {
                snprintf(g_workspace_root, sizeof(g_workspace_root), "%s", root_tmp);
            }
            g_workspace_root_set = true;
        }
    }
    g_harness = h;

#define REGISTER_IF_PERMITTED(t_name, t_desc, t_schema, t_sec, t_fn) \
    do { \
        JsonValue *_sch = (t_schema); \
        if (belya_harness_is_tool_whitelisted(tools_whitelist, t_name)) { \
            belya_harness_register_tool(h, t_name, t_desc, _sch, t_sec, t_fn); \
        } else if (_sch) { \
            json_free(_sch); \
        } \
    } while (0)

    // 1. bash
    JsonValue *bash_params = json_create_object();
    json_obj_add(bash_params, "type", json_create_string("object"));
    JsonValue *bp_props = json_create_object();
    JsonValue *bp_cmd = json_create_object();
    json_obj_add(bp_cmd, "type", json_create_string("string"));
    json_obj_add(bp_cmd, "description", json_create_string("The bash command string to execute"));
    json_obj_add(bp_props, "command", bp_cmd);
    JsonValue *bp_bg = json_create_object();
    json_obj_add(bp_bg, "type", json_create_string("boolean"));
    json_obj_add(bp_bg, "description", json_create_string("Run command asynchronously in a persistent background PTY subshell"));
    json_obj_add(bp_props, "background", bp_bg);
    json_obj_add(bash_params, "properties", bp_props);
    JsonValue *bp_req = json_create_array();
    json_arr_add(bp_req, json_create_string("command"));
    json_obj_add(bash_params, "required", bp_req);
    REGISTER_IF_PERMITTED("bash", "Execute a shell command. Use background:true for long-running processes or servers. Timeout: 30s.", bash_params, PERM_ALLOW, tool_bash);

    // 2. read_file
    JsonValue *read_params = json_create_object();
    json_obj_add(read_params, "type", json_create_string("object"));
    JsonValue *r_props = json_create_object();
    JsonValue *r_path = json_create_object();
    json_obj_add(r_path, "type", json_create_string("string"));
    json_obj_add(r_path, "description", json_create_string("Path of file to read"));
    json_obj_add(r_props, "path", r_path);
    JsonValue *r_off = json_create_object();
    json_obj_add(r_off, "type", json_create_string("number"));
    json_obj_add(r_off, "description", json_create_string("Optional start line number (1-indexed)"));
    json_obj_add(r_props, "offset", r_off);
    JsonValue *r_lim = json_create_object();
    json_obj_add(r_lim, "type", json_create_string("number"));
    json_obj_add(r_lim, "description", json_create_string("Optional maximum number of lines to read"));
    json_obj_add(r_props, "limit", r_lim);
    json_obj_add(read_params, "properties", r_props);
    JsonValue *r_req = json_create_array();
    json_arr_add(r_req, json_create_string("path"));
    json_obj_add(read_params, "required", r_req);
    REGISTER_IF_PERMITTED("read_file", "Read file contents with line numbers. ALWAYS call before edit_file. Use offset+limit for files >200 lines.", read_params, PERM_ALLOW, tool_read_file);

    // 3. write_file
    JsonValue *write_params = json_create_object();
    json_obj_add(write_params, "type", json_create_string("object"));
    JsonValue *w_props = json_create_object();
    JsonValue *w_path = json_create_object();
    json_obj_add(w_path, "type", json_create_string("string"));
    json_obj_add(w_props, "path", w_path);
    JsonValue *w_cont = json_create_object();
    json_obj_add(w_cont, "type", json_create_string("string"));
    json_obj_add(w_props, "content", w_cont);
    json_obj_add(write_params, "properties", w_props);
    JsonValue *w_req = json_create_array();
    json_arr_add(w_req, json_create_string("path"));
    json_arr_add(w_req, json_create_string("content"));
    json_obj_add(write_params, "required", w_req);
    REGISTER_IF_PERMITTED("write_file", "Write or overwrite entire contents to a file path (runs pre-flight syntax check on C/C++ files)", write_params, PERM_ALLOW, tool_write_file);

    // 4. edit_file
    JsonValue *edit_params = json_create_object();
    json_obj_add(edit_params, "type", json_create_string("object"));
    JsonValue *e_props = json_create_object();
    JsonValue *e_path = json_create_object();
    json_obj_add(e_path, "type", json_create_string("string"));
    json_obj_add(e_props, "path", e_path);
    JsonValue *e_old = json_create_object();
    json_obj_add(e_old, "type", json_create_string("string"));
    json_obj_add(e_old, "description", json_create_string("Existing text substring to replace (must match file contents character-for-character)"));
    json_obj_add(e_props, "old_text", e_old);
    JsonValue *e_new = json_create_object();
    json_obj_add(e_new, "type", json_create_string("string"));
    json_obj_add(e_new, "description", json_create_string("New replacement text"));
    json_obj_add(e_props, "new_text", e_new);
    JsonValue *e_vc = json_create_object();
    json_obj_add(e_vc, "type", json_create_string("boolean"));
    json_obj_add(e_vc, "description", json_create_string("If true, verifies compilation with preflight watchdog and auto-reverts file if compilation fails"));
    json_obj_add(e_props, "verify_compile", e_vc);
    json_obj_add(edit_params, "properties", e_props);
    JsonValue *e_req = json_create_array();
    json_arr_add(e_req, json_create_string("path"));
    json_arr_add(e_req, json_create_string("old_text"));
    json_arr_add(e_req, json_create_string("new_text"));
    json_obj_add(edit_params, "required", e_req);
    REGISTER_IF_PERMITTED("edit_file", "Exact search-and-replace edit. old_text must match character-for-character including indentation. Always call read_file first.", edit_params, PERM_ALLOW, tool_edit_file);

    // 5. apply_patch
    JsonValue *patch_params = json_create_object();
    json_obj_add(patch_params, "type", json_create_string("object"));
    JsonValue *p_props = json_create_object();
    JsonValue *p_path = json_create_object();
    json_obj_add(p_path, "type", json_create_string("string"));
    json_obj_add(p_props, "path", p_path);
    JsonValue *p_patch = json_create_object();
    json_obj_add(p_patch, "type", json_create_string("string"));
    json_obj_add(p_patch, "description", json_create_string("Patch block in <<<<<<< SEARCH ... ======= ... >>>>>>> REPLACE format"));
    json_obj_add(p_props, "patch", p_patch);
    json_obj_add(patch_params, "properties", p_props);
    JsonValue *p_req = json_create_array();
    json_arr_add(p_req, json_create_string("path"));
    json_arr_add(p_req, json_create_string("patch"));
    json_obj_add(patch_params, "required", p_req);
    REGISTER_IF_PERMITTED("apply_patch", "Apply multi-hunk structured replacement patch. Atomic: all SEARCH hunks must match exactly or whole patch is rejected.", patch_params, PERM_ALLOW, tool_apply_patch);

    // 6. list_dir
    REGISTER_IF_PERMITTED("list_dir", "List files and directories in path",
        build_string_param_schema("path", "Directory path"), PERM_ALLOW, tool_list_dir);

    // 7. search_files
    JsonValue *s_params = json_create_object();
    json_obj_add(s_params, "type", json_create_string("object"));
    JsonValue *s_props = json_create_object();
    JsonValue *s_pat = json_create_object();
    json_obj_add(s_pat, "type", json_create_string("string"));
    json_obj_add(s_pat, "description", json_create_string("Text pattern to search for"));
    json_obj_add(s_props, "pattern", s_pat);
    JsonValue *s_path = json_create_object();
    json_obj_add(s_path, "type", json_create_string("string"));
    json_obj_add(s_path, "description", json_create_string("Directory path to search in (default: current directory)"));
    json_obj_add(s_props, "path", s_path);
    JsonValue *s_glob = json_create_object();
    json_obj_add(s_glob, "type", json_create_string("string"));
    json_obj_add(s_glob, "description", json_create_string("Optional file glob pattern (e.g. *.c, *.h)"));
    json_obj_add(s_props, "file_glob", s_glob);
    JsonValue *s_reg = json_create_object();
    json_obj_add(s_reg, "type", json_create_string("boolean"));
    json_obj_add(s_reg, "description", json_create_string("Optional flag to treat pattern as POSIX extended regular expression"));
    json_obj_add(s_props, "regex", s_reg);
    json_obj_add(s_params, "properties", s_props);
    JsonValue *s_req = json_create_array();
    json_arr_add(s_req, json_create_string("pattern"));
    json_obj_add(s_params, "required", s_req);
    REGISTER_IF_PERMITTED("search_files", "Search for text or patterns recursively across files (grep-like). Returns up to 50 matches.", s_params, PERM_ALLOW, tool_search_files);

    // 8. git_status
    JsonValue *gs_params = json_create_object();
    json_obj_add(gs_params, "type", json_create_string("object"));
    JsonValue *gs_props = json_create_object();
    JsonValue *gs_path = json_create_object();
    json_obj_add(gs_path, "type", json_create_string("string"));
    json_obj_add(gs_path, "description", json_create_string("Optional target repository directory path (default: current workspace)"));
    json_obj_add(gs_props, "path", gs_path);
    json_obj_add(gs_params, "properties", gs_props);
    REGISTER_IF_PERMITTED("git_status", "Check Git repository branch, staged changes, and cleanliness status", gs_params, PERM_ALLOW, tool_git_status);

    // 9. git_diff
    JsonValue *gd_params = json_create_object();
    json_obj_add(gd_params, "type", json_create_string("object"));
    JsonValue *gd_props = json_create_object();
    JsonValue *gd_staged = json_create_object();
    json_obj_add(gd_staged, "type", json_create_string("boolean"));
    json_obj_add(gd_staged, "description", json_create_string("Whether to diff staged changes (--cached)"));
    json_obj_add(gd_props, "staged", gd_staged);
    JsonValue *gd_path = json_create_object();
    json_obj_add(gd_path, "type", json_create_string("string"));
    json_obj_add(gd_path, "description", json_create_string("Optional specific file path to diff"));
    json_obj_add(gd_props, "path", gd_path);
    json_obj_add(gd_params, "properties", gd_props);
    REGISTER_IF_PERMITTED("git_diff", "View Git working copy or staged diffs", gd_params, PERM_ALLOW, tool_git_diff);

    // 10. save_memory
    JsonValue *mem_params = json_create_object();
    json_obj_add(mem_params, "type", json_create_string("object"));
    JsonValue *m_props = json_create_object();
    JsonValue *m_top = json_create_object();
    json_obj_add(m_top, "type", json_create_string("string"));
    json_obj_add(m_props, "topic", m_top);
    JsonValue *m_cnt = json_create_object();
    json_obj_add(m_cnt, "type", json_create_string("string"));
    json_obj_add(m_props, "content", m_cnt);
    json_obj_add(mem_params, "properties", m_props);
    REGISTER_IF_PERMITTED("save_memory", "Save a verified skill or trajectory to SQLite memory", mem_params, PERM_ALLOW, tool_save_memory);

    // 11. recall_memory
    REGISTER_IF_PERMITTED("recall_memory", "Search SQLite memory for past solutions and skills",
        build_string_param_schema("query", "Search term"), PERM_ALLOW, tool_recall_memory);

    // 12. spawn_subagent
    JsonValue *sub_params = json_create_object();
    json_obj_add(sub_params, "type", json_create_string("object"));
    JsonValue *sub_props = json_create_object();
    JsonValue *sub_task = json_create_object();
    json_obj_add(sub_task, "type", json_create_string("string"));
    json_obj_add(sub_task, "description", json_create_string("Clear task description for the subagent"));
    json_obj_add(sub_props, "task", sub_task);
    JsonValue *sub_inst = json_create_object();
    json_obj_add(sub_inst, "type", json_create_string("string"));
    json_obj_add(sub_inst, "description", json_create_string("Optional specialized instructions for the worker"));
    json_obj_add(sub_props, "instructions", sub_inst);
    JsonValue *sub_turns = json_create_object();
    json_obj_add(sub_turns, "type", json_create_string("number"));
    json_obj_add(sub_turns, "description", json_create_string("Max execution turns (default: 5)"));
    json_obj_add(sub_props, "max_turns", sub_turns);
    json_obj_add(sub_params, "properties", sub_props);
    JsonValue *sub_req = json_create_array();
    json_arr_add(sub_req, json_create_string("task"));
    json_obj_add(sub_params, "required", sub_req);
    REGISTER_IF_PERMITTED("spawn_subagent", "Spawn an autonomous subagent worker in an isolated sandbox context", sub_params, PERM_ALLOW, tool_spawn_subagent);

    // 13. define_tool (Self-Tooling Dynamic Evolution)
    JsonValue *def_params = json_create_object();
    json_obj_add(def_params, "type", json_create_string("object"));
    JsonValue *d_props = json_create_object();
    JsonValue *d_name = json_create_object();
    json_obj_add(d_name, "type", json_create_string("string"));
    json_obj_add(d_name, "description", json_create_string("Unique identifier name for the new tool"));
    json_obj_add(d_props, "name", d_name);
    JsonValue *d_desc = json_create_object();
    json_obj_add(d_desc, "type", json_create_string("string"));
    json_obj_add(d_desc, "description", json_create_string("Clear explanation of what the tool does"));
    json_obj_add(d_props, "description", d_desc);
    JsonValue *d_body = json_create_object();
    json_obj_add(d_body, "type", json_create_string("string"));
    json_obj_add(d_body, "description", json_create_string("Executable shell/python script body"));
    json_obj_add(d_props, "script_body", d_body);
    json_obj_add(def_params, "properties", d_props);
    JsonValue *d_req = json_create_array();
    json_arr_add(d_req, json_create_string("name"));
    json_arr_add(d_req, json_create_string("description"));
    json_arr_add(d_req, json_create_string("script_body"));
    json_obj_add(def_params, "required", d_req);
    REGISTER_IF_PERMITTED("define_tool", "Dynamically create, persist, and register a new executable tool (parameters mapped to $PARAM_<NAME>, $1, $2, and $TOOL_ARGS_JSON)", def_params, PERM_ALLOW, tool_define_tool);

    // 14. fetch_url (Native REST Web Client: GET, POST, PUT, DELETE, PATCH, Headers, Body)
    JsonValue *http_params = json_create_object();
    json_obj_add(http_params, "type", json_create_string("object"));
    JsonValue *hp_props = json_create_object();
    JsonValue *hp_url = json_create_object();
    json_obj_add(hp_url, "type", json_create_string("string"));
    json_obj_add(hp_url, "description", json_create_string("Target web URL or API endpoint"));
    json_obj_add(hp_props, "url", hp_url);
    JsonValue *hp_mth = json_create_object();
    json_obj_add(hp_mth, "type", json_create_string("string"));
    json_obj_add(hp_mth, "description", json_create_string("HTTP method: GET, POST, PUT, DELETE, PATCH, HEAD (default: GET)"));
    json_obj_add(hp_props, "method", hp_mth);
    JsonValue *hp_body = json_create_object();
    json_obj_add(hp_body, "type", json_create_string("string"));
    json_obj_add(hp_body, "description", json_create_string("Request body or JSON payload for POST/PUT/PATCH"));
    json_obj_add(hp_props, "body", hp_body);
    JsonValue *hp_hdrs = json_create_object();
    json_obj_add(hp_hdrs, "type", json_create_string("object"));
    json_obj_add(hp_hdrs, "description", json_create_string("Custom HTTP headers (e.g. {\"Authorization\": \"Bearer ...\", \"Content-Type\": \"application/json\"})"));
    json_obj_add(hp_props, "headers", hp_hdrs);
    json_obj_add(http_params, "properties", hp_props);
    JsonValue *hp_req = json_create_array();
    json_arr_add(hp_req, json_create_string("url"));
    json_obj_add(http_params, "required", hp_req);
    REGISTER_IF_PERMITTED("fetch_url", "Send HTTP/REST requests (GET, POST, PUT, DELETE) with headers and payload", http_params, PERM_ALLOW, tool_fetch_url);

    // 15. save_skill (Procedural Skill Curation)
    JsonValue *sk_params = json_create_object();
    json_obj_add(sk_params, "type", json_create_string("object"));
    JsonValue *sk_props = json_create_object();
    JsonValue *sk_name = json_create_object();
    json_obj_add(sk_name, "type", json_create_string("string"));
    json_obj_add(sk_name, "description", json_create_string("Name of the procedural skill"));
    json_obj_add(sk_props, "name", sk_name);
    JsonValue *sk_trig = json_create_object();
    json_obj_add(sk_trig, "type", json_create_string("string"));
    json_obj_add(sk_trig, "description", json_create_string("Trigger phrase or keyword to activate the skill"));
    json_obj_add(sk_props, "trigger", sk_trig);
    JsonValue *sk_desc = json_create_object();
    json_obj_add(sk_desc, "type", json_create_string("string"));
    json_obj_add(sk_desc, "description", json_create_string("Short explanation of what the skill accomplishes"));
    json_obj_add(sk_props, "description", sk_desc);
    JsonValue *sk_inst = json_create_object();
    json_obj_add(sk_inst, "type", json_create_string("string"));
    json_obj_add(sk_inst, "description", json_create_string("Detailed step-by-step instructions for the agent to follow"));
    json_obj_add(sk_props, "instructions", sk_inst);
    json_obj_add(sk_params, "properties", sk_props);
    JsonValue *sk_req = json_create_array();
    json_arr_add(sk_req, json_create_string("name"));
    json_arr_add(sk_req, json_create_string("instructions"));
    json_obj_add(sk_params, "required", sk_req);
    REGISTER_IF_PERMITTED("save_skill", "Save a reusable procedural workflow skill into agent memory", sk_params, PERM_ALLOW, tool_save_skill);

    // 16. recall_skill
    REGISTER_IF_PERMITTED("recall_skill", "Search and inspect saved procedural skills from memory",
        build_string_param_schema("query", "Search term or trigger keyword"), PERM_ALLOW, tool_recall_skill);

    // 17. recall_conversation (Historical Cross-Session Memory)
    REGISTER_IF_PERMITTED("recall_conversation", "Search past conversation messages and history across all historical sessions",
        build_string_param_schema("query", "Keywords or topics from past conversations"), PERM_ALLOW, tool_recall_conversation);

    // 18. dispatch_agent (Belya Agency Multi-Agent Orchestration)
    JsonValue *da_params = json_create_object();
    json_obj_add(da_params, "type", json_create_string("object"));
    JsonValue *da_props = json_create_object();
    JsonValue *da_role = json_create_object();
    json_obj_add(da_role, "type", json_create_string("string"));
    json_obj_add(da_role, "description", json_create_string("Target subagent role (architect, builder, reviewer, tester, triage)"));
    json_obj_add(da_props, "role", da_role);
    JsonValue *da_task = json_create_object();
    json_obj_add(da_task, "type", json_create_string("string"));
    json_obj_add(da_task, "description", json_create_string("Clear actionable objective or mission"));
    json_obj_add(da_props, "task", da_task);
    JsonValue *da_ctx = json_create_object();
    json_obj_add(da_ctx, "type", json_create_string("string"));
    json_obj_add(da_ctx, "description", json_create_string("Optional architectural or technical context"));
    json_obj_add(da_props, "context", da_ctx);
    json_obj_add(da_params, "properties", da_props);
    JsonValue *da_req = json_create_array();
    json_arr_add(da_req, json_create_string("role"));
    json_arr_add(da_req, json_create_string("task"));
    json_obj_add(da_params, "required", da_req);
    REGISTER_IF_PERMITTED("dispatch_agent", "Dispatch a specialized autonomous engineering subagent with least-privilege bounding and Git rollback guard", da_params, PERM_ALLOW, tool_dispatch_agent);

    // 19. goto_definition (AST Symbol Graph Navigation)
    REGISTER_IF_PERMITTED("goto_definition", "Navigate to symbol definition (functions, structs, macros) using AST index",
        build_string_param_schema("symbol", "Function, struct, or macro name to resolve"), PERM_ALLOW, tool_goto_definition);

    // 20. find_callers (AST Call-Graph Traversal)
    REGISTER_IF_PERMITTED("find_callers", "Find all call sites and invocations of a symbol across codebase",
        build_string_param_schema("symbol", "Target function or symbol name"), PERM_ALLOW, tool_find_callers);

    // 21. synthesize_mcp_server (Paper2Agent Executable Knowledge)
    JsonValue *mcp_syn_params = json_create_object();
    json_obj_add(mcp_syn_params, "type", json_create_string("object"));
    JsonValue *msp_props = json_create_object();
    JsonValue *msp_srv = json_create_object();
    json_obj_add(msp_srv, "type", json_create_string("string"));
    json_obj_add(msp_srv, "description", json_create_string("Name of the MCP server"));
    json_obj_add(msp_props, "server_name", msp_srv);
    JsonValue *msp_tool = json_create_object();
    json_obj_add(msp_tool, "type", json_create_string("string"));
    json_obj_add(msp_tool, "description", json_create_string("Name of the exposed tool"));
    json_obj_add(msp_props, "tool_name", msp_tool);
    JsonValue *msp_desc = json_create_object();
    json_obj_add(msp_desc, "type", json_create_string("string"));
    json_obj_add(msp_desc, "description", json_create_string("Description of tool"));
    json_obj_add(msp_props, "description", msp_desc);
    JsonValue *msp_cmd = json_create_object();
    json_obj_add(msp_cmd, "type", json_create_string("string"));
    json_obj_add(msp_cmd, "description", json_create_string("Shell or Python script command to execute"));
    json_obj_add(msp_props, "script_cmd", msp_cmd);
    JsonValue *msp_sch = json_create_object();
    json_obj_add(msp_sch, "type", json_create_string("string"));
    json_obj_add(msp_sch, "description", json_create_string("JSON Schema string for parameters"));
    json_obj_add(msp_props, "schema_json", msp_sch);
    json_obj_add(mcp_syn_params, "properties", msp_props);
    JsonValue *msp_req = json_create_array();
    json_arr_add(msp_req, json_create_string("server_name"));
    json_arr_add(msp_req, json_create_string("tool_name"));
    json_arr_add(msp_req, json_create_string("script_cmd"));
    json_obj_add(mcp_syn_params, "required", msp_req);
    REGISTER_IF_PERMITTED("synthesize_mcp_server", "Synthesize a standalone Model Context Protocol (MCP) server from any executable script with validation gating", mcp_syn_params, PERM_ALLOW, tool_synthesize_mcp_server);

    // 22. audit_security_variants (Mythos Variant Security Sweeper)
    JsonValue *audit_params = json_create_object();
    json_obj_add(audit_params, "type", json_create_string("object"));
    JsonValue *ap_props = json_create_object();
    JsonValue *ap_pat = json_create_object();
    json_obj_add(ap_pat, "type", json_create_string("string"));
    json_obj_add(ap_pat, "description", json_create_string("Pattern type: 'all', 'buffer_overflow', 'format_string', 'unchecked_alloc'"));
    json_obj_add(ap_props, "pattern_type", ap_pat);
    JsonValue *ap_tgt = json_create_object();
    json_obj_add(ap_tgt, "type", json_create_string("string"));
    json_obj_add(ap_tgt, "description", json_create_string("Target directory path to audit"));
    REGISTER_IF_PERMITTED("audit_security_variants", "Run Mythos variant analysis across codebase to detect unsafe sinks, buffer overflows, and memory vulnerabilities", audit_params, PERM_ALLOW, tool_audit_security_variants);

    // 23. ask_user (Claude Code AskUserQuestion / Hermes clarify parity)
    JsonValue *au_params = json_create_object();
    json_obj_add(au_params, "type", json_create_string("object"));
    JsonValue *au_props = json_create_object();
    JsonValue *au_q = json_create_object();
    json_obj_add(au_q, "type", json_create_string("string"));
    json_obj_add(au_q, "description", json_create_string("The clarification question or interview inquiry for the operator"));
    json_obj_add(au_props, "question", au_q);
    JsonValue *au_opts = json_create_object();
    json_obj_add(au_opts, "type", json_create_string("array"));
    json_obj_add(au_opts, "description", json_create_string("Optional list of selectable answer options"));
    json_obj_add(au_props, "options", au_opts);
    json_obj_add(au_params, "properties", au_props);
    JsonValue *au_req = json_create_array();
    json_arr_add(au_req, json_create_string("question"));
    json_obj_add(au_params, "required", au_req);
    REGISTER_IF_PERMITTED("ask_user", "Ask the human operator a clarification question with optional numbered choices", au_params, PERM_ALLOW, tool_ask_user);

    // 24. task_create
    JsonValue *tc_params = json_create_object();
    json_obj_add(tc_params, "type", json_create_string("object"));
    JsonValue *tc_props = json_create_object();
    JsonValue *tc_subj = json_create_object();
    json_obj_add(tc_subj, "type", json_create_string("string"));
    json_obj_add(tc_subj, "description", json_create_string("Short subject or title of the task"));
    json_obj_add(tc_props, "subject", tc_subj);
    JsonValue *tc_desc = json_create_object();
    json_obj_add(tc_desc, "type", json_create_string("string"));
    json_obj_add(tc_desc, "description", json_create_string("Detailed description or acceptance criteria"));
    json_obj_add(tc_props, "description", tc_desc);
    JsonValue *tc_deps = json_create_object();
    json_obj_add(tc_deps, "type", json_create_string("string"));
    json_obj_add(tc_deps, "description", json_create_string("Comma-separated list of prerequisite task IDs"));
    json_obj_add(tc_props, "dependencies", tc_deps);
    JsonValue *tc_asgn = json_create_object();
    json_obj_add(tc_asgn, "type", json_create_string("string"));
    json_obj_add(tc_asgn, "description", json_create_string("Target sub-agent or twin assigned to this task"));
    json_obj_add(tc_props, "assigned_agent", tc_asgn);
    json_obj_add(tc_params, "properties", tc_props);
    JsonValue *tc_req = json_create_array();
    json_arr_add(tc_req, json_create_string("subject"));
    json_obj_add(tc_params, "required", tc_req);
    REGISTER_IF_PERMITTED("task_create", "Create a structured tracking task on the SQLite task board", tc_params, PERM_ALLOW, tool_task_create);

    // 25. task_update
    JsonValue *tu_params = json_create_object();
    json_obj_add(tu_params, "type", json_create_string("object"));
    JsonValue *tu_props = json_create_object();
    JsonValue *tu_id = json_create_object();
    json_obj_add(tu_id, "type", json_create_string("integer"));
    json_obj_add(tu_id, "description", json_create_string("ID of the task to update"));
    json_obj_add(tu_props, "id", tu_id);
    JsonValue *tu_st = json_create_object();
    json_obj_add(tu_st, "type", json_create_string("string"));
    json_obj_add(tu_st, "description", json_create_string("New status: pending, in_progress, completed, failed, blocked"));
    json_obj_add(tu_props, "status", tu_st);
    JsonValue *tu_desc = json_create_object();
    json_obj_add(tu_desc, "type", json_create_string("string"));
    json_obj_add(tu_desc, "description", json_create_string("Updated description or progress note"));
    json_obj_add(tu_props, "description", tu_desc);
    json_obj_add(tu_params, "properties", tu_props);
    JsonValue *tu_req = json_create_array();
    json_arr_add(tu_req, json_create_string("id"));
    json_obj_add(tu_params, "required", tu_req);
    REGISTER_IF_PERMITTED("task_update", "Update task progress or status on the SQLite task board", tu_params, PERM_ALLOW, tool_task_update);

    // 26. task_list
    JsonValue *tl_params = json_create_object();
    json_obj_add(tl_params, "type", json_create_string("object"));
    JsonValue *tl_props = json_create_object();
    JsonValue *tl_st = json_create_object();
    json_obj_add(tl_st, "type", json_create_string("string"));
    json_obj_add(tl_st, "description", json_create_string("Optional status filter: pending, in_progress, completed, failed, blocked, or all"));
    json_obj_add(tl_props, "status", tl_st);
    json_obj_add(tl_params, "properties", tl_props);
    REGISTER_IF_PERMITTED("task_list", "List tasks from the SQLite task board with optional status filter", tl_params, PERM_ALLOW, tool_task_list);

    // 27. task_get
    JsonValue *tg_params = json_create_object();
    json_obj_add(tg_params, "type", json_create_string("object"));
    JsonValue *tg_props = json_create_object();
    JsonValue *tg_id = json_create_object();
    json_obj_add(tg_id, "type", json_create_string("integer"));
    json_obj_add(tg_id, "description", json_create_string("ID of the task to retrieve"));
    json_obj_add(tg_props, "id", tg_id);
    json_obj_add(tg_params, "properties", tg_props);
    JsonValue *tg_req = json_create_array();
    json_arr_add(tg_req, json_create_string("id"));
    json_obj_add(tg_params, "required", tg_req);
    REGISTER_IF_PERMITTED("task_get", "Retrieve full details and dependency graph of a specific task", tg_params, PERM_ALLOW, tool_task_get);

    // 28. cron_create
    JsonValue *cc_params = json_create_object();
    json_obj_add(cc_params, "type", json_create_string("object"));
    JsonValue *cc_props = json_create_object();
    JsonValue *cc_name = json_create_object();
    json_obj_add(cc_name, "type", json_create_string("string"));
    json_obj_add(cc_name, "description", json_create_string("Unique name for the recurring job"));
    json_obj_add(cc_props, "name", cc_name);
    JsonValue *cc_sched = json_create_object();
    json_obj_add(cc_sched, "type", json_create_string("string"));
    json_obj_add(cc_sched, "description", json_create_string("Standard cron expression or interval specifier"));
    json_obj_add(cc_props, "schedule", cc_sched);
    JsonValue *cc_pmt = json_create_object();
    json_obj_add(cc_pmt, "type", json_create_string("string"));
    json_obj_add(cc_pmt, "description", json_create_string("Prompt instruction or action to execute when triggered"));
    json_obj_add(cc_props, "prompt", cc_pmt);
    json_obj_add(cc_params, "properties", cc_props);
    JsonValue *cc_req = json_create_array();
    json_arr_add(cc_req, json_create_string("name"));
    json_arr_add(cc_req, json_create_string("schedule"));
    json_arr_add(cc_req, json_create_string("prompt"));
    json_obj_add(cc_params, "required", cc_req);
    REGISTER_IF_PERMITTED("cron_create", "Register a recurring background cron job", cc_params, PERM_ALLOW, tool_cron_create);

    // 29. cron_list
    JsonValue *cl_params = json_create_object();
    json_obj_add(cl_params, "type", json_create_string("object"));
    json_obj_add(cl_params, "properties", json_create_object());
    REGISTER_IF_PERMITTED("cron_list", "List all registered background cron jobs", cl_params, PERM_ALLOW, tool_cron_list);

    // 30. cron_tick
    JsonValue *ct_params = json_create_object();
    json_obj_add(ct_params, "type", json_create_string("object"));
    json_obj_add(ct_params, "properties", json_create_object());
    REGISTER_IF_PERMITTED("cron_tick", "Evaluate and trigger due background cron jobs immediately", ct_params, PERM_ALLOW, tool_cron_tick);

    // 31. enter_plan_mode
    JsonValue *epm_params = json_create_object();
    json_obj_add(epm_params, "type", json_create_string("object"));
    json_obj_add(epm_params, "properties", json_create_object());
    REGISTER_IF_PERMITTED("enter_plan_mode", "Enter read-only Plan Mode. Locks all mutating file and shell operations until exit_plan_mode is called", epm_params, PERM_ALLOW, tool_enter_plan_mode);

    // 32. exit_plan_mode
    JsonValue *xpm_params = json_create_object();
    json_obj_add(xpm_params, "type", json_create_string("object"));
    json_obj_add(xpm_params, "properties", json_create_object());
    REGISTER_IF_PERMITTED("exit_plan_mode", "Exit read-only Plan Mode. Restores read-write filesystem and shell execution", xpm_params, PERM_ALLOW, tool_exit_plan_mode);

    // 33. enter_worktree
    JsonValue *ew_params = json_create_object();
    json_obj_add(ew_params, "type", json_create_string("object"));
    JsonValue *ew_props = json_create_object();
    JsonValue *ew_branch = json_create_object();
    json_obj_add(ew_branch, "type", json_create_string("string"));
    json_obj_add(ew_branch, "description", json_create_string("Branch or identifier name for the isolated git worktree"));
    json_obj_add(ew_props, "branch", ew_branch);
    json_obj_add(ew_params, "properties", ew_props);
    REGISTER_IF_PERMITTED("enter_worktree", "Create and switch into an isolated git worktree sandbox", ew_params, PERM_ALLOW, tool_enter_worktree);

    // 34. leave_worktree
    JsonValue *lw_params = json_create_object();
    json_obj_add(lw_params, "type", json_create_string("object"));
    JsonValue *lw_props = json_create_object();
    JsonValue *lw_keep = json_create_object();
    json_obj_add(lw_keep, "type", json_create_string("boolean"));
    json_obj_add(lw_keep, "description", json_create_string("Whether to keep the worktree branch instead of force-removing it"));
    json_obj_add(lw_props, "keep", lw_keep);
    json_obj_add(lw_params, "properties", lw_props);
    REGISTER_IF_PERMITTED("leave_worktree", "Leave isolated git worktree sandbox and restore original working directory", lw_params, PERM_ALLOW, tool_leave_worktree);

    // 35. manage_task
    JsonValue *mt_params = json_create_object();
    json_obj_add(mt_params, "type", json_create_string("object"));
    JsonValue *mt_props = json_create_object();
    JsonValue *mt_act = json_create_object();
    json_obj_add(mt_act, "type", json_create_string("string"));
    json_obj_add(mt_act, "description", json_create_string("Action to perform: 'list', 'status', 'send_input', 'kill'"));
    json_obj_add(mt_props, "action", mt_act);
    JsonValue *mt_id = json_create_object();
    json_obj_add(mt_id, "type", json_create_string("integer"));
    json_obj_add(mt_id, "description", json_create_string("ID of the background task"));
    json_obj_add(mt_props, "id", mt_id);
    JsonValue *mt_inp = json_create_object();
    json_obj_add(mt_inp, "type", json_create_string("string"));
    json_obj_add(mt_inp, "description", json_create_string("Input text to send to the task stdin (for send_input)"));
    json_obj_add(mt_props, "input", mt_inp);
    json_obj_add(mt_params, "properties", mt_props);
    JsonValue *mt_req = json_create_array();
    json_arr_add(mt_req, json_create_string("action"));
    json_obj_add(mt_params, "required", mt_req);
    REGISTER_IF_PERMITTED("manage_task", "Manage persistent background PTY subshell tasks (list, status, send_input, kill)", mt_params, PERM_ALLOW, tool_manage_task);

    // 36. web_search
    JsonValue *ws_params = json_create_object();
    json_obj_add(ws_params, "type", json_create_string("object"));
    JsonValue *ws_props = json_create_object();
    JsonValue *ws_q = json_create_object();
    json_obj_add(ws_q, "type", json_create_string("string"));
    json_obj_add(ws_q, "description", json_create_string("Search query string"));
    json_obj_add(ws_props, "query", ws_q);
    JsonValue *ws_prv = json_create_object();
    json_obj_add(ws_prv, "type", json_create_string("string"));
    json_obj_add(ws_prv, "description", json_create_string("Search provider: 'brave' (default), 'tavily', or 'searxng'"));
    json_obj_add(ws_props, "provider", ws_prv);
    json_obj_add(ws_params, "properties", ws_props);
    JsonValue *ws_req = json_create_array();
    json_arr_add(ws_req, json_create_string("query"));
    json_obj_add(ws_params, "required", ws_req);
    REGISTER_IF_PERMITTED("web_search", "Search the web for up-to-date documentation and information", ws_params, PERM_ALLOW, tool_web_search);

    // 37. web_fetch
    JsonValue *wf_params = json_create_object();
    json_obj_add(wf_params, "type", json_create_string("object"));
    JsonValue *wf_props = json_create_object();
    JsonValue *wf_u = json_create_object();
    json_obj_add(wf_u, "type", json_create_string("string"));
    json_obj_add(wf_u, "description", json_create_string("Target HTTP/HTTPS URL to fetch"));
    json_obj_add(wf_props, "url", wf_u);
    JsonValue *wf_m = json_create_object();
    json_obj_add(wf_m, "type", json_create_string("string"));
    json_obj_add(wf_m, "description", json_create_string("Extraction mode: 'markdown' (default) or 'text'"));
    json_obj_add(wf_props, "extract_mode", wf_m);
    json_obj_add(wf_params, "properties", wf_props);
    JsonValue *wf_req = json_create_array();
    json_arr_add(wf_req, json_create_string("url"));
    json_obj_add(wf_params, "required", wf_req);
    REGISTER_IF_PERMITTED("web_fetch", "Fetch and extract clean markdown content from an HTTP/HTTPS URL (SSRF guarded, max 50KB)", wf_params, PERM_ALLOW, tool_web_fetch);

    // 38. edit_notebook
    JsonValue *nb_params = json_create_object();
    json_obj_add(nb_params, "type", json_create_string("object"));
    JsonValue *nb_props = json_create_object();
    JsonValue *nb_p = json_create_object();
    json_obj_add(nb_p, "type", json_create_string("string"));
    json_obj_add(nb_p, "description", json_create_string("Path to .ipynb Jupyter notebook file"));
    json_obj_add(nb_props, "path", nb_p);
    JsonValue *nb_a = json_create_object();
    json_obj_add(nb_a, "type", json_create_string("string"));
    json_obj_add(nb_a, "description", json_create_string("Action: 'read', 'replace_cell', 'insert_cell', 'delete_cell'"));
    json_obj_add(nb_props, "action", nb_a);
    JsonValue *nb_idx = json_create_object();
    json_obj_add(nb_idx, "type", json_create_string("integer"));
    json_obj_add(nb_idx, "description", json_create_string("Target cell index (0-indexed)"));
    json_obj_add(nb_props, "cell_index", nb_idx);
    JsonValue *nb_ct = json_create_object();
    json_obj_add(nb_ct, "type", json_create_string("string"));
    json_obj_add(nb_ct, "description", json_create_string("Cell type: 'code' or 'markdown'"));
    json_obj_add(nb_props, "cell_type", nb_ct);
    JsonValue *nb_src = json_create_object();
    json_obj_add(nb_src, "type", json_create_string("string"));
    json_obj_add(nb_src, "description", json_create_string("Cell code or markdown content for insertion/replacement"));
    json_obj_add(nb_props, "source", nb_src);
    json_obj_add(nb_params, "properties", nb_props);
    JsonValue *nb_req = json_create_array();
    json_arr_add(nb_req, json_create_string("path"));
    json_arr_add(nb_req, json_create_string("action"));
    json_obj_add(nb_params, "required", nb_req);
    REGISTER_IF_PERMITTED("edit_notebook", "Inspect and manipulate Jupyter notebook (.ipynb) cells preserving outputs and metadata", nb_params, PERM_ALLOW, tool_edit_notebook);

    // 39. lsp_definition
    JsonValue *lsp_def_params = json_create_object();
    json_obj_add(lsp_def_params, "type", json_create_string("object"));
    JsonValue *lsp_def_props = json_create_object();
    JsonValue *lsp_def_f = json_create_object();
    json_obj_add(lsp_def_f, "type", json_create_string("string"));
    json_obj_add(lsp_def_f, "description", json_create_string("Source file path"));
    json_obj_add(lsp_def_props, "file", lsp_def_f);
    JsonValue *lsp_def_l = json_create_object();
    json_obj_add(lsp_def_l, "type", json_create_string("integer"));
    json_obj_add(lsp_def_l, "description", json_create_string("Line number (1-based)"));
    json_obj_add(lsp_def_props, "line", lsp_def_l);
    JsonValue *lsp_def_c = json_create_object();
    json_obj_add(lsp_def_c, "type", json_create_string("integer"));
    json_obj_add(lsp_def_c, "description", json_create_string("Character offset (1-based)"));
    json_obj_add(lsp_def_props, "character", lsp_def_c);
    JsonValue *lsp_def_s = json_create_object();
    json_obj_add(lsp_def_s, "type", json_create_string("string"));
    json_obj_add(lsp_def_s, "description", json_create_string("Optional symbol name (e.g. function or type identifier)"));
    json_obj_add(lsp_def_props, "symbol", lsp_def_s);
    json_obj_add(lsp_def_params, "properties", lsp_def_props);
    REGISTER_IF_PERMITTED("lsp_definition", "Go to symbol definition via LSP-over-MCP or native AST symbol graph", lsp_def_params, PERM_ALLOW, tool_lsp_definition);

    // 40. lsp_references
    JsonValue *ref_params = json_create_object();
    json_obj_add(ref_params, "type", json_create_string("object"));
    JsonValue *ref_props = json_create_object();
    JsonValue *ref_f = json_create_object();
    json_obj_add(ref_f, "type", json_create_string("string"));
    json_obj_add(ref_f, "description", json_create_string("Source file path"));
    json_obj_add(ref_props, "file", ref_f);
    JsonValue *ref_l = json_create_object();
    json_obj_add(ref_l, "type", json_create_string("integer"));
    json_obj_add(ref_l, "description", json_create_string("Line number (1-based)"));
    json_obj_add(ref_props, "line", ref_l);
    JsonValue *ref_c = json_create_object();
    json_obj_add(ref_c, "type", json_create_string("integer"));
    json_obj_add(ref_c, "description", json_create_string("Character offset (1-based)"));
    json_obj_add(ref_props, "character", ref_c);
    JsonValue *ref_s = json_create_object();
    json_obj_add(ref_s, "type", json_create_string("string"));
    json_obj_add(ref_s, "description", json_create_string("Optional symbol name to search references for"));
    json_obj_add(ref_props, "symbol", ref_s);
    json_obj_add(ref_params, "properties", ref_props);
    REGISTER_IF_PERMITTED("lsp_references", "Find all references to symbol via LSP-over-MCP or native AST call-tree", ref_params, PERM_ALLOW, tool_lsp_references);

    // 41. exec_backend (Phase 7 Modular Remote Execution Backend)
    JsonValue *eb_params = json_create_object();
    json_obj_add(eb_params, "type", json_create_string("object"));
    JsonValue *eb_props = json_create_object();
    JsonValue *eb_act = json_create_object();
    json_obj_add(eb_act, "type", json_create_string("string"));
    json_obj_add(eb_act, "description", json_create_string("Action: 'get' or 'set' (default: get)"));
    json_obj_add(eb_props, "action", eb_act);
    JsonValue *eb_be = json_create_object();
    json_obj_add(eb_be, "type", json_create_string("string"));
    json_obj_add(eb_be, "description", json_create_string("Backend type: 'local', 'docker', 'ssh'"));
    json_obj_add(eb_props, "backend", eb_be);
    JsonValue *eb_tgt = json_create_object();
    json_obj_add(eb_tgt, "type", json_create_string("string"));
    json_obj_add(eb_tgt, "description", json_create_string("Target container ID/name (docker) or host (ssh)"));
    json_obj_add(eb_props, "target", eb_tgt);
    json_obj_add(eb_params, "properties", eb_props);
    REGISTER_IF_PERMITTED("exec_backend", "Query or switch execution backend (local, Docker container, or SSH host)", eb_params, PERM_ALLOW, tool_exec_backend);

    // Load any previously defined custom tools if whitelist is unrestricted
    if (!tools_whitelist) {
        belya_harness_load_custom_tools(h);
    }

#undef REGISTER_IF_PERMITTED

    return h;
}

BelyaHarness *belya_harness_init(BelyaAgent *agent) {
    return belya_harness_init_bounded(agent, NULL, NULL);
}

void belya_harness_register_tool(BelyaHarness *h, const char *name, const char *desc, JsonValue *params, SecurityLevel sec, BelyaToolCallback fn) {
    if (h->tool_count >= 64) {
        if (params) json_free(params);
        return;
    }
    char *name_copy = xstrdup(name);
    if (!name_copy) {
        if (params) json_free(params);
        return;
    }
    h->tools[h->tool_count].name = name_copy;
    h->tools[h->tool_count].security = sec;
    h->tools[h->tool_count].callback = fn;
    h->tools[h->tool_count].mcp_client = NULL;
    h->tools[h->tool_count].custom_script_path = NULL;
    h->tool_count++;

    belya_agent_register_schema(h->agent, name, desc, params);
}

bool belya_harness_connect_mcp(BelyaHarness *h, const char *server_cmd) {
    if (!h || !server_cmd || h->mcp_server_count >= 8) return false;

    printf("\033[1;36m[MCP] Connecting to server:\033[0m %s\n", server_cmd);
    MCPClient *client = mcp_client_start(server_cmd);
    if (!client || !client->connected) {
        printf("\033[1;31m[MCP] Failed to connect to server.\033[0m\n");
        return false;
    }

    h->mcp_servers[h->mcp_server_count++] = client;

    JsonValue *tools = mcp_client_list_tools(client);
    if (tools && tools->type == JSON_ARRAY) {
        printf("\033[1;32m[MCP] Discovered %zu tools:\033[0m\n", tools->u.array.count);
        for (size_t i = 0; i < tools->u.array.count; i++) {
            JsonValue *t = tools->u.array.items[i];
            const char *t_name = json_obj_get_str(t, "name");
            const char *t_desc = json_obj_get_str(t, "description");
            JsonValue *t_schema = json_obj_get(t, "inputSchema");

            if (t_name && h->tool_count < 64) {
                /* R2 (2026-10-11): discovered MCP tools must respect the tool
                   whitelist. An empty whitelist keeps the harness unrestricted --
                   parity with the native registration path (REGISTER_IF_PERMITTED). */
                if (h->tools_whitelist[0] && !belya_harness_is_tool_whitelisted(h->tools_whitelist, t_name)) {
                    printf("  - \033[1;31m%s\033[0m: skipped (not in tool whitelist)\n", t_name);
                    continue;
                }
                printf("  + \033[1;33m%s\033[0m: %s\n", t_name, t_desc ? t_desc : "");
                char *mcp_name = xstrdup(t_name);
                if (!mcp_name) continue;
                h->tools[h->tool_count].name = mcp_name;
                h->tools[h->tool_count].security = PERM_ALLOW;
                h->tools[h->tool_count].callback = NULL;
                h->tools[h->tool_count].custom_script_path = NULL;
                h->tools[h->tool_count].mcp_client = client;
                h->tool_count++;

                char *sch_str = t_schema ? json_serialize(t_schema) : xstrdup("{\"type\":\"object\"}");
                JsonValue *sch_clone = json_parse(sch_str);
                free(sch_str);
                belya_agent_register_schema(h->agent, t_name, t_desc ? t_desc : "", sch_clone);
            }
        }
        json_free(tools);
    }
    return true;
}

static struct termios g_saved_termios;
static bool g_termios_saved = false;

static void belya_restore_terminal_mode(void) {
    if (g_termios_saved) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_saved_termios);
        g_termios_saved = false;
    }
}

static void belya_termios_sig_handler(int sig) {
    belya_restore_terminal_mode();
    signal(sig, SIG_DFL);
    raise(sig);
}

bool belya_terminal_permission_prompt(BelyaHarness *h, const char *tool_name, const char *args_json, void *userdata) {
    (void)userdata;
    if (getenv("BELYA_HEADLESS") != NULL || getenv("BELYA_TEST_MODE") != NULL || !isatty(STDIN_FILENO)) {
        return true; // headless automated mode always allows
    }

    if (!g_termios_saved) {
        if (tcgetattr(STDIN_FILENO, &g_saved_termios) == 0) {
            g_termios_saved = true;
            atexit(belya_restore_terminal_mode);
        }
    }

    struct sigaction sa, old_int, old_term, old_quit, old_tstp;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = belya_termios_sig_handler;
    sigaction(SIGINT, &sa, &old_int);
    sigaction(SIGTERM, &sa, &old_term);
    sigaction(SIGQUIT, &sa, &old_quit);
    sigaction(SIGTSTP, &sa, &old_tstp);

    printf("\n\033[1;33m[SECURITY GATE: Permission Required]\033[0m\n");
    printf("Tool:      \033[1;37m%s\033[0m\n", tool_name ? tool_name : "unknown");
    if (args_json && strlen(args_json) > 0) {
        printf("Arguments: \033[0;36m%s\033[0m\n", args_json);
    }
    printf("Action: [\033[1;32ma\033[0m]llow once | [\033[1;34mA\033[0m]lways allow | [\033[1;31md\033[0m]eny | [\033[1;35me\033[0m]xplain: ");
    fflush(stdout);

    tcflush(STDIN_FILENO, TCIFLUSH);

    struct termios raw = g_saved_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 1;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);

    char ch = 'd';
    while (read(STDIN_FILENO, &ch, 1) == 1) {
        if (ch == 0x1B) {
            // Drain trailing escape sequence bytes
            char seq[16];
            fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
            while (read(STDIN_FILENO, seq, sizeof(seq)) > 0);
            fcntl(STDIN_FILENO, F_SETFL, 0);
            continue;
        }
        break;
    }

    belya_restore_terminal_mode();
    sigaction(SIGINT, &old_int, NULL);
    sigaction(SIGTERM, &old_term, NULL);
    sigaction(SIGQUIT, &old_quit, NULL);
    sigaction(SIGTSTP, &old_tstp, NULL);
    printf("%c\n", ch);

    if (ch == 'a') {
        return true;
    }
    if (ch == 'A') {
        // Upgrade tool to PERM_ALLOW for remainder of session
        if (h && tool_name) {
            for (size_t i = 0; i < h->tool_count; i++) {
                if (strcmp(h->tools[i].name, tool_name) == 0) {
                    h->tools[i].security = PERM_ALLOW;
                    break;
                }
            }
        }
        return true;
    }
    if (ch == 'e') {
        printf("\033[1;35m[Intent Explanation]\033[0m Tool '%s' requested with arguments: %s\n", tool_name, args_json ? args_json : "none");
        return false;
    }
    return false; // 'd' or other
}

static bool harness_ask_permission(const char *name, const char *args) {
    return belya_terminal_permission_prompt(g_harness, name, args, NULL);
}

BelyaHookResult belya_run_lifecycle_hook(BelyaHarness *h, BelyaHookType hook_type, const char *tool_name, const char *tool_args, const char *observation) {
    BelyaHookResult res;
    memset(&res, 0, sizeof(res));

    const char *event_name = NULL;
    const char *script_base = NULL;
    switch (hook_type) {
        case BELYA_HOOK_PRE_TOOL_USE:
            event_name = "PreToolUse";
            script_base = "pre_tool_use";
            break;
        case BELYA_HOOK_POST_TOOL_USE:
            event_name = "PostToolUse";
            script_base = "post_tool_use";
            break;
        case BELYA_HOOK_STOP:
            event_name = "Stop";
            script_base = "stop";
            break;
        case BELYA_HOOK_PRE_COMPACT:
            event_name = "PreCompact";
            script_base = "pre_compact";
            break;
        default:
            return res;
    }

    char script_path[512];
    snprintf(script_path, sizeof(script_path), ".belya/hooks/%s.sh", script_base);
    if (access(script_path, X_OK) != 0) {
        snprintf(script_path, sizeof(script_path), ".belya/hooks/%s", script_base);
        if (access(script_path, X_OK) != 0) {
            return res;
        }
    }

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        return res;
    }
    fcntl(pipefd[0], F_SETFD, FD_CLOEXEC);
    fcntl(pipefd[1], F_SETFD, FD_CLOEXEC);

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return res;
    }

    if (pid == 0) {
        setpgid(0, 0);
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            close(devnull);
        }

        setenv("BELYA_HOOK_EVENT", event_name, 1);
        if (tool_name) setenv("BELYA_TOOL_NAME", tool_name, 1);
        if (tool_args) setenv("BELYA_TOOL_ARGS", tool_args, 1);
        if (observation) setenv("BELYA_TOOL_OBSERVATION", observation, 1);
        if (h && h->agent && h->agent->db_path[0]) setenv("BELYA_SESSION_ID", h->agent->db_path, 1);
        if (h && strlen(h->cwd) > 0) setenv("BELYA_CWD", h->cwd, 1);

        execl(script_path, script_path, (char *)NULL);
        _exit(127);
    }

    close(pipefd[1]);

    DynString out = dyn_str_new();
    char buf[512];
    struct pollfd pfd;
    pfd.fd = pipefd[0];
    pfd.events = POLLIN;

    int timeout_ms = 3000;
    bool timed_out = false;

    while (1) {
        int prc = poll(&pfd, 1, timeout_ms);
        if (prc < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (prc == 0) {
            timed_out = true;
            kill(-pid, SIGKILL);
            break;
        }
        if (pfd.revents & (POLLIN | POLLHUP)) {
            ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
            if (n <= 0) break;
            buf[n] = '\0';
            dyn_str_append(&out, buf);
            if (out.len > 16384) break;
        }
    }
    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);

    if (timed_out) {
        res.blocked = false;
        res.exit_code = -1;
        dyn_str_append(&out, " [Hook timed out]");
        res.output = out.data;
        return res;
    }

    if (WIFEXITED(status)) {
        res.exit_code = WEXITSTATUS(status);
        if (res.exit_code == 2) {
            res.blocked = true;
        }
    } else {
        res.exit_code = -1;
    }

    if (out.len > 0 && out.data[out.len - 1] == '\n') {
        out.data[--out.len] = '\0';
    }
    res.output = out.data;
    return res;
}

void belya_hook_result_free(BelyaHookResult *res) {
    if (res && res->output) {
        free(res->output);
        res->output = NULL;
    }
}

// 2026 Competitive Parity Phase 4: Plan Mode Gate & Git Worktree Sandbox
bool belya_harness_enter_plan_mode(BelyaHarness *h) {
    if (!h) return false;
    h->plan_mode = true;
    belya_agent_log_timeline(h->agent, "plan_mode_entered", "Activated read-only Plan Mode");
    return true;
}

bool belya_harness_exit_plan_mode(BelyaHarness *h) {
    if (!h) return false;
    h->plan_mode = false;
    belya_agent_log_timeline(h->agent, "plan_mode_exited", "Deactivated Plan Mode");
    return true;
}

char *belya_harness_enter_worktree(BelyaHarness *h, const char *branch_or_id) {
    if (!h) return xstrdup("Error: Invalid harness.");
    if (h->in_worktree) return xstrdup("Error: Already active inside an isolated worktree.");

    if (!getcwd(h->original_worktree_cwd, sizeof(h->original_worktree_cwd))) {
        snprintf(h->original_worktree_cwd, sizeof(h->original_worktree_cwd), "%s", h->cwd);
    }

    char wt_id[128];
    if (branch_or_id && strlen(branch_or_id) > 0) {
        snprintf(wt_id, sizeof(wt_id), "wt_%s", branch_or_id);
    } else {
        snprintf(wt_id, sizeof(wt_id), "wt_agent_%ld", (long)time(NULL));
    }

    char wt_path[4096];
    snprintf(wt_path, sizeof(wt_path), ".belya/worktrees/%s", wt_id);

    int sys_mk = system("mkdir -p .belya/worktrees");
    (void)sys_mk;

    char cmd[8192];
    snprintf(cmd, sizeof(cmd), "git worktree add -b \"%s\" \"%s\" HEAD >/dev/null 2>&1", wt_id, wt_path);
    int rc = system(cmd);
    if (rc != 0) {
        snprintf(cmd, sizeof(cmd), "git worktree add \"%s\" HEAD >/dev/null 2>&1", wt_path);
        rc = system(cmd);
    }

    if (rc != 0) {
        return xstrdup("Error: Failed to create isolated git worktree (ensure repository has commits).");
    }

    if (chdir(wt_path) != 0) {
        return xstrdup("Error: Failed to enter newly created worktree directory.");
    }

    if (!getcwd(h->cwd, sizeof(h->cwd))) {
        snprintf(h->cwd, sizeof(h->cwd), "%s", wt_path);
    }
    if (!realpath(h->cwd, g_workspace_root)) {
        snprintf(g_workspace_root, sizeof(g_workspace_root), "%s", h->cwd);
    }
    g_workspace_root_set = true;
    snprintf(h->active_worktree_path, sizeof(h->active_worktree_path), "%s", wt_path);
    h->in_worktree = true;

    DynString ds = dyn_str_new();
    dyn_str_appendf(&ds, "Entered isolated git worktree at '%s'. Working directory shifted to isolated workspace.", wt_path);
    belya_agent_log_timeline(h->agent, "worktree_entered", wt_path);
    return ds.data;
}

char *belya_harness_leave_worktree(BelyaHarness *h, bool keep) {
    if (!h) return xstrdup("Error: Invalid harness.");
    if (!h->in_worktree) return xstrdup("Error: Not currently in a worktree.");

    char prev_wt_path[4096];
    snprintf(prev_wt_path, sizeof(prev_wt_path), "%s", h->active_worktree_path);

    if (chdir(h->original_worktree_cwd) != 0) {
        return xstrdup("Error: Failed to restore original working directory.");
    }
    snprintf(h->cwd, sizeof(h->cwd), "%s", h->original_worktree_cwd);
    if (!realpath(h->cwd, g_workspace_root)) {
        snprintf(g_workspace_root, sizeof(g_workspace_root), "%s", h->cwd);
    }
    g_workspace_root_set = true;
    h->in_worktree = false;

    if (!keep) {
        char rm_cmd[8192];
        snprintf(rm_cmd, sizeof(rm_cmd), "git worktree remove --force \"%s\" >/dev/null 2>&1", prev_wt_path);
        int sys_rm = system(rm_cmd);
        (void)sys_rm;
    }

    DynString ds = dyn_str_new();
    dyn_str_appendf(&ds, "Left git worktree. Restored workspace to '%s' (worktree %s).",
        h->cwd, keep ? "preserved" : "reaped");
    belya_agent_log_timeline(h->agent, "worktree_left", h->cwd);
    return ds.data;
}

// ==================== 2026 Competitive Parity Phase 5: POSIX PTY Background Task Manager ====================

static void belya_task_ring_write(BelyaBackgroundTask *t, const char *data, size_t len) {
    if (!t || !t->ring_buffer || !data || len == 0) return;
    for (size_t i = 0; i < len; i++) {
        t->ring_buffer[t->ring_head] = data[i];
        t->ring_head = (t->ring_head + 1) % BELYA_TASK_RING_BUFFER_SIZE;
        if (t->ring_count < BELYA_TASK_RING_BUFFER_SIZE) {
            t->ring_count++;
        } else {
            t->ring_tail = (t->ring_tail + 1) % BELYA_TASK_RING_BUFFER_SIZE;
        }
    }
    t->total_bytes_captured += len;
}

static char *belya_task_ring_read_all(BelyaBackgroundTask *t) {
    if (!t || !t->ring_buffer || t->ring_count == 0) return xstrdup("");
    char *out = malloc(t->ring_count + 1);
    if (!out) return xstrdup("");
    for (size_t i = 0; i < t->ring_count; i++) {
        size_t idx = (t->ring_tail + i) % BELYA_TASK_RING_BUFFER_SIZE;
        out[i] = t->ring_buffer[idx];
    }
    out[t->ring_count] = '\0';
    return out;
}

static void belya_task_drain_output(BelyaBackgroundTask *t) {
    if (!t || t->master_fd <= STDERR_FILENO) return;
    char buf[4096];
    ssize_t n;
    while ((n = read(t->master_fd, buf, sizeof(buf) - 1)) > 0) {
        belya_task_ring_write(t, buf, (size_t)n);
    }
    if (t->state == BELYA_TASK_STATE_RUNNING) {
        int status = 0;
        pid_t res = waitpid(t->pid, &status, WNOHANG);
        if (res > 0) {
            t->end_time = time(NULL);
            if (WIFEXITED(status)) {
                t->exit_code = WEXITSTATUS(status);
                t->state = (t->exit_code == 0) ? BELYA_TASK_STATE_COMPLETED : BELYA_TASK_STATE_FAILED;
            } else if (WIFSIGNALED(status)) {
                t->exit_code = 128 + WTERMSIG(status);
                t->state = BELYA_TASK_STATE_KILLED;
            }
            if (t->master_fd > STDERR_FILENO) {
                close(t->master_fd);
            }
            t->master_fd = -1;
        }
    }
}

int belya_harness_spawn_task(BelyaHarness *h, const char *command) {
    if (!h || !command || strlen(command) == 0) return -1;

    for (size_t i = 0; i < BELYA_MAX_BACKGROUND_TASKS; i++) {
        if (h->task_table.tasks[i].state != BELYA_TASK_STATE_UNUSED) {
            belya_task_drain_output(&h->task_table.tasks[i]);
        }
    }

    int slot = -1;
    for (size_t i = 0; i < BELYA_MAX_BACKGROUND_TASKS; i++) {
        if (h->task_table.tasks[i].state == BELYA_TASK_STATE_UNUSED) {
            slot = (int)i;
            break;
        }
    }
    if (slot < 0) {
        for (size_t i = 0; i < BELYA_MAX_BACKGROUND_TASKS; i++) {
            if (h->task_table.tasks[i].state != BELYA_TASK_STATE_RUNNING) {
                slot = (int)i;
                break;
            }
        }
    }
    if (slot < 0) return -1;

    if (!h->task_table.tasks[slot].ring_buffer) {
        h->task_table.tasks[slot].ring_buffer = calloc(1, BELYA_TASK_RING_BUFFER_SIZE);
        if (!h->task_table.tasks[slot].ring_buffer) return -1;
    }

    int master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (master_fd < 0) return -1;

    if (grantpt(master_fd) != 0 || unlockpt(master_fd) != 0) {
        close(master_fd);
        return -1;
    }

    char *slave_name = ptsname(master_fd);
    if (!slave_name) {
        close(master_fd);
        return -1;
    }

    fcntl(master_fd, F_SETFD, FD_CLOEXEC);
    int flags = fcntl(master_fd, F_GETFL, 0);
    fcntl(master_fd, F_SETFL, flags | O_NONBLOCK);

    pid_t pid = fork();
    if (pid < 0) {
        close(master_fd);
        return -1;
    }

    if (pid == 0) {
        setsid();
        int slave_fd = open(slave_name, O_RDWR);
        if (slave_fd >= 0) {
            dup2(slave_fd, STDIN_FILENO);
            dup2(slave_fd, STDOUT_FILENO);
            dup2(slave_fd, STDERR_FILENO);
            if (slave_fd > STDERR_FILENO) close(slave_fd);
        }
        close(master_fd);
        setpgid(0, 0);
        execlp("bash", "bash", "-c", command, (char *)NULL);
        _exit(127);
    }

    BelyaBackgroundTask *t = &h->task_table.tasks[slot];
    t->id = ++h->task_table.next_id;
    t->pid = pid;
    t->pgid = pid;
    t->master_fd = master_fd;
    t->state = BELYA_TASK_STATE_RUNNING;
    t->exit_code = -1;
    t->start_time = time(NULL);
    t->end_time = 0;
    strncpy(t->command, command, sizeof(t->command) - 1);
    t->command[sizeof(t->command) - 1] = '\0';
    t->ring_head = 0;
    t->ring_tail = 0;
    t->ring_count = 0;
    t->total_bytes_captured = 0;

    usleep(50000);
    belya_task_drain_output(t);

    belya_agent_log_timeline(h->agent, "task_spawned", command);
    return t->id;
}

char *belya_harness_task_list(BelyaHarness *h) {
    if (!h) return xstrdup("Error: Invalid harness.");

    DynString ds = dyn_str_new();
    dyn_str_append(&ds, "=== Active & Background Tasks Table ===\n");
    dyn_str_append(&ds, "ID   | PID    | State      | Elapsed | Output   | Command\n");
    dyn_str_append(&ds, "-----+--------+------------+---------+----------+-----------------------------\n");

    size_t count = 0;
    time_t now = time(NULL);
    for (size_t i = 0; i < BELYA_MAX_BACKGROUND_TASKS; i++) {
        BelyaBackgroundTask *t = &h->task_table.tasks[i];
        if (t->state == BELYA_TASK_STATE_UNUSED) continue;

        belya_task_drain_output(t);
        const char *st_str = "unknown";
        if (t->state == BELYA_TASK_STATE_RUNNING) st_str = "\033[1;32mrunning\033[0m";
        else if (t->state == BELYA_TASK_STATE_COMPLETED) st_str = "completed";
        else if (t->state == BELYA_TASK_STATE_FAILED) st_str = "\033[1;31mfailed\033[0m";
        else if (t->state == BELYA_TASK_STATE_KILLED) st_str = "\033[1;33mkilled\033[0m";

        long elapsed = (long)(t->end_time ? (t->end_time - t->start_time) : (now - t->start_time));
        if (elapsed < 0) elapsed = 0;

        dyn_str_appendf(&ds, "%-4d | %-6d | %-20s | %4lds  | %6zu B | %s\n",
            t->id, (int)t->pid, st_str, elapsed, t->total_bytes_captured, t->command);
        count++;
    }

    if (count == 0) {
        dyn_str_append(&ds, "(No background tasks currently registered)\n");
    }
    return ds.data;
}

char *belya_harness_task_status(BelyaHarness *h, int id) {
    if (!h) return xstrdup("Error: Invalid harness.");

    BelyaBackgroundTask *target = NULL;
    for (size_t i = 0; i < BELYA_MAX_BACKGROUND_TASKS; i++) {
        if (h->task_table.tasks[i].id == id && h->task_table.tasks[i].state != BELYA_TASK_STATE_UNUSED) {
            target = &h->task_table.tasks[i];
            break;
        }
    }
    if (!target) {
        DynString err = dyn_str_new();
        dyn_str_appendf(&err, "Error: Background task ID %d not found.", id);
        return err.data;
    }

    belya_task_drain_output(target);
    time_t now = time(NULL);
    long elapsed = (long)(target->end_time ? (target->end_time - target->start_time) : (now - target->start_time));
    if (elapsed < 0) elapsed = 0;

    const char *st_str = "unknown";
    if (target->state == BELYA_TASK_STATE_RUNNING) st_str = "running";
    else if (target->state == BELYA_TASK_STATE_COMPLETED) st_str = "completed";
    else if (target->state == BELYA_TASK_STATE_FAILED) st_str = "failed";
    else if (target->state == BELYA_TASK_STATE_KILLED) st_str = "killed";

    char *recent_out = belya_task_ring_read_all(target);

    DynString ds = dyn_str_new();
    dyn_str_appendf(&ds, "Task ID: %d\nPID: %d (PGID: %d)\nCommand: %s\nState: %s\nExit Code: %d\nElapsed: %lds\nTotal Bytes Captured: %zu\n\nRecent Output:\n%s",
        target->id, (int)target->pid, (int)target->pgid, target->command, st_str, target->exit_code, elapsed, target->total_bytes_captured,
        recent_out ? recent_out : "");
    if (recent_out) free(recent_out);
    return ds.data;
}

char *belya_harness_task_send_input(BelyaHarness *h, int id, const char *input) {
    if (!h) return xstrdup("Error: Invalid harness.");
    if (!input) return xstrdup("Error: Missing input text.");

    BelyaBackgroundTask *target = NULL;
    for (size_t i = 0; i < BELYA_MAX_BACKGROUND_TASKS; i++) {
        if (h->task_table.tasks[i].id == id && h->task_table.tasks[i].state != BELYA_TASK_STATE_UNUSED) {
            target = &h->task_table.tasks[i];
            break;
        }
    }
    if (!target) {
        DynString err = dyn_str_new();
        dyn_str_appendf(&err, "Error: Background task ID %d not found.", id);
        return err.data;
    }

    if (target->state != BELYA_TASK_STATE_RUNNING || target->master_fd < 0) {
        return xstrdup("Error: Cannot send input; task is not actively running.");
    }

    size_t in_len = strlen(input);
    ssize_t w = write(target->master_fd, input, in_len);
    if (in_len > 0 && input[in_len - 1] != '\n') {
        w += write(target->master_fd, "\n", 1);
    }
    (void)w;

    usleep(50000);
    belya_task_drain_output(target);

    DynString ds = dyn_str_new();
    dyn_str_appendf(&ds, "Sent %zu bytes of input to task %d.\n", in_len, id);
    return ds.data;
}

char *belya_harness_task_kill(BelyaHarness *h, int id) {
    if (!h) return xstrdup("Error: Invalid harness.");

    BelyaBackgroundTask *target = NULL;
    for (size_t i = 0; i < BELYA_MAX_BACKGROUND_TASKS; i++) {
        if (h->task_table.tasks[i].id == id && h->task_table.tasks[i].state != BELYA_TASK_STATE_UNUSED) {
            target = &h->task_table.tasks[i];
            break;
        }
    }
    if (!target) {
        DynString err = dyn_str_new();
        dyn_str_appendf(&err, "Error: Background task ID %d not found.", id);
        return err.data;
    }

    if (target->state == BELYA_TASK_STATE_RUNNING) {
        kill(-target->pgid, SIGTERM);
        usleep(20000);
        int status = 0;
        if (waitpid(target->pid, &status, WNOHANG) == 0) {
            kill(-target->pgid, SIGKILL);
            waitpid(target->pid, &status, 0);
        }
        target->state = BELYA_TASK_STATE_KILLED;
        target->end_time = time(NULL);
        if (target->master_fd > STDERR_FILENO) {
            close(target->master_fd);
            target->master_fd = -1;
        }
    }

    DynString ds = dyn_str_new();
    dyn_str_appendf(&ds, "Task %d (PID %d, PGID %d) terminated successfully.", id, (int)target->pid, (int)target->pgid);
    belya_agent_log_timeline(h->agent, "task_killed", ds.data);
    return ds.data;
}

// ==================== 2026 Competitive Parity Phase 6: Multi-Provider Web Search/Fetch, Byte-Span .ipynb Splicer & LSP-over-MCP ====================

char *belya_html_to_markdown(const char *html, size_t max_bytes) {
    if (!html) return xstrdup("");
    if (max_bytes == 0) max_bytes = 51200;

    DynString md = dyn_str_new();
    const char *p = html;
    bool in_script = false;
    bool in_style = false;
    bool in_pre = false;

    while (*p && md.len < max_bytes) {
        if (*p == '<') {
            if (strncmp(p, "<!--", 4) == 0) {
                const char *end = strstr(p, "-->");
                if (end) { p = end + 3; continue; }
                else break;
            }
            if (strncasecmp(p, "<script", 7) == 0 && (p[7] == ' ' || p[7] == '>')) {
                in_script = true;
                p = strchr(p, '>');
                if (!p) break;
                p++;
                continue;
            }
            if (strncasecmp(p, "</script>", 9) == 0) {
                in_script = false;
                p += 9;
                continue;
            }
            if (strncasecmp(p, "<style", 6) == 0 && (p[6] == ' ' || p[6] == '>')) {
                in_style = true;
                p = strchr(p, '>');
                if (!p) break;
                p++;
                continue;
            }
            if (strncasecmp(p, "</style>", 8) == 0) {
                in_style = false;
                p += 8;
                continue;
            }

            if (in_script || in_style) {
                p++;
                continue;
            }

            if ((p[1] == 'h' || p[1] == 'H') && (p[2] >= '1' && p[2] <= '6')) {
                int level = p[2] - '0';
                dyn_str_append(&md, "\n\n");
                for (int l = 0; l < level; l++) dyn_str_append(&md, "#");
                dyn_str_append(&md, " ");
                p = strchr(p, '>');
                if (!p) break;
                p++;
                continue;
            }
            if (strncasecmp(p, "</h", 3) == 0 && p[3] >= '1' && p[3] <= '6') {
                dyn_str_append(&md, "\n\n");
                p = strchr(p, '>');
                if (!p) break;
                p++;
                continue;
            }

            if (strncasecmp(p, "<p>", 3) == 0 || strncasecmp(p, "<p ", 3) == 0) {
                dyn_str_append(&md, "\n\n");
                p = strchr(p, '>');
                if (!p) break;
                p++;
                continue;
            }
            if (strncasecmp(p, "</p>", 4) == 0) {
                dyn_str_append(&md, "\n\n");
                p += 4;
                continue;
            }
            if (strncasecmp(p, "<br>", 4) == 0 || strncasecmp(p, "<br/>", 5) == 0 || strncasecmp(p, "<br />", 6) == 0) {
                dyn_str_append(&md, "\n");
                p = strchr(p, '>');
                if (!p) break;
                p++;
                continue;
            }

            if (strncasecmp(p, "<li>", 4) == 0 || strncasecmp(p, "<li ", 4) == 0) {
                dyn_str_append(&md, "\n* ");
                p = strchr(p, '>');
                if (!p) break;
                p++;
                continue;
            }
            if (strncasecmp(p, "</li>", 5) == 0) {
                p += 5;
                continue;
            }

            if (strncasecmp(p, "<pre>", 5) == 0 || strncasecmp(p, "<pre ", 5) == 0) {
                in_pre = true;
                dyn_str_append(&md, "\n```\n");
                p = strchr(p, '>');
                if (!p) break;
                p++;
                continue;
            }
            if (strncasecmp(p, "</pre>", 6) == 0) {
                in_pre = false;
                dyn_str_append(&md, "\n```\n");
                p += 6;
                continue;
            }
            if (strncasecmp(p, "<code>", 6) == 0 && !in_pre) {
                dyn_str_append(&md, "`");
                p += 6;
                continue;
            }
            if (strncasecmp(p, "</code>", 7) == 0 && !in_pre) {
                dyn_str_append(&md, "`");
                p += 7;
                continue;
            }

            if (strncasecmp(p, "<a ", 3) == 0) {
                const char *href_pos = strstr(p, "href=");
                const char *tag_end = strchr(p, '>');
                if (href_pos && tag_end && href_pos < tag_end) {
                    char quote = href_pos[5];
                    char href_val[1024] = {0};
                    if (quote == '"' || quote == '\'') {
                        const char *h_end = strchr(href_pos + 6, quote);
                        if (h_end && (size_t)(h_end - (href_pos + 6)) < sizeof(href_val) - 1) {
                            strncpy(href_val, href_pos + 6, h_end - (href_pos + 6));
                        }
                    }
                    p = tag_end + 1;
                    const char *a_close = strstr(p, "</a>");
                    if (a_close) {
                        dyn_str_append(&md, "[");
                        while (p < a_close) {
                            if (*p != '<') dyn_str_appendf(&md, "%c", *p);
                            p++;
                        }
                        dyn_str_appendf(&md, "](%s)", href_val);
                        p = a_close + 4;
                        continue;
                    }
                }
            }

            const char *tag_end = strchr(p, '>');
            if (tag_end) {
                p = tag_end + 1;
                continue;
            } else {
                break;
            }
        }

        if (in_script || in_style) {
            p++;
            continue;
        }

        if (*p == '&') {
            if (strncmp(p, "&amp;", 5) == 0) { dyn_str_append(&md, "&"); p += 5; continue; }
            if (strncmp(p, "&lt;", 4) == 0) { dyn_str_append(&md, "<"); p += 4; continue; }
            if (strncmp(p, "&gt;", 4) == 0) { dyn_str_append(&md, ">"); p += 4; continue; }
            if (strncmp(p, "&quot;", 6) == 0) { dyn_str_append(&md, "\""); p += 6; continue; }
            if (strncmp(p, "&#39;", 5) == 0) { dyn_str_append(&md, "'"); p += 5; continue; }
            if (strncmp(p, "&nbsp;", 6) == 0) { dyn_str_append(&md, " "); p += 6; continue; }
        }

        dyn_str_appendf(&md, "%c", *p);
        p++;
    }

    if (md.len >= max_bytes) {
        dyn_str_append(&md, "\n\n[Content truncated at 50KB]");
    }

    return md.data;
}

char *belya_harness_web_fetch(BelyaHarness *h, const char *url, const char *extract_mode) {
    if (!h) return xstrdup("Error: Invalid harness.");
    if (!url || strlen(url) == 0) return xstrdup("Error: Missing url argument.");

    JsonValue *args = json_create_object();
    json_obj_add(args, "url", json_create_string(url));
    char *raw = tool_fetch_url(h->agent, args);
    json_free(args);

    if (!raw) return xstrdup("Error: Failed to fetch URL.");
    if (strncmp(raw, "Error fetching URL", 18) == 0) {
        return raw;
    }

    if (extract_mode && strcmp(extract_mode, "text") == 0) {
        char *converted = belya_html_to_markdown(raw, 51200);
        free(raw);
        return converted;
    }

    char *md = belya_html_to_markdown(raw, 51200);
    free(raw);
    return md;
}

char *belya_harness_web_search(BelyaHarness *h, const char *query, const char *provider) {
    if (!h) return xstrdup("Error: Invalid harness.");
    if (!query || strlen(query) == 0) return xstrdup("Error: Missing query argument.");

    const char *prov = provider ? provider : "brave";
    const char *brave_key = getenv("BRAVE_API_KEY");
    const char *tavily_key = getenv("TAVILY_API_KEY");

    if (strstr(query, "mock_test_search") != NULL || getenv("BELYA_MOCK_SEARCH") != NULL) {
        DynString ds = dyn_str_new();
        dyn_str_appendf(&ds, "### Web Search Results for: \"%s\" (Provider: mock)\n\n", query);
        dyn_str_appendf(&ds, "1. [Official C99 Specification](https://iso.org/c99)\n   Snippet: ISO/IEC 9899:1999 programming languages C standard specification.\n\n");
        dyn_str_appendf(&ds, "2. [POSIX.1-2008 System Interfaces](https://pubs.opengroup.org/onlinepubs/9699919799/)\n   Snippet: IEEE Std 1003.1, 2016 Edition covering standard POSIX functions.\n\n");
        dyn_str_appendf(&ds, "3. [Model Context Protocol Specification](https://modelcontextprotocol.io/)\n   Snippet: Open standard for secure two-way communication between LLM applications and tools.\n");
        return ds.data;
    }

    if (strcmp(prov, "brave") == 0 || (brave_key && !tavily_key)) {
        if (!brave_key) {
            DynString ds = dyn_str_new();
            dyn_str_appendf(&ds, "Error: Brave search requested but BRAVE_API_KEY environment variable is not set.\n"
                                "Set BRAVE_API_KEY or TAVILY_API_KEY to enable live multi-provider web search.");
            return ds.data;
        }

        CURL *curl = curl_easy_init();
        if (!curl) return xstrdup("Error: Failed to initialize curl.");
        char *esc_query = curl_easy_escape(curl, query, (int)strlen(query));

        DynString url_str = dyn_str_new();
        dyn_str_appendf(&url_str, "https://api.search.brave.com/res/v1/web/search?q=%s&count=5", esc_query ? esc_query : query);
        if (esc_query) curl_free(esc_query);

        DynString resp = dyn_str_new();
        struct curl_slist *headers = NULL;
        char auth_hdr[256];
        snprintf(auth_hdr, sizeof(auth_hdr), "X-Subscription-Token: %s", brave_key);
        headers = curl_slist_append(headers, auth_hdr);
        headers = curl_slist_append(headers, "Accept: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, url_str.data);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fetch_url_curl_sink);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

        CURLcode rc = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        dyn_str_free(&url_str);

        if (rc != CURLE_OK) {
            dyn_str_free(&resp);
            return xstrdup("Error: Failed to communicate with Brave Search API.");
        }

        JsonValue *root = json_parse(resp.data);
        dyn_str_free(&resp);
        if (!root) return xstrdup("Error: Invalid JSON response from Brave Search.");

        JsonValue *web = json_obj_get(root, "web");
        JsonValue *results = web ? json_obj_get(web, "results") : NULL;

        DynString out = dyn_str_new();
        dyn_str_appendf(&out, "### Web Search Results for: \"%s\" (Provider: Brave)\n\n", query);

        if (results && results->type == JSON_ARRAY && results->u.array.count > 0) {
            for (size_t i = 0; i < results->u.array.count && i < 5; i++) {
                JsonValue *item = results->u.array.items[i];
                const char *title = json_obj_get_str(item, "title");
                const char *url = json_obj_get_str(item, "url");
                const char *desc = json_obj_get_str(item, "description");
                dyn_str_appendf(&out, "%zu. [%s](%s)\n   Snippet: %s\n\n",
                    i + 1, title ? title : "Untitled", url ? url : "", desc ? desc : "");
            }
        } else {
            dyn_str_append(&out, "(No results found)\n");
        }
        json_free(root);
        return out.data;
    } else if (strcmp(prov, "tavily") == 0 || tavily_key) {
        if (!tavily_key) {
            return xstrdup("Error: Tavily search requested but TAVILY_API_KEY is not set.");
        }
        CURL *curl = curl_easy_init();
        if (!curl) return xstrdup("Error: Failed to initialize curl.");

        JsonValue *req_obj = json_create_object();
        json_obj_add(req_obj, "api_key", json_create_string(tavily_key));
        json_obj_add(req_obj, "query", json_create_string(query));
        json_obj_add(req_obj, "max_results", json_create_number(5));
        char *req_json = json_serialize(req_obj);
        json_free(req_obj);

        DynString resp = dyn_str_new();
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, "https://api.tavily.com/search");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req_json);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fetch_url_curl_sink);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

        CURLcode rc = curl_easy_perform(curl);
        free(req_json);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (rc != CURLE_OK) {
            dyn_str_free(&resp);
            return xstrdup("Error: Failed to communicate with Tavily Search API.");
        }

        JsonValue *root = json_parse(resp.data);
        dyn_str_free(&resp);
        if (!root) return xstrdup("Error: Invalid JSON from Tavily Search.");

        JsonValue *results = json_obj_get(root, "results");
        DynString out = dyn_str_new();
        dyn_str_appendf(&out, "### Web Search Results for: \"%s\" (Provider: Tavily)\n\n", query);
        if (results && results->type == JSON_ARRAY && results->u.array.count > 0) {
            for (size_t i = 0; i < results->u.array.count && i < 5; i++) {
                JsonValue *item = results->u.array.items[i];
                const char *title = json_obj_get_str(item, "title");
                const char *url = json_obj_get_str(item, "url");
                const char *content = json_obj_get_str(item, "content");
                dyn_str_appendf(&out, "%zu. [%s](%s)\n   Snippet: %s\n\n",
                    i + 1, title ? title : "Untitled", url ? url : "", content ? content : "");
            }
        } else {
            dyn_str_append(&out, "(No results found)\n");
        }
        json_free(root);
        return out.data;
    }

    return xstrdup("Error: Web search requires BRAVE_API_KEY or TAVILY_API_KEY environment variable. Please configure an API key.");
}

char *belya_harness_edit_notebook(BelyaHarness *h, const char *path, const char *action, int cell_index, const char *cell_type, const char *source) {
    if (!h) return xstrdup("Error: Invalid harness.");
    if (!path || strlen(path) == 0) return xstrdup("Error: Missing path argument.");
    if (!action || strlen(action) == 0) return xstrdup("Error: Missing action argument ('read', 'replace_cell', 'insert_cell', 'delete_cell').");

    bool is_write = strcmp(action, "read") != 0;
    if (!is_path_jailed(path, NULL, is_write)) {
        DynString err = dyn_str_new();
        dyn_str_appendf(&err, "Blocked: Path '%s' is outside the workspace jail.", path);
        return err.data;
    }

    if (h->plan_mode && is_write) {
        return xstrdup("Blocked: Plan Mode is active (read-only execution). Notebook editing is prohibited.");
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        DynString err = dyn_str_new();
        dyn_str_appendf(&err, "Error: Could not open notebook file '%s': %s", path, strerror(errno));
        return err.data;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 10 * 1024 * 1024) {
        fclose(f);
        return xstrdup("Error: Invalid notebook file size (empty or exceeds 10MB limit).");
    }
    char *content = malloc(sz + 1);
    if (!content) {
        fclose(f);
        return xstrdup("Error: Memory allocation failure reading notebook.");
    }
    size_t rd = fread(content, 1, sz, f);
    fclose(f);
    content[rd] = '\0';

    JsonValue *nb = json_parse(content);
    free(content);
    if (!nb || nb->type != JSON_OBJECT) {
        if (nb) json_free(nb);
        return xstrdup("Error: Notebook is not valid JSON.");
    }

    JsonValue *cells = json_obj_get(nb, "cells");
    if (!cells || cells->type != JSON_ARRAY) {
        json_free(nb);
        return xstrdup("Error: Notebook missing top-level 'cells' array.");
    }

    if (strcmp(action, "read") == 0) {
        DynString ds = dyn_str_new();
        double nbformat = json_obj_get_num(nb, "nbformat", 4);
        double nbformat_minor = json_obj_get_num(nb, "nbformat_minor", 2);
        dyn_str_appendf(&ds, "### Jupyter Notebook: %s (%zu cells, format v%.0f.%.0f)\n\n",
            path, cells->u.array.count, nbformat, nbformat_minor);

        for (size_t i = 0; i < cells->u.array.count; i++) {
            JsonValue *c = cells->u.array.items[i];
            const char *ctype = json_obj_get_str(c, "cell_type");
            double exec_cnt = json_obj_get_num(c, "execution_count", -1);
            JsonValue *outs = json_obj_get(c, "outputs");
            size_t out_cnt = (outs && outs->type == JSON_ARRAY) ? outs->u.array.count : 0;

            dyn_str_appendf(&ds, "--- [Cell %zu] (type: %s", i, ctype ? ctype : "code");
            if (strcmp(ctype ? ctype : "", "code") == 0) {
                if (exec_cnt >= 0) dyn_str_appendf(&ds, ", execution_count: %.0f", exec_cnt);
                dyn_str_appendf(&ds, ", outputs: %zu", out_cnt);
            }
            dyn_str_append(&ds, ") ---\n");

            JsonValue *src_val = json_obj_get(c, "source");
            if (src_val) {
                if (src_val->type == JSON_STRING) {
                    dyn_str_append(&ds, src_val->u.string);
                } else if (src_val->type == JSON_ARRAY) {
                    for (size_t s = 0; s < src_val->u.array.count; s++) {
                        if (src_val->u.array.items[s] && src_val->u.array.items[s]->type == JSON_STRING) {
                            dyn_str_append(&ds, src_val->u.array.items[s]->u.string);
                        }
                    }
                }
            }
            dyn_str_append(&ds, "\n\n");
        }
        json_free(nb);
        return ds.data;
    } else if (strcmp(action, "replace_cell") == 0) {
        if (cell_index < 0 || (size_t)cell_index >= cells->u.array.count) {
            json_free(nb);
            DynString err = dyn_str_new();
            dyn_str_appendf(&err, "Error: Cell index %d out of bounds (notebook has %zu cells).", cell_index, cells->u.array.count);
            return err.data;
        }
        if (!source) {
            json_free(nb);
            return xstrdup("Error: Missing source argument for replace_cell.");
        }

        JsonValue *c = cells->u.array.items[cell_index];
        bool found_src = false;
        for (size_t m = 0; m < c->u.object.count; m++) {
            if (strcmp(c->u.object.members[m].key, "source") == 0) {
                json_free(c->u.object.members[m].value);
                c->u.object.members[m].value = json_create_string(source);
                found_src = true;
                break;
            }
        }
        if (!found_src) {
            json_obj_add(c, "source", json_create_string(source));
        }

        if (cell_type) {
            for (size_t m = 0; m < c->u.object.count; m++) {
                if (strcmp(c->u.object.members[m].key, "cell_type") == 0) {
                    json_free(c->u.object.members[m].value);
                    c->u.object.members[m].value = json_create_string(cell_type);
                    break;
                }
            }
        }

        char *new_json = json_serialize(nb);
        json_free(nb);
        FILE *wf = fopen(path, "w");
        if (!wf) {
            free(new_json);
            return xstrdup("Error: Failed to open notebook file for writing.");
        }
        fputs(new_json, wf);
        fclose(wf);
        free(new_json);

        DynString res = dyn_str_new();
        dyn_str_appendf(&res, "Cell %d successfully replaced in notebook '%s'.", cell_index, path);
        belya_agent_log_timeline(h->agent, "notebook_cell_replaced", path);
        return res.data;
    } else if (strcmp(action, "insert_cell") == 0) {
        if (!source) {
            json_free(nb);
            return xstrdup("Error: Missing source argument for insert_cell.");
        }
        const char *ctype = cell_type ? cell_type : "code";

        JsonValue *new_cell = json_create_object();
        json_obj_add(new_cell, "cell_type", json_create_string(ctype));
        json_obj_add(new_cell, "metadata", json_create_object());
        json_obj_add(new_cell, "source", json_create_string(source));
        if (strcmp(ctype, "code") == 0) {
            json_obj_add(new_cell, "execution_count", json_create_null());
            json_obj_add(new_cell, "outputs", json_create_array());
        }

        size_t ins_idx = (cell_index >= 0 && (size_t)cell_index <= cells->u.array.count) ? (size_t)cell_index : cells->u.array.count;

        if (cells->u.array.count >= cells->u.array.cap) {
            size_t ncap = cells->u.array.cap ? cells->u.array.cap * 2 : 4;
            cells->u.array.items = realloc(cells->u.array.items, ncap * sizeof(JsonValue *));
            cells->u.array.cap = ncap;
        }
        for (size_t i = cells->u.array.count; i > ins_idx; i--) {
            cells->u.array.items[i] = cells->u.array.items[i - 1];
        }
        cells->u.array.items[ins_idx] = new_cell;
        cells->u.array.count++;

        char *new_json = json_serialize(nb);
        json_free(nb);
        FILE *wf = fopen(path, "w");
        if (!wf) {
            free(new_json);
            return xstrdup("Error: Failed to write notebook file.");
        }
        fputs(new_json, wf);
        fclose(wf);
        free(new_json);

        DynString res = dyn_str_new();
        dyn_str_appendf(&res, "New %s cell successfully inserted at index %zu in '%s'.", ctype, ins_idx, path);
        belya_agent_log_timeline(h->agent, "notebook_cell_inserted", path);
        return res.data;
    } else if (strcmp(action, "delete_cell") == 0) {
        if (cell_index < 0 || (size_t)cell_index >= cells->u.array.count) {
            json_free(nb);
            DynString err = dyn_str_new();
            dyn_str_appendf(&err, "Error: Cell index %d out of bounds (notebook has %zu cells).", cell_index, cells->u.array.count);
            return err.data;
        }

        json_free(cells->u.array.items[cell_index]);
        for (size_t i = (size_t)cell_index; i + 1 < cells->u.array.count; i++) {
            cells->u.array.items[i] = cells->u.array.items[i + 1];
        }
        cells->u.array.count--;

        char *new_json = json_serialize(nb);
        json_free(nb);
        FILE *wf = fopen(path, "w");
        if (!wf) {
            free(new_json);
            return xstrdup("Error: Failed to write notebook file.");
        }
        fputs(new_json, wf);
        fclose(wf);
        free(new_json);

        DynString res = dyn_str_new();
        dyn_str_appendf(&res, "Cell %d successfully deleted from notebook '%s'.", cell_index, path);
        belya_agent_log_timeline(h->agent, "notebook_cell_deleted", path);
        return res.data;
    }

    json_free(nb);
    return xstrdup("Error: Unrecognized notebook action ('read', 'replace_cell', 'insert_cell', 'delete_cell').");
}

char *belya_harness_lsp_definition(BelyaHarness *h, const char *file, int line, int character, const char *symbol) {
    if (!h) return xstrdup("Error: Invalid harness.");

    for (size_t t = 0; t < h->tool_count; t++) {
        if (h->tools[t].mcp_client && h->tools[t].mcp_client->connected) {
            const char *tname = h->tools[t].name;
            if (tname && (strcmp(tname, "textDocument/definition") == 0 ||
                          strcmp(tname, "lsp_definition") == 0 ||
                          strcmp(tname, "definition") == 0)) {
                JsonValue *args = json_create_object();
                if (file) json_obj_add(args, "file", json_create_string(file));
                json_obj_add(args, "line", json_create_number(line));
                json_obj_add(args, "character", json_create_number(character));
                if (symbol) json_obj_add(args, "symbol", json_create_string(symbol));
                char *resp = mcp_client_call_tool(h->tools[t].mcp_client, tname, args);
                json_free(args);
                if (resp) return resp;
            }
        }
    }

    char sym_buf[128] = {0};
    if (symbol && strlen(symbol) > 0) {
        strncpy(sym_buf, symbol, sizeof(sym_buf) - 1);
    } else if (file && line > 0) {
        FILE *f = fopen(file, "r");
        if (f) {
            char line_buf[1024];
            int cur_line = 0;
            while (fgets(line_buf, sizeof(line_buf), f)) {
                cur_line++;
                if (cur_line == line) {
                    int c_idx = character > 0 ? character - 1 : 0;
                    if (c_idx >= (int)strlen(line_buf)) c_idx = (int)strlen(line_buf) - 1;
                    while (c_idx > 0 && (isalnum((unsigned char)line_buf[c_idx - 1]) || line_buf[c_idx - 1] == '_')) {
                        c_idx--;
                    }
                    int out_idx = 0;
                    while (line_buf[c_idx] && (isalnum((unsigned char)line_buf[c_idx]) || line_buf[c_idx] == '_') && out_idx < (int)sizeof(sym_buf) - 1) {
                        sym_buf[out_idx++] = line_buf[c_idx++];
                    }
                    sym_buf[out_idx] = '\0';
                    break;
                }
            }
            fclose(f);
        }
    }

    if (strlen(sym_buf) == 0) {
        return xstrdup("Error: Could not determine symbol name for LSP definition lookup.");
    }

    char *res = belya_harness_goto_definition(h, sym_buf);
    if (!res || strstr(res, "not found") != NULL) {
        if (res) free(res);
        DynString ds = dyn_str_new();
        dyn_str_appendf(&ds, "[LSP Definition] Symbol '%s' not found in workspace AST index.", sym_buf);
        return ds.data;
    }

    DynString ds = dyn_str_new();
    dyn_str_appendf(&ds, "[LSP Definition] (Source: Native Symbol Graph)\n%s", res);
    free(res);
    return ds.data;
}

char *belya_harness_lsp_references(BelyaHarness *h, const char *file, int line, int character, const char *symbol) {
    if (!h) return xstrdup("Error: Invalid harness.");

    for (size_t t = 0; t < h->tool_count; t++) {
        if (h->tools[t].mcp_client && h->tools[t].mcp_client->connected) {
            const char *tname = h->tools[t].name;
            if (tname && (strcmp(tname, "textDocument/references") == 0 ||
                          strcmp(tname, "lsp_references") == 0 ||
                          strcmp(tname, "references") == 0)) {
                JsonValue *args = json_create_object();
                if (file) json_obj_add(args, "file", json_create_string(file));
                json_obj_add(args, "line", json_create_number(line));
                json_obj_add(args, "character", json_create_number(character));
                if (symbol) json_obj_add(args, "symbol", json_create_string(symbol));
                char *resp = mcp_client_call_tool(h->tools[t].mcp_client, tname, args);
                json_free(args);
                if (resp) return resp;
            }
        }
    }

    char sym_buf[128] = {0};
    if (symbol && strlen(symbol) > 0) {
        strncpy(sym_buf, symbol, sizeof(sym_buf) - 1);
    } else if (file && line > 0) {
        FILE *f = fopen(file, "r");
        if (f) {
            char line_buf[1024];
            int cur_line = 0;
            while (fgets(line_buf, sizeof(line_buf), f)) {
                cur_line++;
                if (cur_line == line) {
                    int c_idx = character > 0 ? character - 1 : 0;
                    if (c_idx >= (int)strlen(line_buf)) c_idx = (int)strlen(line_buf) - 1;
                    while (c_idx > 0 && (isalnum((unsigned char)line_buf[c_idx - 1]) || line_buf[c_idx - 1] == '_')) {
                        c_idx--;
                    }
                    int out_idx = 0;
                    while (line_buf[c_idx] && (isalnum((unsigned char)line_buf[c_idx]) || line_buf[c_idx] == '_') && out_idx < (int)sizeof(sym_buf) - 1) {
                        sym_buf[out_idx++] = line_buf[c_idx++];
                    }
                    sym_buf[out_idx] = '\0';
                    break;
                }
            }
            fclose(f);
        }
    }

    if (strlen(sym_buf) == 0) {
        return xstrdup("Error: Could not determine symbol name for LSP references lookup.");
    }

    char *res = belya_harness_find_callers(h, sym_buf);
    if (!res || strstr(res, "No callers") != NULL) {
        if (res) free(res);
        DynString ds = dyn_str_new();
        dyn_str_appendf(&ds, "[LSP References] No references found for symbol '%s'.", sym_buf);
        return ds.data;
    }

    DynString ds = dyn_str_new();
    dyn_str_appendf(&ds, "[LSP References] (Source: Native Symbol Graph)\n%s", res);
    free(res);
    return ds.data;
}

// 2026 Competitive Parity Phase 7: Stdin-Piped Modular Remote Execution Backends
char *belya_remote_exec_piped(BelyaExecBackend backend, const char *target, const char *workdir, const char *cmd, int *exit_code) {
    if (exit_code) *exit_code = 0;
    if (!cmd) return xstrdup("Error: No command specified.");

    if (backend == BELYA_BACKEND_DOCKER && (!target || strlen(target) == 0)) {
        if (exit_code) *exit_code = 1;
        return xstrdup("Error: Missing Docker container target.");
    }
    if (backend == BELYA_BACKEND_SSH && (!target || strlen(target) == 0)) {
        if (exit_code) *exit_code = 1;
        return xstrdup("Error: Missing SSH host target.");
    }

    int in_pipe[2];
    int out_pipe[2];
    if (pipe(in_pipe) == -1) {
        if (exit_code) *exit_code = -1;
        return xstrdup("Error: Failed to create stdin pipe.");
    }
    if (pipe(out_pipe) == -1) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        if (exit_code) *exit_code = -1;
        return xstrdup("Error: Failed to create stdout pipe.");
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        if (exit_code) *exit_code = -1;
        return xstrdup("Error: Subprocess fork failed.");
    }

    if (pid == 0) {
        close(in_pipe[1]);
        if (dup2(in_pipe[0], STDIN_FILENO) == -1) _exit(127);
        close(in_pipe[0]);

        close(out_pipe[0]);
        if (dup2(out_pipe[1], STDOUT_FILENO) == -1) _exit(127);
        if (dup2(out_pipe[1], STDERR_FILENO) == -1) _exit(127);
        close(out_pipe[1]);

        if (backend == BELYA_BACKEND_DOCKER) {
            const char *w = (workdir && strlen(workdir) > 0) ? workdir : "/";
            char *argv[] = {"docker", "exec", "-i", "-w", (char *)w, (char *)target, "/bin/sh", NULL};
            execvp("docker", argv);
        } else if (backend == BELYA_BACKEND_SSH) {
            char *argv[] = {"ssh", "-T", "-o", "ControlMaster=auto", "-o", "ControlPath=/tmp/belya_ssh_%r@%h:%p", "-o", "ControlPersist=600", (char *)target, "/bin/sh", NULL};
            execvp("ssh", argv);
        } else {
            char *argv[] = {"/bin/sh", NULL};
            execvp("/bin/sh", argv);
        }
        _exit(127);
    }

    // Parent
    close(in_pipe[0]);
    close(out_pipe[1]);

    size_t cmd_len = strlen(cmd);
    size_t written = 0;
    while (written < cmd_len) {
        ssize_t n = write(in_pipe[1], cmd + written, cmd_len - written);
        if (n <= 0) break;
        written += (size_t)n;
    }
    ssize_t wn = write(in_pipe[1], "\n", 1);
    (void)wn;
    close(in_pipe[1]); // Close stdin to signal EOF to child shell

    int flags = fcntl(out_pipe[0], F_GETFL, 0);
    fcntl(out_pipe[0], F_SETFL, flags | O_NONBLOCK);

    DynString out = dyn_str_new();
    struct timeval start, now;
    gettimeofday(&start, NULL);
    const char *to_env = getenv("BASH_TIMEOUT");
    double max_timeout = (to_env && atof(to_env) > 0) ? atof(to_env) : 30.0;

    while (1) {
        char buf[512];
        ssize_t bytes = read(out_pipe[0], buf, sizeof(buf) - 1);
        if (bytes > 0) {
            buf[bytes] = '\0';
            dyn_str_append(&out, buf);
            if (out.len > 100000) {
                dyn_str_append(&out, "\n[Output Truncated by Belya Remote Backend (100KB buffer limit)]");
                kill(pid, SIGKILL);
                waitpid(pid, NULL, 0);
                break;
            }
        }

        int status = 0;
        pid_t res = waitpid(pid, &status, WNOHANG);
        if (res == pid) {
            while ((bytes = read(out_pipe[0], buf, sizeof(buf) - 1)) > 0) {
                buf[bytes] = '\0';
                dyn_str_append(&out, buf);
                if (out.len > 100000) break;
            }
            if (exit_code) {
                if (WIFEXITED(status)) *exit_code = WEXITSTATUS(status);
                else if (WIFSIGNALED(status)) *exit_code = 128 + WTERMSIG(status);
            }
            break;
        }

        gettimeofday(&now, NULL);
        double elapsed = (double)(now.tv_sec - start.tv_sec) + (double)(now.tv_usec - start.tv_usec) / 1000000.0;
        if (elapsed > max_timeout) {
            kill(pid, SIGTERM);
            usleep(100000);
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
            if (exit_code) *exit_code = 124;
            dyn_str_append(&out, "\n[Error: Command timed out]");
            break;
        }

        struct timespec ts = {0, 10000000L}; // 10ms
        nanosleep(&ts, NULL);
    }

    close(out_pipe[0]);
    return out.data;
}

void belya_harness_set_exec_backend(BelyaHarness *h, BelyaExecBackend backend, const char *target) {
    if (!h) return;
    h->exec_backend = backend;
    if (backend == BELYA_BACKEND_DOCKER && target) {
        snprintf(h->docker_container, sizeof(h->docker_container), "%s", target);
    } else if (backend == BELYA_BACKEND_SSH && target) {
        snprintf(h->ssh_host, sizeof(h->ssh_host), "%s", target);
    }
}

char *belya_harness_exec_remote(BelyaHarness *h, const char *cmd, int *exit_code) {
    if (!h) return xstrdup("Error: Invalid harness.");
    const char *target = (h->exec_backend == BELYA_BACKEND_DOCKER) ? h->docker_container : h->ssh_host;
    return belya_remote_exec_piped(h->exec_backend, target, h->cwd, cmd, exit_code);
}

static void print_help(BelyaHarness *h) {
    printf("\n\033[1;36m=== BelyaHarness & BelyaAgent Help ===\033[0m\n");
    printf("Current Model:   \033[1;33m%s\033[0m\n", h->agent->gateway->model);
    printf("Current CWD:     \033[1;33m%s\033[0m\n", h->cwd);
    printf("Context Size:    \033[1;33m%zu messages | %zu estimated tokens\033[0m\n", h->agent->msg_count, belya_agent_total_tokens(h->agent));
    printf("FTS5 Memory:     \033[1;33m%s\033[0m\n", h->agent->has_fts5 ? "Enabled (BM25)" : "Standard");
    printf("Prompt Caching:  \033[1;33m%s\033[0m\n", h->agent->gateway->prompt_caching ? "Enabled" : "Disabled");
    printf("Streaming SSE:   \033[1;33m%s\033[0m\n", h->agent->gateway->streaming ? "Enabled (Real-time)" : "Disabled");
    printf("Connected MCPs:  \033[1;33m%zu servers\033[0m\n\n", h->mcp_server_count);
    printf("\033[1;32mAvailable Slash Commands:\033[0m\n");
    printf("  /help            Show this help reference\n");
    printf("  /status          View active system status and token utilization\n");
    printf("  /tools           List all registered tools and permissions\n");
    printf("  /rules           View active repository guidelines (.agentrules)\n");
    printf("  /timeline [N]    View recent Gomaa timeline event log\n");
    printf("  /sessions        List all checkpointed conversation sessions\n");
    printf("  /save [id]       Checkpoint conversation tree to SQLite\n");
    printf("  /resume <id>     Restore conversation session by ID\n");
    printf("  /skills [query]  List or search saved procedural skills\n");
    printf("  /checkpoint [id] Create a Git & conversation snapshot\n");
    printf("  /rollback [id]   Instant rollback to checkpoint\n");
    printf("  /export [id]     Export trajectory in OpenAI JSONL format\n");
    printf("  /cache           View prompt cache economics & hit rates\n");
    printf("  /reflect         Distill recent trajectory into reusable SQLite FTS5 skill\n");
    printf("  /clear           Reset conversation history (preserves system prompt)\n");
    printf("  /compact [N]     Prune older messages, keeping N recent (default: 10)\n");
    printf("  /memory [query]  Search SQLite persistent memory directly\n");
    printf("  /model <name>    Dynamically change the active AI model\n");
    printf("  /diff [path]     View colorized Git working copy or staged diff\n");
    printf("  /doctor          Run environment & systems integrity diagnostics\n");
    printf("  /cost            View token accounting and session cost estimate\n");
    printf("  /openrouter [m]  Switch to OpenRouter (e.g. anthropic/claude-3.7-sonnet, deepseek/deepseek-r1)\n");
    printf("  /ollama [model]  Switch to local Ollama (e.g. qwen2.5-coder:7b, deepseek-r1:7b-m2)\n");
    printf("  /key <api_key>   Dynamically set or update API key (for OpenRouter / cloud providers)\n");
    printf("  /cwd [path]      View or change working directory\n");
    printf("  /mcp <command>   Connect to an external MCP stdio server\n");
    printf("  /agency [task]   Execute task via Belya Agency multi-agent pipeline (or list subagents)\n");
    printf("  /tasks [filter]  List active tasks from SQLite task board\n");
    printf("  /cron            List scheduled recurring background cron jobs\n");
    printf("  /dream           Trigger cross-session memory consolidation sweep\n");
    printf("  /plan            Toggle read-only Plan Mode sandbox\n");
    printf("  /bg              List running background PTY subshell tasks\n");
    printf("  exit             Terminate the harness REPL\n\n");
}

static void list_tools(BelyaHarness *h) {
    printf("\n\033[1;36m=== Registered Tools (%zu) ===\033[0m\n", h->tool_count);
    for (size_t i = 0; i < h->tool_count; i++) {
        const char *sec_str = "ALLOW";
        const char *sec_color = "\033[0;32m";
        if (h->tools[i].security == PERM_ASK_USER) {
            sec_str = "ASK_USER";
            sec_color = "\033[1;33m";
        } else if (h->tools[i].security == PERM_DENY) {
            sec_str = "DENY";
            sec_color = "\033[1;31m";
        }
        const char *origin = h->tools[i].mcp_client ? " [MCP]" : (h->tools[i].custom_script_path ? " [Custom Script]" : "");
        printf("  - \033[1;37m%-16s\033[0m [%s%s\033[0m]%s %s\n", 
            h->tools[i].name, sec_color, sec_str, origin,
            i < h->agent->schema_count ? h->agent->schemas[i].description : "");
    }
    printf("\n");
}

static void harness_completion_hook(const char *buf, linenoiseCompletions *lc) {
    if (buf[0] == '/') {
        const char *commands[] = {
            "/help", "/status", "/tools", "/rules", "/timeline",
            "/sessions", "/save", "/resume", "/skills", "/checkpoint",
            "/rollback", "/export", "/cache", "/reflect", "/clear",
            "/compact", "/memory", "/model", "/diff", "/doctor", "/cost",
            "/tasks", "/cron", "/dream", "/plan", "/bg",
            "/openrouter", "/ollama", "/key", "/cwd", "/mcp", "/agency", NULL
        };
        for (int i = 0; commands[i]; i++) {
            if (strncmp(buf, commands[i], strlen(buf)) == 0) {
                linenoiseAddCompletion(lc, commands[i]);
            }
        }
    }
}

void belya_harness_repl(BelyaHarness *h) {
    linenoiseHistorySetMaxLen(200);
    linenoiseHistoryLoad(".belya_history");
    linenoiseSetCompletionCallback(harness_completion_hook);

    printf("\033[1;32m=== BelyaHarness & BelyaAgent Evolution 4.0 System Activated ===\033[0m\n");
    printf("Model: \033[1;36m%s\033[0m | Endpoint: \033[1;36m%s\033[0m\n", h->agent->gateway->model, h->agent->gateway->endpoint);
    printf("Type \033[1;33m/help\033[0m for commands or \033[1;31mexit\033[0m to terminate.\n\n");

    while (1) {
        const char *m_disp = h->agent->gateway->model ? h->agent->gateway->model : "unknown";
        const char *slash = strrchr(m_disp, '/');
        if (slash) m_disp = slash + 1;
        char prompt[128];
        snprintf(prompt, sizeof(prompt), "\033[1;35mbelya (\033[1;36m%s\033[1;35m) [%zu msgs]>\033[0m ", m_disp, h->agent->msg_count);

        char *line = linenoise(prompt);
        if (!line) break;

        char input_buf[4096];
        strncpy(input_buf, line, sizeof(input_buf) - 1);
        input_buf[sizeof(input_buf) - 1] = '\0';
        input_buf[strcspn(input_buf, "\r\n")] = '\0';

        if (strlen(input_buf) > 0) {
            linenoiseHistoryAdd(input_buf);
            linenoiseHistorySave(".belya_history");
        }
        linenoiseFree(line);

        if (strcmp(input_buf, "exit") == 0) break;
        if (strlen(input_buf) == 0) continue;

        // Handle Slash Commands
        if (input_buf[0] == '/') {
            if (strcmp(input_buf, "/help") == 0) {
                print_help(h);
                continue;
            }
            if (strcmp(input_buf, "/status") == 0) {
                printf("\n\033[1;36m=== BelyaHarness System Status ===\033[0m\n");
                printf("Active Model:    \033[1;33m%s\033[0m\n", h->agent->gateway->model);
                printf("Endpoint:        \033[1;33m%s\033[0m\n", h->agent->gateway->endpoint);
                printf("Working Dir:     \033[1;33m%s\033[0m\n", h->cwd);
                printf("Message History: \033[1;33m%zu messages\033[0m\n", h->agent->msg_count);
                printf("Token Budget:    \033[1;33m%zu estimated tokens\033[0m\n", belya_agent_total_tokens(h->agent));
                printf("FTS5 Memory:     \033[1;33m%s\033[0m\n", h->agent->has_fts5 ? "Enabled (BM25)" : "Standard");
                printf("Prompt Caching:  \033[1;33m%s\033[0m\n", h->agent->gateway->prompt_caching ? "Enabled" : "Disabled");
                printf("Connected MCPs:  \033[1;33m%zu servers\033[0m\n\n", h->mcp_server_count);
                continue;
            }
            if (strncmp(input_buf, "/diff", 5) == 0) {
                const char *dpath = strlen(input_buf) > 5 ? input_buf + 5 : "";
                while (*dpath == ' ') dpath++;
                JsonValue *args = json_create_object();
                if (strlen(dpath) > 0) {
                    json_obj_add(args, "path", json_create_string(dpath));
                }
                char *raw = tool_git_diff(h->agent, args);
                json_free(args);
                char *colored = belya_ansi_diff_colorize(raw);
                printf("\n%s\n", colored ? colored : raw);
                if (colored) free(colored);
                free(raw);
                continue;
            }
            if (strcmp(input_buf, "/doctor") == 0) {
                printf("\n\033[1;36m=== Belya Environment Doctor & Diagnostics ===\033[0m\n");
                int gcc_ret = system("gcc --version >/dev/null 2>&1");
                printf("  [✓] C Compiler (gcc): %s\n", (gcc_ret == 0) ? "Available (C99 ready)" : "Missing");
                int git_ret = system("git rev-parse --is-inside-work-tree >/dev/null 2>&1");
                printf("  [✓] Git Worktree:     %s\n", (git_ret == 0) ? "Active git repository" : "Not a git repo");
                printf("  [✓] SQLite3 Library:  Version %s\n", sqlite3_libversion());
                printf("  [✓] Libcurl:          Version %s\n", curl_version());
                printf("  [✓] Memory Footprint: Peak RSS %.2f MB\n", belya_get_current_rss_mb());
                printf("  [✓] Terminal Mode:    %s\n", isatty(STDIN_FILENO) ? "Interactive TTY (ANSI VT100 enabled)" : "Non-interactive / Headless");
                printf("  [✓] Model Endpoint:   %s (%s)\n\n", h->agent->gateway->endpoint, h->agent->gateway->model);
                continue;
            }
            if (strcmp(input_buf, "/cost") == 0) {
                size_t p_tok = h->agent->total_prompt_tokens;
                size_t c_tok = h->agent->total_completion_tokens;
                size_t cached_tok = h->agent->total_cached_tokens;
                double prompt_cost = ((double)(p_tok > cached_tok ? p_tok - cached_tok : 0) / 1000000.0) * 3.0;
                double cached_cost = ((double)cached_tok / 1000000.0) * 0.30;
                double compl_cost = ((double)c_tok / 1000000.0) * 15.0;
                double total_cost = prompt_cost + cached_cost + compl_cost;
                printf("\n\033[1;36m=== Token Usage & Estimated Cost ===\033[0m\n");
                printf("  Model:             \033[1;33m%s\033[0m\n", h->agent->gateway->model);
                printf("  Prompt Tokens:     %zu (Cached: %zu)\n", p_tok, cached_tok);
                printf("  Completion Tokens: %zu\n", c_tok);
                printf("  Total Tokens:      %zu\n", p_tok + c_tok);
                printf("  Estimated Cost:    \033[1;32m$%.4f USD\033[0m\n\n", total_cost);
                continue;
            }
            if (strcmp(input_buf, "/tools") == 0) {
                list_tools(h);
                continue;
            }
            if (strcmp(input_buf, "/rules") == 0) {
                printf("\n\033[1;36m=== Repository Guidelines ===\033[0m\n");
                if (h->agent->msg_count > 0 && h->agent->messages[0].content) {
                    const char *found = strstr(h->agent->messages[0].content, "=== Repository Guidelines");
                    if (found) {
                        printf("%s\n", found);
                    } else {
                        printf("No .agentrules, AGENT.md, or CLAUDE.md found in repository root.\n\n");
                    }
                }
                continue;
            }
            if (strncmp(input_buf, "/timeline", 9) == 0) {
                int limit = 15;
                if (strlen(input_buf) > 9) {
                    limit = atoi(input_buf + 9);
                    if (limit <= 0) limit = 15;
                }
                char *tl = belya_agent_get_timeline(h->agent, limit);
                printf("\n%s\n", tl);
                free(tl);
                continue;
            }
            if (strcmp(input_buf, "/sessions") == 0) {
                char *sess_list = belya_agent_list_sessions(h->agent);
                printf("\n%s\n", sess_list);
                free(sess_list);
                continue;
            }
            if (strncmp(input_buf, "/save", 5) == 0) {
                const char *s_id = strlen(input_buf) > 5 ? input_buf + 5 : "default_session";
                while (*s_id == ' ') s_id++;
                if (strlen(s_id) == 0) s_id = "default_session";
                if (belya_agent_save_session(h->agent, s_id, s_id)) {
                    printf("\033[1;32mSession '%s' successfully checkpointed to SQLite database.\033[0m\n\n", s_id);
                } else {
                    printf("\033[1;31mFailed to save session.\033[0m\n\n");
                }
                continue;
            }
            if (strncmp(input_buf, "/resume", 7) == 0) {
                const char *s_id = strlen(input_buf) > 7 ? input_buf + 7 : "";
                while (*s_id == ' ') s_id++;
                if (strlen(s_id) > 0) {
                    if (belya_agent_load_session(h->agent, s_id)) {
                        printf("\033[1;32mSession '%s' successfully loaded (%zu messages restored).\033[0m\n\n", s_id, h->agent->msg_count);
                    } else {
                        printf("\033[1;31mSession '%s' not found or empty.\033[0m\n\n", s_id);
                    }
                } else {
                    printf("Usage: /resume <session_id>. Type /sessions to list available sessions.\n\n");
                }
                continue;
            }
            if (strncmp(input_buf, "/skills", 7) == 0) {
                const char *q = strlen(input_buf) > 7 ? input_buf + 7 : "";
                while (*q == ' ') q++;
                char *sk = belya_agent_search_skills(h->agent, q);
                printf("\n%s\n", sk ? sk : "No skills found.");
                if (sk) free(sk);
                continue;
            }
            if (strcmp(input_buf, "/checkpoints") == 0) {
                char *cps = belya_agent_list_checkpoints(h->agent);
                printf("\n%s\n", cps ? cps : "");
                if (cps) free(cps);
                continue;
            }
            if (strncmp(input_buf, "/checkpoint", 11) == 0) {
                const char *lbl = strlen(input_buf) > 11 ? input_buf + 11 : "";
                while (*lbl == ' ') lbl++;
                if (belya_agent_create_checkpoint(h->agent, strlen(lbl) > 0 ? lbl : "manual")) {
                    printf("\033[1;32mGit & state checkpoint created successfully.\033[0m\n\n");
                } else {
                    printf("\033[1;31mFailed to create checkpoint.\033[0m\n\n");
                }
                continue;
            }
            if (strncmp(input_buf, "/rollback", 9) == 0) {
                const char *cid = strlen(input_buf) > 9 ? input_buf + 9 : "";
                while (*cid == ' ') cid++;
                if (belya_agent_rollback_to_checkpoint(h->agent, strlen(cid) > 0 ? cid : NULL)) {
                    printf("\033[1;32mRollback completed successfully (%zu messages active in context).\033[0m\n\n", h->agent->msg_count);
                } else {
                    printf("\033[1;31mRollback failed (no matching checkpoint found).\033[0m\n\n");
                }
                continue;
            }
            if (strncmp(input_buf, "/export", 7) == 0) {
                char target_session[128] = {0};
                char target_file[256] = {0};
                const char *args = input_buf + 7;
                while (*args == ' ') args++;
                if (strlen(args) > 0) {
                    sscanf(args, "%127s %255s", target_session, target_file);
                }
                const char *sid = strlen(target_session) > 0 ? target_session : NULL;
                const char *outf = strlen(target_file) > 0 ? target_file : NULL;
                if (belya_agent_export_trajectory(h->agent, sid, outf)) {
                    printf("\033[1;32mTrajectory exported to %s (OpenAI fine-tune JSONL format).\033[0m\n\n", outf ? outf : "trajectory_*.jsonl");
                } else {
                    printf("\033[1;31mFailed to export trajectory.\033[0m\n\n");
                }
                continue;
            }
            if (strcmp(input_buf, "/cache") == 0) {
                size_t total_p = h->agent->total_prompt_tokens;
                size_t total_c = h->agent->total_cached_tokens;
                double hit_rate = (total_p > 0) ? ((double)total_c / (double)total_p * 100.0) : 0.0;
                printf("\n\033[1;36m=== Prompt Cache Economics ===\033[0m\n");
                printf("Prompt Tokens:      \033[1;33m%zu\033[0m\n", total_p);
                printf("Completion Tokens:  \033[1;33m%zu\033[0m\n", h->agent->total_completion_tokens);
                printf("Cached Tokens:      \033[1;32m%zu\033[0m\n", total_c);
                printf("Cache Hit Rate:     \033[1;32m%.2f%%\033[0m\n\n", hit_rate);
                continue;
            }
            if (strcmp(input_buf, "/reflect") == 0) {
                char *ref = belya_agent_reflect_and_distill(h->agent);
                printf("\n\033[1;36m=== Reflection & Skill Distillation ===\033[0m\n%s\n\n", ref);
                free(ref);
                continue;
            }
            if (strcmp(input_buf, "/clear") == 0 || strcmp(input_buf, "/reset") == 0 || strcmp(input_buf, "/new") == 0) {
                belya_agent_clear_history(h->agent);
                printf("\033[1;32mSession reset. Conversation context cleared (system prompt and persistent SQLite memory preserved).\033[0m\n\n");
                continue;
            }
            if (strncmp(input_buf, "/compact", 8) == 0) {
                size_t keep = 10;
                if (strlen(input_buf) > 8) {
                    keep = (size_t)atoi(input_buf + 8);
                    if (keep == 0) keep = 10;
                }
                belya_agent_compact_history(h->agent, keep);
                printf("\033[1;32mContext compacted to %zu messages.\033[0m\n\n", h->agent->msg_count);
                continue;
            }
            if (strncmp(input_buf, "/memory", 7) == 0) {
                const char *query = strlen(input_buf) > 7 ? input_buf + 7 : "";
                while (*query == ' ') query++;
                char *res = belya_agent_search_memory(h->agent, strlen(query) > 0 ? query : "");
                printf("\n\033[1;36m=== Memory Search: '%s' ===\033[0m\n%s\n", query, res ? res : "No results.");
                if (res) free(res);
                continue;
            }
            if (strncmp(input_buf, "/tasks", 6) == 0) {
                const char *filter = strlen(input_buf) > 6 ? input_buf + 6 : "";
                while (*filter == ' ') filter++;
                char *tl = belya_agent_task_list(h->agent, strlen(filter) > 0 ? filter : "all");
                printf("\n%s\n", tl ? tl : "No tasks found.");
                if (tl) free(tl);
                continue;
            }
            if (strcmp(input_buf, "/cron") == 0) {
                char *cl = belya_agent_cron_list(h->agent);
                printf("\n%s\n", cl ? cl : "No cron jobs registered.");
                if (cl) free(cl);
                continue;
            }
            if (strcmp(input_buf, "/dream") == 0) {
                char *dr = belya_agent_dream_sweep(h->agent);
                printf("\n%s\n", dr ? dr : "Dream consolidation complete.");
                if (dr) free(dr);
                continue;
            }
            if (strcmp(input_buf, "/plan") == 0) {
                if (h->plan_mode) {
                    belya_harness_exit_plan_mode(h);
                    printf("\n\033[1;32m[Plan Mode]: Deactivated. Mutating filesystem and shell actions restored.\033[0m\n\n");
                } else {
                    belya_harness_enter_plan_mode(h);
                    printf("\n\033[1;33m[Plan Mode]: Activated (Read-Only Sandbox). Write, edit, patch, and mutating shell commands are locked.\033[0m\n\n");
                }
                continue;
            }
            if (strcmp(input_buf, "/bg") == 0) {
                char *bg_list = belya_harness_task_list(h);
                printf("\n%s\n", bg_list ? bg_list : "No background tasks.");
                if (bg_list) free(bg_list);
                continue;
            }
            if (strncmp(input_buf, "/openrouter", 11) == 0) {
                const char *new_m = strlen(input_buf) > 11 ? input_buf + 11 : "";
                while (*new_m == ' ') new_m++;
                if (strlen(new_m) == 0) new_m = "anthropic/claude-3.7-sonnet";

                const char *or_key = getenv("OPENROUTER_API_KEY");
                if (!or_key || strlen(or_key) == 0) {
                    or_key = getenv("MODEL_API_KEY");
                }

                free(h->agent->gateway->endpoint);
                h->agent->gateway->endpoint = xstrdup("https://openrouter.ai/api/v1/chat/completions");
                free(h->agent->gateway->model);
                h->agent->gateway->model = xstrdup(new_m);
                if (or_key && strlen(or_key) > 0 && strcmp(or_key, "ollama") != 0 && strcmp(or_key, "none") != 0) {
                    free(h->agent->gateway->api_key);
                    h->agent->gateway->api_key = xstrdup(or_key);
                }
                printf("\033[1;32mSwitched to OpenRouter: Model: %s | Endpoint: %s\033[0m\n", h->agent->gateway->model, h->agent->gateway->endpoint);
                if (!h->agent->gateway->api_key || strcmp(h->agent->gateway->api_key, "none") == 0 || strcmp(h->agent->gateway->api_key, "ollama") == 0) {
                    printf("\033[1;33m[Notice] API key not set. Provide your OpenRouter key with: /key sk-or-v1-...\033[0m\n\n");
                } else {
                    printf("\n");
                }
                continue;
            }
            if (strncmp(input_buf, "/ollama", 7) == 0) {
                const char *new_m = strlen(input_buf) > 7 ? input_buf + 7 : "";
                while (*new_m == ' ') new_m++;
                if (strlen(new_m) == 0) new_m = "qwen2.5-coder:7b";

                free(h->agent->gateway->endpoint);
                h->agent->gateway->endpoint = xstrdup("http://localhost:11434/v1/chat/completions");
                free(h->agent->gateway->model);
                h->agent->gateway->model = xstrdup(new_m);
                free(h->agent->gateway->api_key);
                h->agent->gateway->api_key = xstrdup("ollama");
                printf("\033[1;32mSwitched to Local Ollama: Model: %s | Endpoint: %s\033[0m\n\n", h->agent->gateway->model, h->agent->gateway->endpoint);
                continue;
            }
            if (strncmp(input_buf, "/key", 4) == 0) {
                const char *new_k = strlen(input_buf) > 4 ? input_buf + 4 : "";
                while (*new_k == ' ') new_k++;
                if (strlen(new_k) > 0) {
                    free(h->agent->gateway->api_key);
                    h->agent->gateway->api_key = xstrdup(new_k);
                    setenv("OPENROUTER_API_KEY", new_k, 1);
                    setenv("MODEL_API_KEY", new_k, 1);
                    printf("\033[1;32mAPI Key updated successfully.\033[0m\n\n");
                } else {
                    printf("Current API Key: %s\nUsage: /key <api_key>\n\n", (h->agent->gateway->api_key && strcmp(h->agent->gateway->api_key, "none") != 0) ? "****" : "none");
                }
                continue;
            }
            if (strncmp(input_buf, "/model", 6) == 0) {
                const char *new_m = strlen(input_buf) > 6 ? input_buf + 6 : "";
                while (*new_m == ' ') new_m++;
                if (strlen(new_m) > 0) {
                    free(h->agent->gateway->model);
                    h->agent->gateway->model = xstrdup(new_m);
                    // If model name contains slash (e.g. anthropic/claude, openai/gpt-4o), auto-route to OpenRouter if currently on localhost
                    if (strchr(new_m, '/') != NULL && strstr(h->agent->gateway->endpoint, "localhost") != NULL) {
                        free(h->agent->gateway->endpoint);
                        h->agent->gateway->endpoint = xstrdup("https://openrouter.ai/api/v1/chat/completions");
                        const char *or_key = getenv("OPENROUTER_API_KEY");
                        if (or_key && strlen(or_key) > 0 && strcmp(or_key, "ollama") != 0) {
                            free(h->agent->gateway->api_key);
                            h->agent->gateway->api_key = xstrdup(or_key);
                        }
                        printf("\033[1;32mModel set to '%s' (Auto-routed endpoint to OpenRouter: %s)\033[0m\n\n", h->agent->gateway->model, h->agent->gateway->endpoint);
                    } else {
                        printf("\033[1;32mActive model switched to: %s\033[0m\n\n", h->agent->gateway->model);
                    }
                } else {
                    printf("Current model: %s. Usage: /model <model_name>\n\n", h->agent->gateway->model);
                }
                continue;
            }
            if (strncmp(input_buf, "/cwd", 4) == 0) {
                const char *new_d = strlen(input_buf) > 4 ? input_buf + 4 : "";
                while (*new_d == ' ') new_d++;
                if (strlen(new_d) > 0) {
                    if (chdir(new_d) == 0 && getcwd(h->cwd, sizeof(h->cwd))) {
                        printf("\033[1;32mWorking directory changed to: %s\033[0m\n\n", h->cwd);
                    } else {
                        printf("\033[1;31mFailed to change directory: %s\033[0m\n\n", strerror(errno));
                    }
                } else {
                    printf("Current working directory: %s\n\n", h->cwd);
                }
                continue;
            }
            if (strncmp(input_buf, "/mcp", 4) == 0) {
                const char *cmd = strlen(input_buf) > 4 ? input_buf + 4 : "";
                while (*cmd == ' ') cmd++;
                if (strlen(cmd) > 0) {
                    belya_harness_connect_mcp(h, cmd);
                } else {
                    printf("Usage: /mcp <server_command> (e.g. /mcp npx -y @modelcontextprotocol/server-filesystem .)\n\n");
                }
                continue;
            }
            if (strncmp(input_buf, "/agency", 7) == 0) {
                const char *task = strlen(input_buf) > 7 ? input_buf + 7 : "";
                while (*task == ' ') task++;
                if (strlen(task) == 0) {
                    char *manifests = belya_agent_list_manifests(h->agent);
                    printf("\n\033[1;36m=== Belya Agency Multi-Agent Architecture ===\033[0m\n");
                    printf("%s\n", manifests ? manifests : "No subagent manifests registered.\n");
                    if (manifests) free(manifests);
                    printf("Usage: /agency <task description>\n\n");
                } else {
                    char **pipeline = NULL;
                    size_t count = 0;
                    char *direct = NULL;
                    belya_agency_triage(h, task, &pipeline, &count, &direct);
                    if (direct) {
                        printf("\n\033[1;34m[Belya Triage]\033[0m %s\n\n", direct);
                        free(direct);
                    } else if (count > 0) {
                        char *report = belya_agency_execute_pipeline(h, task, (const char **)pipeline, count);
                        if (report) {
                            printf("\n%s\n", report);
                            free(report);
                        }
                        for (size_t i = 0; i < count; i++) free(pipeline[i]);
                        free(pipeline);
                    }
                }
                continue;
            }
            printf("\033[1;31mUnknown command: %s. Type /help for available commands.\033[0m\n\n", input_buf);
            continue;
        }

        belya_harness_execute_turn(h, input_buf);
    }

    BelyaHookResult stop_hook = belya_run_lifecycle_hook(h, BELYA_HOOK_STOP, NULL, NULL, NULL);
    belya_hook_result_free(&stop_hook);
}

char *belya_troubleshooting_resolve(const char *error_trace, const char *troubleshooting_path) {
    if (!error_trace || strlen(error_trace) == 0) return NULL;

    const char *path = troubleshooting_path ? troubleshooting_path : "TROUBLESHOOTING.md";
    FILE *f = fopen(path, "r");
    if (!f && (!troubleshooting_path || strcmp(troubleshooting_path, "TROUBLESHOOTING.md") == 0)) {
        path = "docs/TROUBLESHOOTING.md";
        f = fopen(path, "r");
    }
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 500000) {
        fclose(f);
        return NULL;
    }

    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t nread = fread(buf, 1, (size_t)sz, f);
    buf[nread] = '\0';
    fclose(f);

    char *p = buf;
    char *matched_remedy = NULL;

    while (*p) {
        char *line_start = p;
        char *next_nl = strchr(line_start, '\n');
        if (next_nl) {
            *next_nl = '\0';
            p = next_nl + 1;
        } else {
            p = line_start + strlen(line_start);
        }

        const char *pat_hdr = "## Pattern: ";
        if (strncmp(line_start, pat_hdr, strlen(pat_hdr)) == 0) {
            const char *pattern_str = line_start + strlen(pat_hdr);
            while (*pattern_str == ' ' || *pattern_str == '`') pattern_str++;
            char pat_clean[256];
            strncpy(pat_clean, pattern_str, sizeof(pat_clean) - 1);
            pat_clean[sizeof(pat_clean) - 1] = '\0';
            char *backtick = strchr(pat_clean, '`');
            if (backtick) *backtick = '\0';
            char *end = pat_clean + strlen(pat_clean) - 1;
            while (end >= pat_clean && isspace((unsigned char)*end)) {
                *end = '\0';
                end--;
            }

            if (strlen(pat_clean) > 0 && contains_case_insensitive(error_trace, pat_clean)) {
                const char *section_start = p;
                const char *section_end = strstr(p, "\n## ");
                size_t section_len = section_end ? (size_t)(section_end - section_start) : strlen(section_start);

                DynString ds = dyn_str_new();
                dyn_str_appendf(&ds, "Pattern: %s\n", pat_clean);
                dyn_str_append_len(&ds, section_start, section_len);
                matched_remedy = ds.data;
                break;
            }
        }
    }

    free(buf);
    return matched_remedy;
}

static uint64_t fnv1a_hash_str(const char *s, uint64_t hash) {
    if (!s) return hash;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        hash ^= (uint64_t)(*p++);
        hash *= 1099511628211ULL;
    }
    return hash;
}

static uint64_t compute_action_hash(const char *tool_name, const char *args_json) {
    uint64_t h = 14695981039346656037ULL;
    h = fnv1a_hash_str(tool_name, h);
    h = fnv1a_hash_str(":", h);
    h = fnv1a_hash_str(args_json, h);
    return h;
}

int belya_harness_detect_loop(BelyaHarness *h, int *out_period) {
    if (!h || h->loop_detector.count < 3) return 0;
    if (out_period) *out_period = 0;

    int total = h->loop_detector.count;
    int head = h->loop_detector.head;

    // Check periods k = 1, 2, 3
    for (int k = 1; k <= 3; k++) {
        // To verify 3 consecutive cycles of length k, we need at least 3 * k records
        if (total < 3 * k) continue;

        bool cycle_matched = true;
        for (int i = 0; i < k; i++) {
            int idx_curr = (head - 1 - i + HARNESS_ACTION_HISTORY_SIZE) % HARNESS_ACTION_HISTORY_SIZE;
            int idx_prev1 = (head - 1 - i - k + HARNESS_ACTION_HISTORY_SIZE) % HARNESS_ACTION_HISTORY_SIZE;
            int idx_prev2 = (head - 1 - i - 2 * k + HARNESS_ACTION_HISTORY_SIZE) % HARNESS_ACTION_HISTORY_SIZE;

            uint64_t h_curr = h->loop_detector.history[idx_curr].action_hash;
            uint64_t h_prev1 = h->loop_detector.history[idx_prev1].action_hash;
            uint64_t h_prev2 = h->loop_detector.history[idx_prev2].action_hash;

            if (h_curr != h_prev1 || h_curr != h_prev2) {
                cycle_matched = false;
                break;
            }
        }

        if (cycle_matched) {
            if (out_period) *out_period = k;
            return k;
        }
    }
    return 0;
}

void belya_harness_reset_turn_state(BelyaHarness *h) {
    if (!h) return;
    h->files_modified_in_turn = false;
    h->verification_performed_in_turn = false;
    h->verification_guard_tripped = false;
    h->consecutive_tool_failures = 0;
    memset(h->last_failed_tool, 0, sizeof(h->last_failed_tool));
    memset(h->last_failed_args, 0, sizeof(h->last_failed_args));
    h->loop_breaker_tripped = false;
    memset(&h->loop_detector, 0, sizeof(h->loop_detector));
}

bool belya_harness_record_tool_observation(BelyaHarness *h, const char *tool_name, const char *args_json, const char *observation, char **out_breaker_msg) {
    if (!h || !tool_name) return false;
    if (out_breaker_msg) *out_breaker_msg = NULL;

    // Track modifications and verification actions
    if (strcmp(tool_name, "write_file") == 0 || strcmp(tool_name, "edit_file") == 0 || strcmp(tool_name, "apply_patch") == 0) {
        h->files_modified_in_turn = true;
    }
    if (strcmp(tool_name, "bash") == 0 || strcmp(tool_name, "git_diff") == 0 || strcmp(tool_name, "git_status") == 0) {
        h->verification_performed_in_turn = true;
    }

    bool is_failure = false;
    if (observation) {
        if (contains_case_insensitive(observation, "error:") ||
            contains_case_insensitive(observation, "failed") ||
            contains_case_insensitive(observation, "exit status") ||
            contains_case_insensitive(observation, "command not found") ||
            contains_case_insensitive(observation, "AddressSanitizer") ||
            contains_case_insensitive(observation, "fatal:") ||
            strncmp(observation, "Error:", 6) == 0) {
            is_failure = true;
        }
    } else {
        is_failure = true;
    }

    // Record into Loop Detector ring buffer
    int head_idx = h->loop_detector.head;
    h->loop_detector.history[head_idx].action_hash = compute_action_hash(tool_name, args_json);
    snprintf(h->loop_detector.history[head_idx].tool_name, sizeof(h->loop_detector.history[head_idx].tool_name), "%s", tool_name ? tool_name : "");
    h->loop_detector.history[head_idx].return_code = is_failure ? 1 : 0;
    h->loop_detector.history[head_idx].timestamp = time(NULL);
    h->loop_detector.head = (head_idx + 1) % HARNESS_ACTION_HISTORY_SIZE;
    if (h->loop_detector.count < HARNESS_ACTION_HISTORY_SIZE) {
        h->loop_detector.count++;
    }

    // Check for In-Context Locking / Attractor Loop
    int detected_k = 0;
    belya_harness_detect_loop(h, &detected_k);

    if (is_failure) {
        char args_prefix[256];
        snprintf(args_prefix, sizeof(args_prefix), "%.255s", args_json ? args_json : "");
        if (strcmp(h->last_failed_tool, tool_name) == 0 && strcmp(h->last_failed_args, args_prefix) == 0) {
            h->consecutive_tool_failures++;
        } else {
            snprintf(h->last_failed_tool, sizeof(h->last_failed_tool), "%s", tool_name);
            snprintf(h->last_failed_args, sizeof(h->last_failed_args), "%s", args_prefix);
            h->consecutive_tool_failures = 1;
        }

        char *remedy = observation ? belya_troubleshooting_resolve(observation, "TROUBLESHOOTING.md") : NULL;

        if (h->consecutive_tool_failures >= 3 || detected_k > 0) {
            h->loop_breaker_tripped = true;
            if (detected_k > 0) {
                h->loop_detector.consecutive_cycle_matches++;
                h->loop_detector.detected_period = detected_k;
            }

            DynString cb = dyn_str_new();
            dyn_str_appendf(&cb, "%s\n\n[METACOGNITIVE CIRCUIT BREAKER]: You have attempted tool '%s' with identical/failing arguments 3 times consecutively. "
                                 "STOP repeating this action. Step back, re-evaluate assumptions, inspect error details, or switch to an alternate strategy.",
                            observation ? observation : "Tool returned empty observation", tool_name);
            if (detected_k > 1) {
                dyn_str_appendf(&cb, "\n\n[CIRCUIT INTERRUPT: DETERMINISTIC ATTRACTOR DETECTED (Period %d)]\n"
                                     "You have executed the exact same sequence of %d action(s) (%s) 3 consecutive times with identical parameters.\n"
                                     "Your current hypothesis is fundamentally flawed. You are strictly forbidden from repeating this action.\n"
                                     "Step back, re-read your initial constraints, and select an alternative hypothesis.",
                                detected_k, detected_k, tool_name);
            }
            if (remedy) {
                dyn_str_appendf(&cb, "\n\n💡 [TROUBLESHOOTING RESOLVER]: Known Failure Pattern Remedy:\n%s", remedy);
                free(remedy);
            }
            if (out_breaker_msg) *out_breaker_msg = cb.data;
            else dyn_str_free(&cb);
            h->consecutive_tool_failures = 0; // Trip and reset
            return true;
        } else if (remedy) {
            if (out_breaker_msg) {
                DynString enriched = dyn_str_new();
                dyn_str_append(&enriched, observation ? observation : "Tool execution failed.");
                dyn_str_appendf(&enriched, "\n\n💡 [TROUBLESHOOTING RESOLVER]: Known Failure Pattern Remedy:\n%s", remedy);
                *out_breaker_msg = enriched.data;
            }
            free(remedy);
        }
    } else {
        h->consecutive_tool_failures = 0;
        memset(h->last_failed_tool, 0, sizeof(h->last_failed_tool));
        memset(h->last_failed_args, 0, sizeof(h->last_failed_args));

        // Even on success, an infinite attractor loop (e.g. bash ls in a loop) must be broken!
        if (detected_k > 0) {
            h->loop_breaker_tripped = true;
            h->loop_detector.consecutive_cycle_matches++;
            h->loop_detector.detected_period = detected_k;

            DynString loop_cb = dyn_str_new();
            dyn_str_appendf(&loop_cb, "%s\n\n[CIRCUIT INTERRUPT: DETERMINISTIC ATTRACTOR DETECTED (Period %d)]\n"
                                      "You have executed the exact same sequence of %d action(s) (%s) 3 consecutive times with identical parameters.\n"
                                      "Your current hypothesis is fundamentally flawed. You are strictly forbidden from repeating this action.\n"
                                      "Step back, re-read your initial constraints, and select an alternative hypothesis.",
                            observation ? observation : "Tool returned empty observation",
                            detected_k, detected_k, tool_name);
            if (out_breaker_msg) {
                if (*out_breaker_msg) free(*out_breaker_msg);
                *out_breaker_msg = loop_cb.data;
            } else {
                dyn_str_free(&loop_cb);
            }
            return true;
        }
    }
    return false;
}

void belya_harness_execute_turn(BelyaHarness *h, const char *prompt) {
    if (!h || !prompt || strlen(prompt) == 0) return;

    belya_harness_reset_turn_state(h);

    // Ephemeral Git checkpoint before turn starts
    belya_agent_create_checkpoint(h->agent, "pre_turn_auto");

    belya_agent_add_message(h->agent, "user", prompt);

    // Turn Execution Cycle
    bool turn_running = true;
    int max_steps = 50;

    while (turn_running && max_steps-- > 0) {
        if (!h->agent->gateway->streaming) {
            printf("\033[0;33m[Thinking...]\033[0m\n");
        }
        ModelGatewayResponse resp = belya_agent_step(h->agent);

        if (!resp.has_tool_call) {
            // Deterministic Verification Guard
            if (h->files_modified_in_turn && !h->verification_performed_in_turn && !h->verification_guard_tripped) {
                h->verification_guard_tripped = true;
                model_gateway_response_free(&resp);
                printf("\033[1;33m[Harness Verification Guard]: Nudging agent to verify file modifications...\033[0m\n");
                belya_agent_add_message(h->agent, "user",
                    "[HARNESS VERIFICATION GUARD]: Files were modified during this turn, but no test, build, or verification command has been executed. "
                    "Run a verification step (e.g. compile, run tests, or inspect diff) to verify the changes before concluding.");
                continue;
            }

            if (!h->agent->gateway->streaming || (resp.content && (strncmp(resp.content, "API Error", 9) == 0 || strncmp(resp.content, "Network Error", 13) == 0 || strncmp(resp.content, "Empty response", 14) == 0 || strncmp(resp.content, "Error:", 6) == 0))) {
                printf("\n\033[1;34m[Belya]\033[0m\n%s\n\n", resp.content ? resp.content : "");
            } else {
                printf("\n\n");
            }
            turn_running = false;
        } else {
            for (size_t i = 0; i < resp.tool_call_count; i++) {
                ModelParsedToolCall *tc = &resp.tool_calls[i];
                printf("\n\033[1;33m[Tool Call Request]:\033[0m %s(%s)\n", tc->name, tc->arguments_json);

                BelyaRegisteredTool *matched = NULL;
                for (size_t t = 0; t < h->tool_count; t++) {
                    if (strcmp(h->tools[t].name, tc->name) == 0) {
                        matched = &h->tools[t];
                        break;
                    }
                }

                if (!matched || matched->security == PERM_DENY) {
                    printf("\033[1;31m[Denied]: Tool execution blocked.\033[0m\n");
                    belya_agent_add_tool_result(h->agent, tc->id, tc->name, "Error: Tool blocked by security policy.");
                    continue;
                }

                // 2026 Competitive Parity Phase 4: Plan Mode Gate
                if (h->plan_mode) {
                    if (strcmp(tc->name, "write_file") == 0 ||
                        strcmp(tc->name, "edit_file") == 0 ||
                        strcmp(tc->name, "apply_patch") == 0 ||
                        strcmp(tc->name, "define_tool") == 0) {
                        printf("\033[1;31m[Plan Mode Guard]: Mutating tool '%s' blocked in read-only plan mode.\033[0m\n", tc->name);
                        belya_agent_add_tool_result(h->agent, tc->id, tc->name, "Blocked: Plan Mode is active (read-only execution). Use exit_plan_mode to perform modifications.");
                        continue;
                    }
                }

                if (matched->security == PERM_ASK_USER) {
                    bool permitted = false;
                    if (h->permission_prompt_fn) {
                        permitted = h->permission_prompt_fn(h, tc->name, tc->arguments_json, h->permission_userdata);
                    } else {
                        permitted = harness_ask_permission(tc->name, tc->arguments_json);
                    }
                    if (!permitted) {
                        printf("\033[1;31m[Rejected]: Operation cancelled by operator.\033[0m\n");
                        belya_agent_add_tool_result(h->agent, tc->id, tc->name, "Error: User denied permission for this tool call.");
                        continue;
                    }
                }

                // Jev TypeSafe AI Risk Gating Hook
                if (h->agent && h->agent->jev) {
                    if (strcmp(tc->name, "bash") == 0 ||
                        strcmp(tc->name, "write_file") == 0 ||
                        strcmp(tc->name, "edit_file") == 0 ||
                        strcmp(tc->name, "apply_patch") == 0) {
                        JevRiskResult *risk = jev_check_tool_risk(h->agent->jev, "Autonomous execution", tc->name, tc->arguments_json);
                        if (risk) {
                            if (risk->action && (strcmp(risk->action, "block") == 0 || risk->risk > 0.85)) {
                                printf("\033[1;31m[Jev Risk Guard]: Operation blocked (risk=%.2f, action=%s).\033[0m\n", risk->risk, risk->action ? risk->action : "block");
                                belya_agent_add_tool_result(h->agent, tc->id, tc->name, "Error: Tool call blocked by Jev TypeSafe AI risk guard (high risk).");
                                jev_risk_result_free(risk);
                                continue;
                            } else if (risk->action && (strcmp(risk->action, "confirm") == 0 || risk->risk > 0.60)) {
                                printf("\033[1;33m[Jev Risk Guard]: Elevated risk detected (risk=%.2f, action=%s). Prompting confirmation.\033[0m\n", risk->risk, risk->action ? risk->action : "confirm");
                                bool permitted = false;
                                if (h->permission_prompt_fn) {
                                    permitted = h->permission_prompt_fn(h, tc->name, tc->arguments_json, h->permission_userdata);
                                } else {
                                    permitted = harness_ask_permission(tc->name, tc->arguments_json);
                                }
                                if (!permitted) {
                                    printf("\033[1;31m[Rejected]: Operation cancelled by operator after Jev risk alert.\033[0m\n");
                                    belya_agent_add_tool_result(h->agent, tc->id, tc->name, "Error: User denied permission after Jev risk alert.");
                                    jev_risk_result_free(risk);
                                    continue;
                                }
                            }
                            jev_risk_result_free(risk);
                        }
                    }
                }
                BelyaHookResult pre_hook = belya_run_lifecycle_hook(h, BELYA_HOOK_PRE_TOOL_USE, tc->name, tc->arguments_json, NULL);
                if (pre_hook.blocked) {
                    printf("\033[1;31m[Hook Policy Rejected]: Tool '%s' blocked by PreToolUse hook policy: %s\033[0m\n",
                           tc->name, pre_hook.output ? pre_hook.output : "Operation blocked.");
                    DynString rej = dyn_str_new();
                    dyn_str_appendf(&rej, "Error: PreToolUse hook blocked execution: %s",
                                    pre_hook.output ? pre_hook.output : "Operation blocked by security policy.");
                    belya_agent_add_tool_result(h->agent, tc->id, tc->name, rej.data);
                    dyn_str_free(&rej);
                    belya_hook_result_free(&pre_hook);
                    continue;
                }
                belya_hook_result_free(&pre_hook);

                JsonValue *args_parsed = json_parse(tc->arguments_json);
                char *observation = NULL;
                g_active_custom_script_path = matched->custom_script_path;
                if (matched->mcp_client) {
                    observation = mcp_client_call_tool(matched->mcp_client, tc->name, args_parsed);
                } else if (matched->callback) {
                    observation = matched->callback(h->agent, args_parsed);
                }
                g_active_custom_script_path = NULL;
                json_free(args_parsed);

                char *breaker_alert = NULL;
                bool tripped = belya_harness_record_tool_observation(h, tc->name, tc->arguments_json, observation, &breaker_alert);
                const char *final_obs = breaker_alert ? breaker_alert : (observation ? observation : "Success");

                BelyaHookResult post_hook = belya_run_lifecycle_hook(h, BELYA_HOOK_POST_TOOL_USE, tc->name, tc->arguments_json, final_obs);
                belya_hook_result_free(&post_hook);

                printf("\033[0;32m[Observation Output (%zu bytes)]\033[0m\n", final_obs ? strlen(final_obs) : 0);
                if (h->loop_breaker_tripped) {
                    printf("\033[1;35m[In-Context Locking Breaker Tripped (Period %d)]: Attractor loop broken!\033[0m\n", h->loop_detector.detected_period);
                } else if (tripped) {
                    printf("\033[1;35m[Metacognitive Circuit Breaker Tripped]: Loop interrupted!\033[0m\n");
                }
                belya_agent_add_tool_result(h->agent, tc->id, tc->name, final_obs);
                if (breaker_alert) free(breaker_alert);
                if (observation) free(observation);
            }
        }
        model_gateway_response_free(&resp);
    }
}

void belya_harness_free(BelyaHarness *h) {
    if (!h) return;
    for (size_t i = 0; i < BELYA_MAX_BACKGROUND_TASKS; i++) {
        BelyaBackgroundTask *t = &h->task_table.tasks[i];
        if (t->state == BELYA_TASK_STATE_RUNNING) {
            kill(-t->pgid, SIGTERM);
            usleep(10000);
            kill(-t->pgid, SIGKILL);
            waitpid(t->pid, NULL, WNOHANG);
            if (t->master_fd > STDERR_FILENO) {
                close(t->master_fd);
            }
            t->master_fd = -1;
            t->state = BELYA_TASK_STATE_KILLED;
        } else if (t->master_fd > STDERR_FILENO) {
            close(t->master_fd);
            t->master_fd = -1;
        }
        if (t->ring_buffer) {
            free(t->ring_buffer);
            t->ring_buffer = NULL;
        }
    }
    for (size_t i = 0; i < h->tool_count; i++) {
        if (h->tools[i].name) free(h->tools[i].name);
        if (h->tools[i].custom_script_path) free(h->tools[i].custom_script_path);
    }
    for (size_t s = 0; s < h->mcp_server_count; s++) {
        mcp_client_close(h->mcp_servers[s]);
    }
    belya_agent_free(h->agent);
    if (g_harness == h) g_harness = NULL;
    free(h);
}
