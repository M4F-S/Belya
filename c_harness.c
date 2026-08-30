#include "c_harness.h"
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <dirent.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <fnmatch.h>

static CHarness *g_harness = NULL;

// Built-in Native Tools

static char *tool_bash(CAgent *agent, const JsonValue *args) {
    (void)agent;
    const char *cmd = json_obj_get_str(args, "command");
    if (!cmd) return strdup("Error: Missing command argument.");

    // Handle cd built-in directly to maintain working directory persistence
    if (strncmp(cmd, "cd ", 3) == 0 || strcmp(cmd, "cd") == 0) {
        const char *target = strlen(cmd) > 3 ? cmd + 3 : getenv("HOME");
        while (*target == ' ') target++;
        if (!target || strlen(target) == 0) target = getenv("HOME");
        if (!target) target = ".";

        char clean_path[4096];
        size_t tlen = strlen(target);
        if (tlen >= 2 && ((target[0] == '\"' && target[tlen-1] == '\"') || (target[0] == '\'' && target[tlen-1] == '\''))) {
            strncpy(clean_path, target + 1, tlen - 2);
            clean_path[tlen - 2] = '\0';
        } else {
            strncpy(clean_path, target, sizeof(clean_path) - 1);
            clean_path[sizeof(clean_path) - 1] = '\0';
        }

        char resolved[4096];
        if (clean_path[0] == '/' || !g_harness) {
            strncpy(resolved, clean_path, sizeof(resolved) - 1);
            resolved[sizeof(resolved) - 1] = '\0';
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

    while (1) {
        char buf[512];
        ssize_t bytes = read(pipefd[0], buf, sizeof(buf) - 1);
        if (bytes > 0) {
            buf[bytes] = '\0';
            dyn_str_append(&out, buf);
            if (out.len > 100000) {
                dyn_str_append(&out, "\n[Output Truncated by C Harness (100KB buffer limit)]");
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
        if (elapsed > 15.0) {
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
            timed_out = true;
            break;
        }

        usleep(10000); // 10ms sleep
    }
    close(pipefd[0]);

    if (timed_out) {
        dyn_str_append(&out, "\n[Process killed: Execution exceeded 15s timeout]");
    }

    if (out.len == 0) dyn_str_append(&out, "Command executed with no output.");
    return out.data;
}

static char *tool_read_file(CAgent *agent, const JsonValue *args) {
    (void)agent;
    const char *path = json_obj_get_str(args, "path");
    if (!path) return strdup("Error: Missing file path argument.");

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

static char *tool_write_file(CAgent *agent, const JsonValue *args) {
    (void)agent;
    const char *path = json_obj_get_str(args, "path");
    const char *content = json_obj_get_str(args, "content");
    if (!path || !content) return strdup("Error: Missing path or content argument.");

    FILE *f = fopen(path, "wb");
    if (!f) return strdup("Error: Failed to open path for writing.");

    size_t clen = strlen(content);
    fwrite(content, 1, clen, f);
    fclose(f);
    return strdup("File successfully written to disk.");
}

static char *tool_edit_file(CAgent *agent, const JsonValue *args) {
    (void)agent;
    const char *path = json_obj_get_str(args, "path");
    const char *old_text = json_obj_get_str(args, "old_text");
    const char *new_text = json_obj_get_str(args, "new_text");

    if (!path || !old_text || !new_text) {
        return strdup("Error: Missing required parameters (path, old_text, new_text).");
    }

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
    if (!pos) {
        free(content);
        return strdup("Error: old_text was not found in the target file.");
    }

    size_t prefix_len = pos - content;
    size_t old_len = strlen(old_text);
    size_t new_len = strlen(new_text);
    size_t suffix_len = read_bytes - (prefix_len + old_len);

    DynString updated = dyn_str_new();
    dyn_str_append_len(&updated, content, prefix_len);
    dyn_str_append_len(&updated, new_text, new_len);
    dyn_str_append_len(&updated, pos + old_len, suffix_len);
    free(content);

    FILE *out_f = fopen(path, "wb");
    if (!out_f) {
        dyn_str_free(&updated);
        return strdup("Error: Failed to open target file for writing.");
    }

    fwrite(updated.data, 1, updated.len, out_f);
    fclose(out_f);
    dyn_str_free(&updated);

    DynString msg = dyn_str_new();
    dyn_str_appendf(&msg, "File '%s' successfully edited (replaced %zu bytes with %zu bytes).", path, old_len, new_len);
    return msg.data;
}

static char *tool_apply_patch(CAgent *agent, const JsonValue *args) {
    (void)agent;
    const char *path = json_obj_get_str(args, "path");
    const char *patch = json_obj_get_str(args, "patch");

    if (!path || !patch) return strdup("Error: Missing path or patch argument.");

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
                free(search_str);
                free(rep_str);
                if (cur_doc != orig) free(cur_doc);
                free(orig);
                return strdup("Error: Search block mismatch during patch application.");
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
        return strdup("Patch successfully applied to file.");
    }

    free(orig);
    return strdup("Error: Unsupported patch format. Use <<<<<<< SEARCH ... ======= ... >>>>>>> REPLACE blocks.");
}

static char *tool_list_dir(CAgent *agent, const JsonValue *args) {
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

static void search_files_recursive(const char *dir_path, const char *pattern, const char *glob_pat, DynString *out, int *match_count) {
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
            search_files_recursive(sub_path, pattern, glob_pat, out, match_count);
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
                if (strstr(line, pattern)) {
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

static char *tool_search_files(CAgent *agent, const JsonValue *args) {
    (void)agent;
    const char *pattern = json_obj_get_str(args, "pattern");
    const char *path = json_obj_get_str(args, "path");
    const char *glob_pat = json_obj_get_str(args, "file_glob");

    if (!pattern || strlen(pattern) == 0) {
        return strdup("Error: Missing search pattern argument.");
    }
    if (!path || strlen(path) == 0) {
        path = g_harness ? g_harness->cwd : ".";
    }

    DynString out = dyn_str_new();
    int match_count = 0;
    search_files_recursive(path, pattern, glob_pat, &out, &match_count);

    if (match_count == 0) {
        dyn_str_appendf(&out, "No matches found for '%s' in '%s'.", pattern, path);
    } else if (match_count >= 50) {
        dyn_str_append(&out, "\n[Search capped at 50 matches]");
    }

    return out.data;
}

static char *tool_git_status(CAgent *agent, const JsonValue *args) {
    (void)agent; (void)args;
    DynString cmd = dyn_str_new();
    if (g_harness && strlen(g_harness->cwd) > 0) {
        dyn_str_appendf(&cmd, "cd \"%s\" && git status -s", g_harness->cwd);
    } else {
        dyn_str_append(&cmd, "git status -s");
    }

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

static char *tool_git_diff(CAgent *agent, const JsonValue *args) {
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

static char *tool_save_memory(CAgent *agent, const JsonValue *args) {
    const char *topic = json_obj_get_str(args, "topic");
    const char *content = json_obj_get_str(args, "content");
    if (!topic || !content) return strdup("Error: Missing topic or content.");
    c_agent_persist_memory(agent, topic, content);
    return strdup("Knowledge successfully stored in persistent agent SQLite database.");
}

static char *tool_recall_memory(CAgent *agent, const JsonValue *args) {
    const char *query = json_obj_get_str(args, "query");
    if (!query) return strdup("Error: Missing query string.");
    return c_agent_search_memory(agent, query);
}

static char *tool_spawn_subagent(CAgent *agent, const JsonValue *args) {
    const char *task = json_obj_get_str(args, "task");
    const char *instructions = json_obj_get_str(args, "instructions");
    double max_turns_num = json_obj_get_num(args, "max_turns", 5);

    if (!task) return strdup("Error: Missing subagent task argument.");

    DynString sys = dyn_str_new();
    dyn_str_append(&sys, "You are a specialized subagent worker. Execute the assigned task and provide a concise, direct answer.");
    if (instructions && strlen(instructions) > 0) {
        dyn_str_append(&sys, "\nTask instructions:\n");
        dyn_str_append(&sys, instructions);
    }

    ModelGateway *sub_gw = model_gateway_init(agent->gateway->endpoint, agent->gateway->api_key, agent->gateway->model);
    sub_gw->streaming = false; // Run silent to avoid stdout collision

    CAgent *sub_agent = c_agent_init(sub_gw, ":memory:", sys.data);
    dyn_str_free(&sys);

    CHarness *sub_harness = c_harness_init(sub_agent);
    c_agent_add_message(sub_agent, "user", task);

    int turns = (int)max_turns_num;
    if (turns <= 0 || turns > 10) turns = 5;

    DynString result_summary = dyn_str_new();
    bool running = true;

    while (running && turns-- > 0) {
        ModelGatewayResponse resp = c_agent_step(sub_agent);
        if (!resp.has_tool_call) {
            if (resp.content) dyn_str_append(&result_summary, resp.content);
            running = false;
        } else {
            for (size_t i = 0; i < resp.tool_call_count; i++) {
                ModelParsedToolCall *tc = &resp.tool_calls[i];
                CHarnessRegisteredTool *matched = NULL;
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
                    c_agent_add_tool_result(sub_agent, tc->id, tc->name, obs);
                    if (obs) free(obs);
                } else {
                    c_agent_add_tool_result(sub_agent, tc->id, tc->name, "Tool not available in subagent sandbox.");
                }
            }
        }
        model_gateway_response_free(&resp);
    }

    c_harness_free(sub_harness);
    model_gateway_free(sub_gw);

    if (result_summary.len == 0) {
        dyn_str_append(&result_summary, "Subagent completed execution.");
    }
    return result_summary.data;
}

// Dynamic Custom Tool Execution Runner
static char *tool_custom_script_runner(CAgent *agent, const JsonValue *args) {
    (void)agent;
    // Find matching custom tool path from harness
    const char *script_path = NULL;
    if (g_harness) {
        for (size_t i = 0; i < g_harness->tool_count; i++) {
            if (g_harness->tools[i].custom_script_path) {
                // If callback matches or was invoked
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

    if (pid == 0) {
        // Child
        close(in_pipe[1]);
        dup2(in_pipe[0], STDIN_FILENO);
        close(in_pipe[0]);

        close(out_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(out_pipe[1], STDERR_FILENO);
        close(out_pipe[1]);

        execl(script_path, script_path, (char *)NULL);
        // Fallback to /bin/sh if not directly executable binary
        execl("/bin/sh", "sh", script_path, (char *)NULL);
        _exit(127);
    }

    // Parent
    close(in_pipe[0]);
    close(out_pipe[1]);

    write(in_pipe[1], args_str, strlen(args_str));
    write(in_pipe[1], "\n", 1);
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
        usleep(10000);
    }
    close(out_pipe[0]);

    if (timed_out) dyn_str_append(&out, "\n[Custom tool execution timed out after 15s]");
    if (out.len == 0) dyn_str_append(&out, "Custom tool executed successfully with no output.");
    return out.data;
}

// Tool define_tool: Enables CAgent to dynamically add new tools to itself
static char *tool_define_tool(CAgent *agent, const JsonValue *args) {
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

    bool ok = c_harness_define_custom_tool(g_harness, name, desc, params_clone, script_body);
    if (ok) {
        DynString res = dyn_str_new();
        dyn_str_appendf(&res, "Tool '%s' successfully defined, persisted, and registered into active tool catalog.", name);
        return res.data;
    }
    return strdup("Error: Failed to register custom tool (max tool limit reached or disk write error).");
}

bool c_harness_define_custom_tool(CHarness *h, const char *name, const char *desc, JsonValue *params, const char *script_body) {
    if (!h || !name || !script_body || h->tool_count >= 64) {
        if (params) json_free(params);
        return false;
    }

    mkdir(".charness", 0755);
    mkdir(".charness/tools", 0755);

    char script_path[512];
    snprintf(script_path, sizeof(script_path), ".charness/tools/%s.sh", name);

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
    snprintf(meta_path, sizeof(meta_path), ".charness/tools/%s.json", name);
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
    h->tools[h->tool_count].security = PERM_ASK_USER;
    h->tools[h->tool_count].callback = tool_custom_script_runner;
    h->tools[h->tool_count].custom_script_path = strdup(script_path);
    h->tools[h->tool_count].mcp_client = NULL;
    h->tool_count++;

    c_agent_register_schema(h->agent, name, desc ? desc : "", params);
    return true;
}

void c_harness_load_custom_tools(CHarness *h) {
    if (!h) return;
    DIR *d = opendir(".charness/tools");
    if (!d) return;

    struct dirent *dir;
    while ((dir = readdir(d)) != NULL) {
        const char *ext = strrchr(dir->d_name, '.');
        if (ext && strcmp(ext, ".json") == 0) {
            char fpath[512];
            snprintf(fpath, sizeof(fpath), ".charness/tools/%s", dir->d_name);
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
                                h->tools[h->tool_count].security = PERM_ASK_USER;
                                h->tools[h->tool_count].callback = tool_custom_script_runner;
                                h->tools[h->tool_count].custom_script_path = strdup(s_path);
                                h->tools[h->tool_count].mcp_client = NULL;
                                h->tool_count++;

                                c_agent_register_schema(h->agent, t_name, t_desc ? t_desc : "", sch_clone);
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

CHarness *c_harness_init(CAgent *agent) {
    CHarness *h = calloc(1, sizeof(CHarness));
    h->agent = agent;
    if (getcwd(h->cwd, sizeof(h->cwd)) == NULL) {
        strncpy(h->cwd, ".", sizeof(h->cwd));
    }
    g_harness = h;

    // 1. bash
    c_harness_register_tool(h, "bash", "Execute shell commands in the host system", 
        build_string_param_schema("command", "The bash command string to execute"), PERM_ASK_USER, tool_bash);

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
    c_harness_register_tool(h, "read_file", "Read contents from a file with optional line ranges", read_params, PERM_ALLOW, tool_read_file);

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
    c_harness_register_tool(h, "write_file", "Write contents to a file path", write_params, PERM_ASK_USER, tool_write_file);

    // 4. edit_file
    JsonValue *edit_params = json_create_object();
    json_obj_add(edit_params, "type", json_create_string("object"));
    JsonValue *e_props = json_create_object();
    JsonValue *e_path = json_create_object();
    json_obj_add(e_path, "type", json_create_string("string"));
    json_obj_add(e_props, "path", e_path);
    JsonValue *e_old = json_create_object();
    json_obj_add(e_old, "type", json_create_string("string"));
    json_obj_add(e_old, "description", json_create_string("Existing text substring to replace"));
    json_obj_add(e_props, "old_text", e_old);
    JsonValue *e_new = json_create_object();
    json_obj_add(e_new, "type", json_create_string("string"));
    json_obj_add(e_new, "description", json_create_string("New text to replace old_text with"));
    json_obj_add(e_props, "new_text", e_new);
    json_obj_add(edit_params, "properties", e_props);
    JsonValue *e_req = json_create_array();
    json_arr_add(e_req, json_create_string("path"));
    json_arr_add(e_req, json_create_string("old_text"));
    json_arr_add(e_req, json_create_string("new_text"));
    json_obj_add(edit_params, "required", e_req);
    c_harness_register_tool(h, "edit_file", "Perform exact search-and-replace edit on a file", edit_params, PERM_ASK_USER, tool_edit_file);

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
    c_harness_register_tool(h, "apply_patch", "Apply multi-hunk structured replacement patch to a file", patch_params, PERM_ASK_USER, tool_apply_patch);

    // 6. list_dir
    c_harness_register_tool(h, "list_dir", "List files and directories in path",
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
    json_obj_add(s_params, "properties", s_props);
    JsonValue *s_req = json_create_array();
    json_arr_add(s_req, json_create_string("pattern"));
    json_obj_add(s_params, "required", s_req);
    c_harness_register_tool(h, "search_files", "Search for text patterns recursively across files", s_params, PERM_ALLOW, tool_search_files);

    // 8. git_status
    JsonValue *gs_params = json_create_object();
    json_obj_add(gs_params, "type", json_create_string("object"));
    c_harness_register_tool(h, "git_status", "Check Git repository status in current workspace", gs_params, PERM_ALLOW, tool_git_status);

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
    c_harness_register_tool(h, "git_diff", "View Git working copy or staged diffs", gd_params, PERM_ALLOW, tool_git_diff);

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
    c_harness_register_tool(h, "save_memory", "Save a verified skill or trajectory to SQLite memory", mem_params, PERM_ALLOW, tool_save_memory);

    // 11. recall_memory
    c_harness_register_tool(h, "recall_memory", "Search SQLite memory for past solutions and skills",
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
    c_harness_register_tool(h, "spawn_subagent", "Spawn an autonomous subagent worker in an isolated sandbox context", sub_params, PERM_ALLOW, tool_spawn_subagent);

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
    c_harness_register_tool(h, "define_tool", "Dynamically create, persist, and register a new executable tool for self-evolution", def_params, PERM_ASK_USER, tool_define_tool);

    // Load any previously defined custom tools
    c_harness_load_custom_tools(h);

    return h;
}

void c_harness_register_tool(CHarness *h, const char *name, const char *desc, JsonValue *params, SecurityLevel sec, ToolCallback fn) {
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

    c_agent_register_schema(h->agent, name, desc, params);
}

bool c_harness_connect_mcp(CHarness *h, const char *server_cmd) {
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
                h->tools[h->tool_count].security = PERM_ASK_USER;
                h->tools[h->tool_count].callback = NULL;
                h->tools[h->tool_count].custom_script_path = NULL;
                h->tools[h->tool_count].mcp_client = client;
                h->tool_count++;

                char *sch_str = t_schema ? json_serialize(t_schema) : strdup("{\"type\":\"object\"}");
                JsonValue *sch_clone = json_parse(sch_str);
                free(sch_str);
                c_agent_register_schema(h->agent, t_name, t_desc ? t_desc : "", sch_clone);
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

static void print_help(CHarness *h) {
    printf("\n\033[1;36m=== CHarness & CAgent Help ===\033[0m\n");
    printf("Current Model:   \033[1;33m%s\033[0m\n", h->agent->gateway->model);
    printf("Current CWD:     \033[1;33m%s\033[0m\n", h->cwd);
    printf("Context Size:    \033[1;33m%zu messages | %zu estimated tokens\033[0m\n", h->agent->msg_count, c_agent_total_tokens(h->agent));
    printf("FTS5 Memory:     \033[1;33m%s\033[0m\n", h->agent->has_fts5 ? "Enabled (BM25)" : "Standard");
    printf("Prompt Caching:  \033[1;33m%s\033[0m\n", h->agent->gateway->prompt_caching ? "Enabled" : "Disabled");
    printf("Streaming SSE:   \033[1;33m%s\033[0m\n", h->agent->gateway->streaming ? "Enabled (Real-time)" : "Disabled");
    printf("Connected MCPs:  \033[1;33m%zu servers\033[0m\n\n", h->mcp_server_count);
    printf("\033[1;32mAvailable Slash Commands:\033[0m\n");
    printf("  /help            Show this help reference\n");
    printf("  /status          View active system status and token utilization\n");
    printf("  /tools           List all registered tools and permissions\n");
    printf("  /rules           View active repository guidelines (.agentrules)\n");
    printf("  /sessions        List all checkpointed conversation sessions\n");
    printf("  /save [id]       Checkpoint conversation tree to SQLite\n");
    printf("  /resume <id>     Restore conversation session by ID\n");
    printf("  /reflect         Distill recent trajectory into reusable SQLite FTS5 skill\n");
    printf("  /clear           Reset conversation history (preserves system prompt)\n");
    printf("  /compact [N]     Prune older messages, keeping N recent (default: 10)\n");
    printf("  /memory [query]  Search SQLite persistent memory directly\n");
    printf("  /model <name>    Dynamically change the active AI model\n");
    printf("  /cwd [path]      View or change working directory\n");
    printf("  /mcp <command>   Connect to an external MCP stdio server\n");
    printf("  exit             Terminate the harness REPL\n\n");
}

static void list_tools(CHarness *h) {
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
            "/help", "/status", "/tools", "/rules", "/sessions",
            "/save", "/resume", "/reflect", "/clear", "/compact",
            "/memory", "/model", "/cwd", "/mcp", NULL
        };
        for (int i = 0; commands[i]; i++) {
            if (strncmp(buf, commands[i], strlen(buf)) == 0) {
                linenoiseAddCompletion(lc, commands[i]);
            }
        }
    }
}

void c_harness_repl(CHarness *h) {
    linenoiseHistorySetMaxLen(200);
    linenoiseHistoryLoad(".charness_history");
    linenoiseSetCompletionCallback(harness_completion_hook);

    printf("\033[1;32m=== CHarness & CAgent Evolution 2.0 System Activated ===\033[0m\n");
    printf("Model: \033[1;36m%s\033[0m | Endpoint: \033[1;36m%s\033[0m\n", h->agent->gateway->model, h->agent->gateway->endpoint);
    printf("Type \033[1;33m/help\033[0m for commands or \033[1;31mexit\033[0m to terminate.\n\n");

    while (1) {
        size_t est_tokens = c_agent_total_tokens(h->agent);
        char prompt[128];
        snprintf(prompt, sizeof(prompt), "\033[1;35mcharness [%zu msgs | %zu toks]>\033[0m ", h->agent->msg_count, est_tokens);

        char *line = linenoise(prompt);
        if (!line) break;

        char input_buf[4096];
        strncpy(input_buf, line, sizeof(input_buf) - 1);
        input_buf[sizeof(input_buf) - 1] = '\0';
        input_buf[strcspn(input_buf, "\r\n")] = '\0';

        if (strlen(input_buf) > 0) {
            linenoiseHistoryAdd(input_buf);
            linenoiseHistorySave(".charness_history");
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
                printf("\n\033[1;36m=== CHarness System Status ===\033[0m\n");
                printf("Active Model:    \033[1;33m%s\033[0m\n", h->agent->gateway->model);
                printf("Endpoint:        \033[1;33m%s\033[0m\n", h->agent->gateway->endpoint);
                printf("Working Dir:     \033[1;33m%s\033[0m\n", h->cwd);
                printf("Message History: \033[1;33m%zu messages\033[0m\n", h->agent->msg_count);
                printf("Token Budget:    \033[1;33m%zu estimated tokens\033[0m\n", c_agent_total_tokens(h->agent));
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
            if (strcmp(input_buf, "/sessions") == 0) {
                char *sess_list = c_agent_list_sessions(h->agent);
                printf("\n%s\n", sess_list);
                free(sess_list);
                continue;
            }
            if (strncmp(input_buf, "/save", 5) == 0) {
                const char *s_id = strlen(input_buf) > 5 ? input_buf + 5 : "default_session";
                while (*s_id == ' ') s_id++;
                if (strlen(s_id) == 0) s_id = "default_session";
                if (c_agent_save_session(h->agent, s_id, s_id)) {
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
                    if (c_agent_load_session(h->agent, s_id)) {
                        printf("\033[1;32mSession '%s' successfully loaded (%zu messages restored).\033[0m\n\n", s_id, h->agent->msg_count);
                    } else {
                        printf("\033[1;31mSession '%s' not found or empty.\033[0m\n\n", s_id);
                    }
                } else {
                    printf("Usage: /resume <session_id>. Type /sessions to list available sessions.\n\n");
                }
                continue;
            }
            if (strcmp(input_buf, "/reflect") == 0) {
                char *ref = c_agent_reflect_and_distill(h->agent);
                printf("\n\033[1;36m=== Reflection & Skill Distillation ===\033[0m\n%s\n\n", ref);
                free(ref);
                continue;
            }
            if (strcmp(input_buf, "/clear") == 0) {
                c_agent_clear_history(h->agent);
                printf("\033[1;32mConversation context cleared (system prompt preserved).\033[0m\n\n");
                continue;
            }
            if (strncmp(input_buf, "/compact", 8) == 0) {
                size_t keep = 10;
                if (strlen(input_buf) > 8) {
                    keep = (size_t)atoi(input_buf + 8);
                    if (keep == 0) keep = 10;
                }
                c_agent_compact_history(h->agent, keep);
                printf("\033[1;32mContext compacted to %zu messages.\033[0m\n\n", h->agent->msg_count);
                continue;
            }
            if (strncmp(input_buf, "/memory", 7) == 0) {
                const char *query = strlen(input_buf) > 7 ? input_buf + 7 : "";
                while (*query == ' ') query++;
                char *res = c_agent_search_memory(h->agent, strlen(query) > 0 ? query : "");
                printf("\n\033[1;36m=== Memory Search: '%s' ===\033[0m\n%s\n", query, res ? res : "No results.");
                if (res) free(res);
                continue;
            }
            if (strncmp(input_buf, "/model", 6) == 0) {
                const char *new_m = strlen(input_buf) > 6 ? input_buf + 6 : "";
                while (*new_m == ' ') new_m++;
                if (strlen(new_m) > 0) {
                    free(h->agent->gateway->model);
                    h->agent->gateway->model = strdup(new_m);
                    printf("\033[1;32mActive model switched to: %s\033[0m\n\n", h->agent->gateway->model);
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
                    c_harness_connect_mcp(h, cmd);
                } else {
                    printf("Usage: /mcp <server_command> (e.g. /mcp npx -y @modelcontextprotocol/server-filesystem .)\n\n");
                }
                continue;
            }
            printf("\033[1;31mUnknown command: %s. Type /help for available commands.\033[0m\n\n", input_buf);
            continue;
        }

        c_agent_add_message(h->agent, "user", input_buf);

        // Turn Execution Cycle
        bool turn_running = true;
        int max_steps = 10;

        while (turn_running && max_steps-- > 0) {
            if (!h->agent->gateway->streaming) {
                printf("\033[0;33m[Thinking...]\033[0m\n");
            }
            ModelGatewayResponse resp = c_agent_step(h->agent);

            if (!resp.has_tool_call) {
                if (!h->agent->gateway->streaming) {
                    printf("\n\033[1;34m[C Agent]\033[0m\n%s\n\n", resp.content ? resp.content : "");
                } else {
                    printf("\n\n");
                }
                turn_running = false;
            } else {
                for (size_t i = 0; i < resp.tool_call_count; i++) {
                    ModelParsedToolCall *tc = &resp.tool_calls[i];
                    printf("\n\033[1;33m[Tool Call Request]:\033[0m %s(%s)\n", tc->name, tc->arguments_json);

                    CHarnessRegisteredTool *matched = NULL;
                    for (size_t t = 0; t < h->tool_count; t++) {
                        if (strcmp(h->tools[t].name, tc->name) == 0) {
                            matched = &h->tools[t];
                            break;
                        }
                    }

                    if (!matched || matched->security == PERM_DENY) {
                        printf("\033[1;31m[Denied]: Tool execution blocked.\033[0m\n");
                        c_agent_add_tool_result(h->agent, tc->id, tc->name, "Error: Tool blocked by security policy.");
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
                            c_agent_add_tool_result(h->agent, tc->id, tc->name, "Error: User denied permission for this tool call.");
                            continue;
                        }
                    }

                    JsonValue *args_parsed = json_parse(tc->arguments_json);
                    char *observation = NULL;
                    if (matched->mcp_client) {
                        observation = mcp_client_call_tool(matched->mcp_client, tc->name, args_parsed);
                    } else if (matched->callback) {
                        observation = matched->callback(h->agent, args_parsed);
                    }
                    json_free(args_parsed);

                    printf("\033[0;32m[Observation Output (%zu bytes)]\033[0m\n", observation ? strlen(observation) : 0);
                    c_agent_add_tool_result(h->agent, tc->id, tc->name, observation);
                    if (observation) free(observation);
                }
            }
            model_gateway_response_free(&resp);
        }
    }
}

void c_harness_free(CHarness *h) {
    if (!h) return;
    for (size_t i = 0; i < h->tool_count; i++) {
        if (h->tools[i].name) free(h->tools[i].name);
        if (h->tools[i].custom_script_path) free(h->tools[i].custom_script_path);
    }
    for (size_t s = 0; s < h->mcp_server_count; s++) {
        mcp_client_close(h->mcp_servers[s]);
    }
    c_agent_free(h->agent);
    if (g_harness == h) g_harness = NULL;
    free(h);
}
