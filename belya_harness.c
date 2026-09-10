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
#include <curl/curl.h>

static BelyaHarness *g_harness = NULL;
const char *g_active_custom_script_path = NULL;

static void safe_pipe_write(int fd, const char *data, size_t len) {
    while (len > 0) {
        ssize_t w = write(fd, data, len);
        if (w <= 0) break;
        data += w;
        len -= (size_t)w;
    }
}

static char *preflight_syntax_check(const char *path) {
    if (!path) return NULL;
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

    char *wl_copy = strdup(whitelist);
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
    if (!cmd) return strdup("Error: Missing command argument.");

    // Tester Restricted Execution Guard: allow only test and build commands
    if (g_harness && g_harness->bash_restricted) {
        const char *t = cmd;
        while (*t == ' ' || *t == '\t') t++;
        bool allowed = false;
        const char *allowed_cmds[] = {
            "make", "./belya_test", "gcc", "clang", "ctest", "git status", "git diff", "cat ", "ls", "pwd", "echo", NULL
        };
        for (int i = 0; allowed_cmds[i]; i++) {
            if (strncmp(t, allowed_cmds[i], strlen(allowed_cmds[i])) == 0 ||
                strstr(t, "make test") != NULL ||
                strstr(t, "./belya_test") != NULL) {
                allowed = true;
                break;
            }
        }
        if (!allowed) {
            DynString err = dyn_str_new();
            dyn_str_appendf(&err, "Error: Tester role bash execution is restricted to build/test validation targets (e.g. make test, make, ./belya_test). Unauthorized command rejected: '%s'", cmd);
            return err.data;
        }
    }

    // Handle cd built-in directly to maintain working directory persistence
    if (strncmp(cmd, "cd ", 3) == 0 || strcmp(cmd, "cd") == 0) {
        const char *target = strlen(cmd) > 3 ? cmd + 3 : getenv("HOME");
        while (*target == ' ') target++;
        if (!target || strlen(target) == 0) target = getenv("HOME");
        if (!target) target = ".";

        char clean_path[4096];
        size_t tlen = strlen(target);
        if (tlen >= 2 && ((target[0] == '\"' && target[tlen-1] == '\"') || (target[0] == '\'' && target[tlen-1] == '\''))) {
            size_t copy_len = (tlen - 2 < sizeof(clean_path) - 1) ? (tlen - 2) : (sizeof(clean_path) - 1);
            memcpy(clean_path, target + 1, copy_len);
            clean_path[copy_len] = '\0';
        } else {
            snprintf(clean_path, sizeof(clean_path), "%s", target);
        }

        char resolved[8192];
        if (clean_path[0] == '/' || !g_harness) {
            snprintf(resolved, sizeof(resolved), "%s", clean_path);
        } else {
            snprintf(resolved, sizeof(resolved), "%s/%s", g_harness->cwd, clean_path);
        }

        if (chdir(resolved) == 0) {
            if (g_harness && getcwd(g_harness->cwd, sizeof(g_harness->cwd))) {
                DynString res = dyn_str_new();
                dyn_str_appendf(&res, "Changed working directory to: %s", g_harness->cwd);
                return res.data;
            }
            return strdup("Directory changed successfully.");
        }
        DynString err_ds = dyn_str_new();
        dyn_str_appendf(&err_ds, "Error: Failed to change directory to '%s': %s", clean_path, strerror(errno));
        return err_ds.data;
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
        return strdup("Error: Pipe creation failed.");
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        dyn_str_free(&full_cmd_ds);
        return strdup("Error: Subprocess fork failed.");
    }

    if (pid == 0) {
        // Child process
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
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

bool is_path_jailed(const char *path, const char *workspace_root, bool is_write) {
    if (!path || path[0] == '\0') return false;

    // Reject paths with explicit directory traversal
    if (strstr(path, "..") != NULL) return false;

    // Determine and canonicalize workspace root
    char root_buf[4096];
    char canonical_root[4096];
    if (workspace_root && workspace_root[0] != '\0') {
        strncpy(root_buf, workspace_root, sizeof(root_buf) - 1);
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
            "/proc/",
            "/dev/null",
            "/etc/os-release",
            NULL
        };
        for (int i = 0; read_whitelist[i]; i++) {
            size_t wlen = strlen(read_whitelist[i]);
            if (strncmp(resolved, read_whitelist[i], wlen) == 0) {
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
        "/proc/",
        "/dev/null",
        "/etc/os-release",
        NULL
    };
    for (int i = 0; read_whitelist[i]; i++) {
        size_t wlen = strlen(read_whitelist[i]);
        if (strncmp(path, read_whitelist[i], wlen) == 0) {
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
    if (!path) return strdup("Error: Missing file path argument.");
    if (!is_path_jailed(path, NULL, false)) return strdup("Error: Path traversal denied.");

    double offset_num = json_obj_get_num(args, "offset", 0);
    double limit_num = json_obj_get_num(args, "limit", 0);

    FILE *f = fopen(path, "rb");
    if (!f) return strdup("Error: Target file not found or inaccessible.");

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

    if (sz > 200000) {
        fclose(f);
        return strdup("Error: File exceeds maximum safe context read size (200KB). Use offset and limit parameters.");
    }

    char *buf = malloc(sz + 1);
    if (!buf) {
        fclose(f);
        return strdup("Error: Memory allocation failed reading file.");
    }
    size_t read_bytes = fread(buf, 1, sz, f);
    buf[read_bytes] = '\0';
    fclose(f);
    return buf;
}

static char *tool_write_file(BelyaAgent *agent, const JsonValue *args) {
    (void)agent;
    const char *path = json_obj_get_str(args, "path");
    const char *content = json_obj_get_str(args, "content");
    if (!path || !content) return strdup("Error: Missing path or content argument.");
    if (!is_path_jailed(path, NULL, true)) return strdup("Error: Path traversal denied.");

    FILE *f = fopen(path, "wb");
    if (!f) return strdup("Error: Failed to open path for writing.");

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
    const char *path = json_obj_get_str(args, "path");
    const char *old_text = json_obj_get_str(args, "old_text");
    const char *new_text = json_obj_get_str(args, "new_text");

    if (!path || !old_text || !new_text) {
        return strdup("Error: Missing required parameters (path, old_text, new_text).");
    }
    if (!is_path_jailed(path, NULL, true)) return strdup("Error: Path traversal denied.");

    FILE *f = fopen(path, "rb");
    if (!f) return strdup("Error: Target file not found or inaccessible.");

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz > 500000) {
        fclose(f);
        return strdup("Error: File exceeds maximum safe edit size (500KB).");
    }

    char *content = malloc(sz + 1);
    if (!content) {
        fclose(f);
        return strdup("Error: Memory allocation failed.");
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

    size_t prefix_len = pos - content;
    size_t old_len = matched_span_len;
    size_t new_len = strlen(new_text);
    size_t suffix_len = read_bytes - (prefix_len + old_len);

    DynString updated = dyn_str_new();
    dyn_str_append_len(&updated, content, prefix_len);
    dyn_str_append_len(&updated, new_text, new_len);
    dyn_str_append_len(&updated, pos + old_len, suffix_len);
    FILE *out_f = fopen(path, "wb");
    if (!out_f) {
        dyn_str_free(&updated);
        free(content);
        return strdup("Error: Failed to open target file for writing.");
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
    const char *path = json_obj_get_str(args, "path");
    const char *patch = json_obj_get_str(args, "patch");

    if (!path || !patch) return strdup("Error: Missing path or patch argument.");
    if (!is_path_jailed(path, NULL, true)) return strdup("Error: Path traversal denied.");

    FILE *f = fopen(path, "rb");
    if (!f) return strdup("Error: Target file not found.");

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *orig = malloc(sz + 1);
    if (!orig) { fclose(f); return strdup("Error: Out of memory."); }
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
            if (!div) break;

            size_t search_len = div - p;
            char *search_str = malloc(search_len + 1);
            memcpy(search_str, p, search_len);
            search_str[search_len] = '\0';

            const char *rep_start = div + strlen(div_marker);
            const char *rep_end = strstr(rep_start, replace_marker);
            if (!rep_end) { free(search_str); break; }

            size_t rep_len = rep_end - rep_start;
            char *rep_str = malloc(rep_len + 1);
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
            return strdup("Error: Failed to open target file for writing.");
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
    return strdup("Error: Unsupported patch format. Use <<<<<<< SEARCH ... ======= ... >>>>>>> REPLACE blocks.");
}

static char *tool_list_dir(BelyaAgent *agent, const JsonValue *args) {
    (void)agent;
    const char *path = json_obj_get_str(args, "path");
    if (!path) path = g_harness ? g_harness->cwd : ".";

    DIR *d = opendir(path);
    if (!d) return strdup("Error: Unable to open directory path.");

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
    if (*match_count >= 50) return;
    DIR *d = opendir(dir_path);
    if (!d) return;

    struct dirent *dir;
    while ((dir = readdir(d)) != NULL) {
        if (*match_count >= 50) break;
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;
        if (dir->d_name[0] == '.' && strcmp(dir->d_name, ".env") != 0) continue;
        if (strcmp(dir->d_name, "node_modules") == 0 || strcmp(dir->d_name, "build") == 0) continue;

        char sub_path[4096];
        snprintf(sub_path, sizeof(sub_path), "%s/%s", dir_path, dir->d_name);

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

            char line[2048];
            size_t line_num = 1;
            while (fgets(line, sizeof(line), f)) {
                if (*match_count >= 50) break;
                bool matched = false;
                if (is_regex && preg) {
                    matched = (regexec(preg, line, 0, NULL, 0) == 0);
                } else {
                    matched = (strstr(line, pattern) != NULL);
                }

                if (matched) {
                    line[strcspn(line, "\r\n")] = '\0';
                    dyn_str_appendf(out, "%s:%zu: %s\n", sub_path, line_num, line);
                    (*match_count)++;
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
        return strdup("Error: Missing search pattern argument.");
    }
    if (!path || strlen(path) == 0) {
        path = g_harness ? g_harness->cwd : ".";
    }

    regex_t reg;
    regex_t *preg = NULL;
    if (use_regex) {
        if (regcomp(&reg, pattern, REG_EXTENDED | REG_NOSUB) != 0) {
            return strdup("Error: Invalid regular expression pattern.");
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
    } else if (match_count >= 50) {
        dyn_str_append(&out, "\n[Search capped at 50 matches]");
    }

    return out.data;
}

static char *tool_git_status(BelyaAgent *agent, const JsonValue *args) {
    (void)agent;
    const char *path = json_obj_get_str(args, "path");
    const char *dir = (path && strlen(path) > 0) ? path : (g_harness && strlen(g_harness->cwd) > 0 ? g_harness->cwd : ".");

    DynString cmd = dyn_str_new();
    dyn_str_appendf(&cmd, "cd \"%s\" 2>&1 && git status", dir);

    FILE *pipe = popen(cmd.data, "r");
    dyn_str_free(&cmd);
    if (!pipe) return strdup("Error: Failed to execute git status.");

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

    DynString cmd = dyn_str_new();
    if (g_harness && strlen(g_harness->cwd) > 0) {
        dyn_str_appendf(&cmd, "cd \"%s\" && git diff %s %s", g_harness->cwd, staged ? "--cached" : "", path ? path : "");
    } else {
        dyn_str_appendf(&cmd, "git diff %s %s", staged ? "--cached" : "", path ? path : "");
    }

    FILE *pipe = popen(cmd.data, "r");
    dyn_str_free(&cmd);
    if (!pipe) return strdup("Error: Failed to execute git diff.");

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

static char *tool_save_memory(BelyaAgent *agent, const JsonValue *args) {
    const char *topic = json_obj_get_str(args, "topic");
    const char *content = json_obj_get_str(args, "content");
    if (!topic || !content) return strdup("Error: Missing topic or content.");
    belya_agent_persist_memory(agent, topic, content);
    return strdup("Knowledge successfully stored in persistent agent SQLite database.");
}

static char *tool_recall_memory(BelyaAgent *agent, const JsonValue *args) {
    const char *query = json_obj_get_str(args, "query");
    if (!query) return strdup("Error: Missing query string.");
    return belya_agent_search_memory(agent, query);
}

static char *tool_spawn_subagent(BelyaAgent *agent, const JsonValue *args) {
    const char *task = json_obj_get_str(args, "task");
    const char *instructions = json_obj_get_str(args, "instructions");
    double max_turns_num = json_obj_get_num(args, "max_turns", 5);

    if (!task) return strdup("Error: Missing subagent task argument.");

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
    sub_gw->streaming = false; // Run silent to avoid stdout collision

    BelyaAgent *sub_agent = belya_agent_init(sub_gw, ":memory:", sys.data);
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
        return strdup("Error: Invalid arguments for subagent dispatch.");
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
    sub_gw->streaming = false; // Run silent to avoid stdout collision

    BelyaAgent *sub_agent = belya_agent_init(sub_gw, ":memory:", sys.data);
    dyn_str_free(&sys);

    BelyaHarness *saved_harness = g_harness;
    BelyaHarness *sub_harness = belya_harness_init_bounded(sub_agent, whitelist, role_name);
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

    // 6. Git Rollback Guard Execution
    bool rolled_back = false;
    if (has_write && git_repo_available) {
        if (subagent_failed) {
            char reset_cmd[256];
            snprintf(reset_cmd, sizeof(reset_cmd), "git reset --hard %s 2>/dev/null && git clean -fd 2>/dev/null", snapshot_sha);
            int ret = system(reset_cmd);
            (void)ret;
            rolled_back = true;

            char log_msg[256];
            snprintf(log_msg, sizeof(log_msg), "Subagent '%s' failed; rolled back to snapshot %.8s", role_name, snapshot_sha);
            belya_agent_log_timeline(parent_harness->agent, "subagent_rollback", log_msg);
        } else {
            char log_msg[256];
            snprintf(log_msg, sizeof(log_msg), "Subagent '%s' completed successfully; changes preserved", role_name);
            belya_agent_log_timeline(parent_harness->agent, "subagent_commit", log_msg);
        }
    }

    // 7. Format output envelope
    DynString envelope = dyn_str_new();
    dyn_str_appendf(&envelope, "=== Belya Agency Subagent Execution Envelope ===\n");
    dyn_str_appendf(&envelope, "Role: %s | Permitted Tools: [%s]\n", role_name, whitelist);
    dyn_str_appendf(&envelope, "Status: %s\n", subagent_failed ? "FAILED (Rolled Back)" : "SUCCESS");
    dyn_str_appendf(&envelope, "Tools Executed: %zu\n", tool_executions);
    if (rolled_back) {
        dyn_str_appendf(&envelope, "Git Rollback Guard: ACTIVATED (Reverted working tree to snapshot %.8s)\n", snapshot_sha);
    }
    if (tool_log.len > 0) {
        dyn_str_append(&envelope, "\nExecution Trace:\n");
        dyn_str_append(&envelope, tool_log.data);
    }
    dyn_str_appendf(&envelope, "\nFinal Output:\n%s", final_ans.len > 0 ? final_ans.data : "(Subagent completed without output text)");

    dyn_str_free(&tool_log);
    dyn_str_free(&final_ans);
    belya_harness_free(sub_harness);
    g_harness = saved_harness;
    model_gateway_free(sub_gw);
    if (manifest) belya_agent_manifest_free(manifest);

    return envelope.data;
}

static char *tool_dispatch_agent(BelyaAgent *agent, const JsonValue *args) {
    (void)agent;
    const char *role = json_obj_get_str(args, "role");
    const char *task = json_obj_get_str(args, "task");
    const char *context = json_obj_get_str(args, "context");

    if (!role || !task) return strdup("Error: Missing role or task argument for dispatch_agent.");
    if (!g_harness) return strdup("Error: Harness execution environment not available.");

    return belya_agency_dispatch_subagent(g_harness, role, task, context);
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
                *out_direct_reply = strdup("Hello! I am Belya Agency, an autonomous multi-agent engineering harness. I coordinate Architect, Builder, Reviewer, and Tester subagents to solve complex engineering tasks.");
            }
            return true;
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
        pipeline[0] = strdup("reviewer");
    } else if (is_test && !str_contains_ci(p, "fix") && !str_contains_ci(p, "implement")) {
        count = 1;
        pipeline = malloc(sizeof(char *) * count);
        pipeline[0] = strdup("tester");
    } else if (is_research && !str_contains_ci(p, "fix") && !str_contains_ci(p, "implement")) {
        count = 1;
        pipeline = malloc(sizeof(char *) * count);
        pipeline[0] = strdup("architect");
    } else {
        // Standard Full Engineering Pipeline: Architect -> Builder -> Tester
        count = 3;
        pipeline = malloc(sizeof(char *) * count);
        pipeline[0] = strdup("architect");
        pipeline[1] = strdup("builder");
        pipeline[2] = strdup("tester");
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

static char *tool_fetch_url(BelyaAgent *agent, const JsonValue *args) {
    (void)agent;
    const char *url = json_obj_get_str(args, "url");
    if (!url || strlen(url) == 0) return strdup("Error: Missing url argument.");

    const char *method = json_obj_get_str(args, "method");
    const char *req_body = json_obj_get_str(args, "body");
    if (!req_body) req_body = json_obj_get_str(args, "data");

    CURL *curl = curl_easy_init();
    if (!curl) return strdup("Error: Failed to initialize HTTP client.");

    DynString body = dyn_str_new();
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "User-Agent: BelyaAgent/4.0 (Autonomous C99 Engine)");

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
    }

    return body.data;
}

static char *tool_save_skill(BelyaAgent *agent, const JsonValue *args) {
    const char *name = json_obj_get_str(args, "name");
    const char *trigger = json_obj_get_str(args, "trigger");
    const char *desc = json_obj_get_str(args, "description");
    const char *instructions = json_obj_get_str(args, "instructions");

    if (!name || !instructions) return strdup("Error: Missing required arguments (name, instructions).");
    if (belya_agent_save_skill(agent, name, trigger, desc, instructions)) {
        DynString res = dyn_str_new();
        dyn_str_appendf(&res, "Skill '%s' successfully saved to procedural memory with trigger '%s'.", name, trigger ? trigger : name);
        return res.data;
    }
    return strdup("Error: Failed to save skill to database.");
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
    if (!script_path) return strdup("Error: Custom script path not found.");

    char *args_str = args ? json_serialize(args) : strdup("{}");

    int in_pipe[2];
    int out_pipe[2];
    if (pipe(in_pipe) == -1 || pipe(out_pipe) == -1) {
        free(args_str);
        return strdup("Error: Failed to create pipes for custom tool execution.");
    }

    pid_t pid = fork();
    if (pid == -1) {
        free(args_str);
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        return strdup("Error: Failed to fork process for custom tool.");
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
                    char *v_str = (v->type == JSON_STRING) ? strdup(v->u.string) : json_serialize(v);
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
            if (out.len > 100000) break;
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
    if (!g_harness) return strdup("Error: Harness runtime not active.");

    const char *name = json_obj_get_str(args, "name");
    const char *desc = json_obj_get_str(args, "description");
    JsonValue *params = json_obj_get(args, "parameters");
    const char *script_body = json_obj_get_str(args, "script_body");

    if (!name || !desc || !script_body) {
        return strdup("Error: Missing required fields (name, description, script_body).");
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
    return strdup("Error: Failed to register custom tool (max tool limit reached or disk write error).");
}

bool belya_harness_define_custom_tool(BelyaHarness *h, const char *name, const char *desc, JsonValue *params, const char *script_body) {
    if (!h || !name || !script_body || h->tool_count >= 64) {
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
    h->tools[h->tool_count].name = strdup(name);
    h->tools[h->tool_count].security = PERM_ALLOW;
    h->tools[h->tool_count].callback = tool_custom_script_runner;
    h->tools[h->tool_count].custom_script_path = strdup(script_path);
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
                                char *sch_str = t_schema ? json_serialize(t_schema) : strdup("{\"type\":\"object\"}");
                                JsonValue *sch_clone = json_parse(sch_str);
                                free(sch_str);

                                h->tools[h->tool_count].name = strdup(t_name);
                                h->tools[h->tool_count].security = PERM_ALLOW;
                                h->tools[h->tool_count].callback = tool_custom_script_runner;
                                h->tools[h->tool_count].custom_script_path = strdup(s_path);
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

BelyaHarness *belya_harness_init_bounded(BelyaAgent *agent, const char *tools_whitelist, const char *role_name) {
    BelyaHarness *h = calloc(1, sizeof(BelyaHarness));
    h->agent = agent;
    if (role_name) {
        strncpy(h->active_role, role_name, sizeof(h->active_role) - 1);
        if (strcmp(role_name, "tester") == 0) {
            h->bash_restricted = true;
        }
    }
    if (tools_whitelist) {
        strncpy(h->tools_whitelist, tools_whitelist, sizeof(h->tools_whitelist) - 1);
        if (strstr(tools_whitelist, "restricted_bash") != NULL) {
            h->bash_restricted = true;
        }
    }
    if (getcwd(h->cwd, sizeof(h->cwd)) == NULL) {
        strncpy(h->cwd, ".", sizeof(h->cwd));
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
    REGISTER_IF_PERMITTED("bash", "Execute a non-interactive shell command. Do NOT use vim/nano/top/less/sudo/man (they will hang). Timeout: 30s. Use cat/grep/sed/awk for file ops.", 
        build_string_param_schema("command", "The bash command string to execute"), PERM_ALLOW, tool_bash);

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
    h->tools[h->tool_count].name = strdup(name);
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
                printf("  + \033[1;33m%s\033[0m: %s\n", t_name, t_desc ? t_desc : "");
                h->tools[h->tool_count].name = strdup(t_name);
                h->tools[h->tool_count].security = PERM_ALLOW;
                h->tools[h->tool_count].callback = NULL;
                h->tools[h->tool_count].custom_script_path = NULL;
                h->tools[h->tool_count].mcp_client = client;
                h->tool_count++;

                char *sch_str = t_schema ? json_serialize(t_schema) : strdup("{\"type\":\"object\"}");
                JsonValue *sch_clone = json_parse(sch_str);
                free(sch_str);
                belya_agent_register_schema(h->agent, t_name, t_desc ? t_desc : "", sch_clone);
            }
        }
        json_free(tools);
    }
    return true;
}

static bool harness_ask_permission(const char *name, const char *args) {
    printf("\n\033[1;33m[SECURITY GATE: Permission Required]\033[0m\n");
    printf("Tool:      \033[1;37m%s\033[0m\n", name);
    printf("Arguments: \033[0;36m%s\033[0m\n", args);
    printf("Execute action? (\033[1;32my\033[0m/\033[1;31mN\033[0m): ");
    fflush(stdout);

    char line[32];
    if (fgets(line, sizeof(line), stdin)) {
        return (line[0] == 'y' || line[0] == 'Y');
    }
    return false;
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
    printf("  /openrouter [m]  Switch to OpenRouter (e.g. anthropic/claude-3.7-sonnet, deepseek/deepseek-r1)\n");
    printf("  /ollama [model]  Switch to local Ollama (e.g. qwen2.5-coder:7b, deepseek-r1:7b-m2)\n");
    printf("  /key <api_key>   Dynamically set or update API key (for OpenRouter / cloud providers)\n");
    printf("  /cwd [path]      View or change working directory\n");
    printf("  /mcp <command>   Connect to an external MCP stdio server\n");
    printf("  /agency [task]   Execute task via Belya Agency multi-agent pipeline (or list subagents)\n");
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
            "/compact", "/memory", "/model", "/openrouter", "/ollama", "/key", "/cwd", "/mcp", "/agency", NULL
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
            if (strncmp(input_buf, "/openrouter", 11) == 0) {
                const char *new_m = strlen(input_buf) > 11 ? input_buf + 11 : "";
                while (*new_m == ' ') new_m++;
                if (strlen(new_m) == 0) new_m = "anthropic/claude-3.7-sonnet";

                const char *or_key = getenv("OPENROUTER_API_KEY");
                if (!or_key || strlen(or_key) == 0) {
                    or_key = getenv("MODEL_API_KEY");
                }

                free(h->agent->gateway->endpoint);
                h->agent->gateway->endpoint = strdup("https://openrouter.ai/api/v1/chat/completions");
                free(h->agent->gateway->model);
                h->agent->gateway->model = strdup(new_m);
                if (or_key && strlen(or_key) > 0 && strcmp(or_key, "ollama") != 0 && strcmp(or_key, "none") != 0) {
                    free(h->agent->gateway->api_key);
                    h->agent->gateway->api_key = strdup(or_key);
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
                h->agent->gateway->endpoint = strdup("http://localhost:11434/v1/chat/completions");
                free(h->agent->gateway->model);
                h->agent->gateway->model = strdup(new_m);
                free(h->agent->gateway->api_key);
                h->agent->gateway->api_key = strdup("ollama");
                printf("\033[1;32mSwitched to Local Ollama: Model: %s | Endpoint: %s\033[0m\n\n", h->agent->gateway->model, h->agent->gateway->endpoint);
                continue;
            }
            if (strncmp(input_buf, "/key", 4) == 0) {
                const char *new_k = strlen(input_buf) > 4 ? input_buf + 4 : "";
                while (*new_k == ' ') new_k++;
                if (strlen(new_k) > 0) {
                    free(h->agent->gateway->api_key);
                    h->agent->gateway->api_key = strdup(new_k);
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
                    h->agent->gateway->model = strdup(new_m);
                    // If model name contains slash (e.g. anthropic/claude, openai/gpt-4o), auto-route to OpenRouter if currently on localhost
                    if (strchr(new_m, '/') != NULL && strstr(h->agent->gateway->endpoint, "localhost") != NULL) {
                        free(h->agent->gateway->endpoint);
                        h->agent->gateway->endpoint = strdup("https://openrouter.ai/api/v1/chat/completions");
                        const char *or_key = getenv("OPENROUTER_API_KEY");
                        if (or_key && strlen(or_key) > 0 && strcmp(or_key, "ollama") != 0) {
                            free(h->agent->gateway->api_key);
                            h->agent->gateway->api_key = strdup(or_key);
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

void belya_harness_reset_turn_state(BelyaHarness *h) {
    if (!h) return;
    h->files_modified_in_turn = false;
    h->verification_performed_in_turn = false;
    h->verification_guard_tripped = false;
    h->consecutive_tool_failures = 0;
    memset(h->last_failed_tool, 0, sizeof(h->last_failed_tool));
    memset(h->last_failed_args, 0, sizeof(h->last_failed_args));
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

        if (h->consecutive_tool_failures >= 3) {
            DynString cb = dyn_str_new();
            dyn_str_appendf(&cb, "%s\n\n[METACOGNITIVE CIRCUIT BREAKER]: You have attempted tool '%s' with identical/failing arguments 3 times consecutively. "
                                 "STOP repeating this action. Step back, re-evaluate assumptions, inspect error details, or switch to an alternate strategy.",
                            observation ? observation : "Tool returned empty observation", tool_name);
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

                printf("\033[0;32m[Observation Output (%zu bytes)]\033[0m\n", final_obs ? strlen(final_obs) : 0);
                if (tripped) {
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
