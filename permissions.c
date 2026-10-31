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
    "date", "sort", "uniq", "cut", "tr", "nl", "diff", "make", "gcc", "cc",
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

/* ------------------------------------------------------------- classify */

static bool is_read_tool(const char *t) {
    return strcmp(t, "read_file") == 0 || strcmp(t, "list_dir") == 0 ||
           strcmp(t, "search_files") == 0 || strcmp(t, "git_status") == 0 ||
           strcmp(t, "git_diff") == 0 || strcmp(t, "fetch_url") == 0;
}

static bool is_write_tool(const char *t) {
    return strcmp(t, "write_file") == 0 || strcmp(t, "edit_file") == 0 ||
           strcmp(t, "apply_patch") == 0 || strcmp(t, "create_file") == 0;
}

static bool is_inert_tool(const char *t) {
    return strcmp(t, "todo_write") == 0 || strcmp(t, "todo_read") == 0 ||
           strcmp(t, "memory_save") == 0 || strcmp(t, "memory_recall") == 0 ||
           strcmp(t, "send_telegram") == 0 || strcmp(t, "exit_plan_mode") == 0 ||
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

    if (strcmp(tool_name, "define_tool") == 0) return PERM_TIER_ASK;

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
