#include "permissions.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define PERM_CMD_MAX 4096

/* ---------------------------------------------------------------- helpers */

static bool str_contains_ci(const char *hay, const char *needle) {
    if (!hay || !needle || !*needle) return false;
    size_t n = strlen(needle);
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < n && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == n) return true;
    }
    return false;
}

bool permission_arg_string(const char *args_json, const char *key, char *buf, size_t buflen) {
    if (!args_json || !key || !buf || buflen == 0) return false;
    buf[0] = '\0';

    char pattern[128];
    int pn = snprintf(pattern, sizeof pattern, "\"%s\"", key);
    if (pn <= 0 || (size_t)pn >= sizeof pattern) return false;

    const char *p = strstr(args_json, pattern);
    if (!p) return false;
    p += (size_t)pn;
    while (*p && *p != ':') p++;
    if (*p != ':') return false;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != '"') return false;
    p++;

    size_t i = 0;
    while (*p && *p != '"' && i + 1 < buflen) {
        if (*p == '\\' && p[1]) {
            p++;
            switch (*p) {
                case 'n': buf[i++] = '\n'; break;
                case 't': buf[i++] = '\t'; break;
                default: buf[i++] = *p; break;
            }
            p++;
            continue;
        }
        buf[i++] = *p++;
    }
    buf[i] = '\0';
    return true;
}

/* ------------------------------------------------------------ allowlists */

static const char *const k_safe_argv0[] = {
    "ls", "pwd", "cat", "head", "tail", "wc", "grep", "rg", "find", "file",
    "stat", "du", "df", "which", "uname", "echo", "printf", "true", "false",
    "date", "sort", "uniq", "cut", "tr", "nl", "diff", "gcc", "cc",
    "clang", "nm", "objdump", "readelf", "sha256sum", "md5sum", "basename",
    "dirname", "realpath", "tree", "jq", "git", NULL
};

static const char *const k_safe_git_sub[] = {
    "status", "diff", "log", "show", "branch", "rev-parse", "ls-files",
    "blame", "describe", "remote", "tag", "stash", NULL
};

/* find is only safe for looking, never for acting. */
static const char *const k_unsafe_find[] = {
    "-delete", "-exec", "-execdir", "-ok", "-okdir", "-fprint", "-fprintf", NULL
};

static bool in_list(const char *const *list, const char *s) {
    if (!s) return false;
    for (size_t i = 0; list[i]; i++) {
        if (strcmp(list[i], s) == 0) return true;
    }
    return false;
}

/* --------------------------------------------------------------- commands */

bool permission_is_safe_command(const char *cmd) {
    if (!cmd) return false;
    while (*cmd && isspace((unsigned char)*cmd)) cmd++;
    if (!*cmd || strlen(cmd) >= PERM_CMD_MAX) return false;

    /* Any chaining, substitution or redirection means we cannot reason about
       the side effects, so it is not "safe". */
    for (const char *p = cmd; *p; p++) {
        if (strchr(";|&$`(){}<>\n\r", *p)) return false;
    }

    char buf[PERM_CMD_MAX];
    snprintf(buf, sizeof buf, "%s", cmd);

    char *save = NULL;
    char *argv0 = strtok_r(buf, " \t", &save);
    if (!argv0) return false;
    if (strchr(argv0, '/')) return false; /* /tmp/ls must not inherit ls trust */
    if (!in_list(k_safe_argv0, argv0)) return false;

    if (strcmp(argv0, "find") == 0) {
        char *tok = strtok_r(NULL, " \t", &save);
        while (tok) {
            if (in_list(k_unsafe_find, tok)) return false;
            tok = strtok_r(NULL, " \t", &save);
        }
        return true;
    }

    if (strcmp(argv0, "git") == 0) {
        char *sub = strtok_r(NULL, " \t", &save);
        if (!sub || !in_list(k_safe_git_sub, sub)) return false;
        return true;
    }

    return true;
}

/* ------------------------------------------------------------------ paths */

bool permission_is_protected_path(const char *path) {
    if (!path || !*path) return false;

    static const char *const k_protected_fragments[] = {
        "/.git/hooks", "/.git/config", "/.ssh/", "/.gnupg/", "/etc/", "/boot/",
        "/sys/", "/proc/", "/dev/", "/.bashrc", "/.bash_profile", "/.zshrc",
        "/.profile", "/.gitconfig", "/.docker/config.json", NULL
    };

    for (size_t i = 0; k_protected_fragments[i]; i++) {
        if (str_contains_ci(path, k_protected_fragments[i])) return true;
    }
    if (strncmp(path, "~/.ssh", 6) == 0) return true;
    if (strncmp(path, "~/.bash", 7) == 0) return true;
    if (strncmp(path, ".git/hooks", 10) == 0) return true;
    if (strncmp(path, ".git/config", 11) == 0) return true;
    return false;
}

/* --------------------------------------------------------------- patterns */

static bool command_is_destructive(const char *cmd) {
    if (!cmd) return false;

    /* rm against a root-ish operand is always refused. A recursive delete of a
       relative path is not "deny" — it is "ask", which falls out because rm is
       not in the safe allowlist. */
    if (strncmp(cmd, "rm ", 3) == 0 || strncmp(cmd, "rm\t", 3) == 0) {
        static const char *const k_roots[] = {
            "/", "/*", "~", "~/", "$HOME", "/etc", "/usr", "/bin", "/sbin",
            "/var", "/opt", "/boot", "/home", NULL
        };
        char buf[PERM_CMD_MAX];
        snprintf(buf, sizeof buf, "%s", cmd);
        char *save = NULL;
        char *tok = strtok_r(buf, " \t", &save);
        while ((tok = strtok_r(NULL, " \t", &save))) {
            if (in_list(k_roots, tok)) return true;
        }
        return false;
    }

    if (str_contains_ci(cmd, "mkfs") || str_contains_ci(cmd, "dd if=") ||
        str_contains_ci(cmd, "> /dev/sd") || str_contains_ci(cmd, ":(){") ||
        str_contains_ci(cmd, "chmod -r 777 /") || str_contains_ci(cmd, "chown -r")) {
        return true;
    }
    if ((str_contains_ci(cmd, "curl") || str_contains_ci(cmd, "wget")) &&
        (str_contains_ci(cmd, "| sh") || str_contains_ci(cmd, "|sh") ||
         str_contains_ci(cmd, "| bash") || str_contains_ci(cmd, "|bash"))) {
        return true;
    }
    if (str_contains_ci(cmd, "git push --force") || str_contains_ci(cmd, "git push -f")) {
        return true;
    }
    return false;
}


/* --------------------------------------------------- hard denial vectors */
/*
 * The first classifier matched argv0 by exact string, so "/bin/rm -rf /" was
 * only ASK - and ASK is auto-approved under turbo. These checks run on every
 * token, strip any directory prefix, and understand the wrappers that are
 * normally used to slip past a naive match (env/nice/time/sudo/xargs/-exec).
 * A verdict here is DENY and turbo cannot lift it.
 */
static const char *tok_basename(const char *tok) {
    const char *slash = strrchr(tok, '/');
    return slash ? slash + 1 : tok;
}

static bool is_rootish_operand(const char *tok) {
    static const char *const k_roots[] = {
        "/", "/*", "~", "~/", "$HOME", "${HOME}", "/etc", "/usr", "/bin", "/sbin",
        "/var", "/opt", "/boot", "/home", "/root", "/lib", "/lib64", "/dev", NULL
    };
    if (in_list(k_roots, tok)) return true;
    /* --no-preserve-root, and any path under a root-ish prefix, counts */
    if (strncmp(tok, "--no-preserve-root", 18) == 0) return true;
    for (int i = 0; k_roots[i]; i++) {
        size_t n = strlen(k_roots[i]);
        if (strncmp(tok, k_roots[i], n) == 0 && tok[n] == '/') return true;
    }
    return false;
}

static bool token_is_destructive_binary(const char *base) {
    static const char *const k_bins[] = {
        "rm", "rmdir", "unlink", "shred", "wipefs", "mkfs", "mkfs.ext4", "mkfs.xfs",
        "dd", "truncate", "mkswap", NULL
    };
    for (int i = 0; k_bins[i]; i++)
        if (strcmp(base, k_bins[i]) == 0) return true;
    if (strncmp(base, "mkfs.", 5) == 0) return true;
    return false;
}

static bool command_has_denied_vector(const char *cmd) {
    if (!cmd) return false;

    char buf[PERM_CMD_MAX];
    snprintf(buf, sizeof buf, "%s", cmd);

    bool destructive_seen = false;
    bool root_operand = false;

    /* separators become spaces so every word is visible as its own token */
    for (char *p = buf; *p; p++)
        if (*p == ';' || *p == '|' || *p == '&' || *p == '\n' || *p == '(' || *p == ')')
            *p = ' ';

    char *save = NULL;
    for (char *tok = strtok_r(buf, " \t", &save); tok; tok = strtok_r(NULL, " \t", &save)) {
        const char *base = tok_basename(tok);
        if (token_is_destructive_binary(base)) destructive_seen = true;
        if (is_rootish_operand(tok)) root_operand = true;
        /* "--output=/anything" on git (or any tool) writes outside the workspace */
        if (strncmp(tok, "--output=/", 10) == 0) return true;
        /* make -f/-C pointed at the root filesystem */
        if ((strcmp(base, "make") == 0 || strcmp(base, "gmake") == 0)) {
            /* handled by the following tokens */
        }
    }

    if (destructive_seen && root_operand) return true;

    if (str_contains_ci(cmd, "-exec rm") || str_contains_ci(cmd, "xargs rm") ||
        str_contains_ci(cmd, "xargs -0 rm") || str_contains_ci(cmd, "> /etc/") ||
        str_contains_ci(cmd, ">> /etc/") || str_contains_ci(cmd, "> /usr/") ||
        str_contains_ci(cmd, "make -f /dev/") || str_contains_ci(cmd, "make -f /etc/") ||
        str_contains_ci(cmd, "make -C / ") || str_contains_ci(cmd, "-o /etc/"))
        return true;

    return false;
}

/* ------------------------------------------------------------- classify */

static bool is_read_tool(const char *t) {
    /* fetch_url is deliberately absent: it is read-only only for GET, and that
     * depends on the call's arguments, not on the tool's name. */
    return strcmp(t, "read_file") == 0 || strcmp(t, "list_dir") == 0 ||
           strcmp(t, "search_files") == 0 || strcmp(t, "git_status") == 0 ||
           strcmp(t, "git_diff") == 0;
}

static bool is_write_tool(const char *t) {
    return strcmp(t, "write_file") == 0 || strcmp(t, "edit_file") == 0 ||
           strcmp(t, "apply_patch") == 0 || strcmp(t, "create_file") == 0;
}

static bool is_inert_tool(const char *t) {
    return strcmp(t, "todo_write") == 0 || strcmp(t, "todo_read") == 0 ||
           strcmp(t, "memory_save") == 0 || strcmp(t, "memory_recall") == 0 ||
           strcmp(t, "send_telegram") == 0 ||
           strcmp(t, "enter_plan_mode") == 0 || strcmp(t, "task") == 0 ||
           strcmp(t, "web_search") == 0;
}

PermTier permission_classify(const char *tool_name, const char *args_json) {
    if (!tool_name) return PERM_TIER_ASK;

    if (strcmp(tool_name, "bash") == 0) {
        char cmd[PERM_CMD_MAX];
        if (!permission_arg_string(args_json, "command", cmd, sizeof cmd)) {
            return PERM_TIER_ASK;
        }
        if (command_is_destructive(cmd)) return PERM_TIER_DENY;
        if (command_has_denied_vector(cmd)) return PERM_TIER_DENY;
        if (permission_is_safe_command(cmd)) return PERM_TIER_ALLOW;
        return PERM_TIER_ASK;
    }

    if (is_read_tool(tool_name)) {
        char path[2048];
        if (permission_arg_string(args_json, "path", path, sizeof path) && *path) {
            if (permission_is_protected_path(path)) return PERM_TIER_ASK;
        }
        return PERM_TIER_ALLOW;
    }

    if (is_write_tool(tool_name)) {
        char path[2048];
        bool have = permission_arg_string(args_json, "path", path, sizeof path) && *path;
        if (!have) return PERM_TIER_ASK; /* e.g. apply_patch: patch text, no path */
        if (permission_is_protected_path(path)) return PERM_TIER_DENY;
        if (path[0] == '/') return PERM_TIER_ASK; /* outside-workspace write */
        return PERM_TIER_ALLOW;
    }

    if (strcmp(tool_name, "fetch_url") == 0) {
        /* GET is a read. Anything that can carry a body can ship workspace
           contents to an arbitrary host, so it asks. */
        if (args_json &&
            (str_contains_ci(args_json, "\"POST\"") || str_contains_ci(args_json, "\"PUT\"") ||
             str_contains_ci(args_json, "\"PATCH\"") || str_contains_ci(args_json, "\"DELETE\"")))
            return PERM_TIER_ASK;
        return PERM_TIER_ALLOW;
    }

    if (strcmp(tool_name, "define_tool") == 0) return PERM_TIER_ASK;

    /* Force-push rewrites shared history: no approve button, turbo or not. */
    if (strcmp(tool_name, "git") == 0 && args_json &&
        (str_contains_ci(args_json, "push --force") || str_contains_ci(args_json, "push -f")))
        return PERM_TIER_DENY;

    /* Leaving plan mode is how a sandboxed turn escapes its own sandbox. */
    if (strcmp(tool_name, "exit_plan_mode") == 0) return PERM_TIER_ASK;

    if (is_inert_tool(tool_name)) return PERM_TIER_ALLOW;

    return PERM_TIER_ASK;
}

const char *permission_tier_name(PermTier tier) {
    switch (tier) {
        case PERM_TIER_ALLOW: return "allow";
        case PERM_TIER_ASK: return "ask";
        case PERM_TIER_DENY: return "deny";
    }
    return "unknown";
}

PermTier permission_apply_turbo(PermTier tier, int turbo) {
    if (turbo && tier == PERM_TIER_ASK) return PERM_TIER_ALLOW;
    return tier;
}
