#include "c_harness.h"
#include "linenoise.h"
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
static bool g_reasoning_header_printed = false;
static bool g_content_header_printed = false;

static void cli_stream_callback(const char *chunk, bool is_reasoning, void *userdata) {
    (void)userdata;
    if (is_reasoning) {
        if (!g_reasoning_header_printed) {
            printf("\n\033[0;36m[Thinking / Reasoning]:\033[0m\n\033[0;90m");
            g_reasoning_header_printed = true;
        }
        printf("%s", chunk);
        fflush(stdout);
    } else {
        if (g_reasoning_header_printed && !g_content_header_printed) {
            printf("\033[0m\n\n\033[1;34m[C Agent]:\033[0m\n");
            g_content_header_printed = true;
        } else if (!g_content_header_printed) {
            printf("\n\033[1;34m[C Agent]:\033[0m\n");
            g_content_header_printed = true;
        }
        printf("%s", chunk);
        fflush(stdout);
    }
}

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
            // Child exited, drain remaining pipe bytes
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

    // If offset or limit specified, read line-by-line
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

    // 5. list_dir
    c_harness_register_tool(h, "list_dir", "List files and directories in path",
        build_string_param_schema("path", "Directory path"), PERM_ALLOW, tool_list_dir);

    // 6. search_files
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

    // 7. save_memory
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

    // 8. recall_memory
    c_harness_register_tool(h, "recall_memory", "Search SQLite memory for past solutions and skills",
        build_string_param_schema("query", "Search term"), PERM_ALLOW, tool_recall_memory);

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
    h->tool_count++;

    c_agent_register_schema(h->agent, name, desc, params);
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
    printf("Context Size:    \033[1;33m%zu messages\033[0m\n", h->agent->msg_count);
    printf("Streaming:       \033[1;33m%s\033[0m\n", h->agent->gateway->enable_streaming ? "Enabled (SSE Real-Time)" : "Disabled");
    printf("FTS5 Memory:     \033[1;33m%s\033[0m\n\n", h->agent->has_fts5 ? "Enabled (BM25)" : "Disabled (LIKE fallback)");
    printf("\033[1;32mAvailable Slash Commands:\033[0m\n");
    printf("  /help            Show this help reference\n");
    printf("  /tools           List all registered tools and permissions\n");
    printf("  /clear           Reset conversation history (preserves system prompt)\n");
    printf("  /compact [N]     Prune older messages, keeping N recent (default: 10)\n");
    printf("  /memory [query]  Search SQLite persistent memory directly\n");
    printf("  /model <name>    Dynamically change the active AI model\n");
    printf("  /stream <on|off> Toggle real-time SSE token streaming\n");
    printf("  /cwd [path]      View or change working directory\n");
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
        printf("  - \033[1;37m%-15s\033[0m [%s%s\033[0m] %s\n", 
            h->tools[i].name, sec_color, sec_str, 
            i < h->agent->schema_count ? h->agent->schemas[i].description : "");
    }
    printf("\n");
}

static void linenoise_completion_hook(const char *buf, linenoiseCompletions *lc) {
    if (buf[0] == '/') {
        if (strncmp(buf, "/h", 2) == 0) linenoiseAddCompletion(lc, "/help");
        if (strncmp(buf, "/t", 2) == 0) linenoiseAddCompletion(lc, "/tools");
        if (strncmp(buf, "/cl", 3) == 0) linenoiseAddCompletion(lc, "/clear");
        if (strncmp(buf, "/co", 3) == 0) linenoiseAddCompletion(lc, "/compact");
        if (strncmp(buf, "/me", 3) == 0) linenoiseAddCompletion(lc, "/memory");
        if (strncmp(buf, "/mo", 3) == 0) linenoiseAddCompletion(lc, "/model");
        if (strncmp(buf, "/st", 3) == 0) linenoiseAddCompletion(lc, "/stream");
        if (strncmp(buf, "/cw", 3) == 0) linenoiseAddCompletion(lc, "/cwd");
    }
}

void c_harness_repl(CHarness *h) {
    printf("\033[1;32m=== CHarness & CAgent System Activated ===\033[0m\n");
    printf("Model: \033[1;36m%s\033[0m | Endpoint: \033[1;36m%s\033[0m\n", h->agent->gateway->model, h->agent->gateway->endpoint);
    printf("Real-Time Streaming: \033[1;32m%s\033[0m | Line Editing: \033[1;32mLinenoise Enabled\033[0m\n", 
        h->agent->gateway->enable_streaming ? "ON" : "OFF");
    printf("Type \033[1;33m/help\033[0m for commands or \033[1;31mexit\033[0m to terminate.\n\n");

    linenoiseSetCompletionCallback(linenoise_completion_hook);
    linenoiseHistoryLoad(".charness_history");

    while (1) {
        char prompt_buf[128];
        snprintf(prompt_buf, sizeof(prompt_buf), "\033[1;35mcharness [%zu msgs]>\033[0m ", h->agent->msg_count);

        char *line = linenoise(prompt_buf);
        if (!line) break;

        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, "exit") == 0) {
            free(line);
            break;
        }
        if (strlen(line) == 0) {
            free(line);
            continue;
        }

        linenoiseHistoryAdd(line);
        linenoiseHistorySave(".charness_history");

        // Handle Slash Commands
        if (line[0] == '/') {
            if (strcmp(line, "/help") == 0) {
                print_help(h);
                free(line);
                continue;
            }
            if (strcmp(line, "/tools") == 0) {
                list_tools(h);
                free(line);
                continue;
            }
            if (strcmp(line, "/clear") == 0) {
                c_agent_clear_history(h->agent);
                printf("\033[1;32mConversation context cleared (system message preserved).\033[0m\n\n");
                free(line);
                continue;
            }
            if (strncmp(line, "/compact", 8) == 0) {
                size_t keep = 10;
                if (strlen(line) > 8) {
                    keep = (size_t)atoi(line + 8);
                    if (keep == 0) keep = 10;
                }
                c_agent_compact_history(h->agent, keep);
                printf("\033[1;32mContext compacted to %zu messages.\033[0m\n\n", h->agent->msg_count);
                free(line);
                continue;
            }
            if (strncmp(line, "/memory", 7) == 0) {
                const char *query = strlen(line) > 7 ? line + 7 : "";
                while (*query == ' ') query++;
                char *res = c_agent_search_memory(h->agent, strlen(query) > 0 ? query : "");
                printf("\n\033[1;36m=== Memory Search: '%s' ===\033[0m\n%s\n", query, res ? res : "No results.");
                if (res) free(res);
                free(line);
                continue;
            }
            if (strncmp(line, "/model", 6) == 0) {
                const char *new_m = strlen(line) > 6 ? line + 6 : "";
                while (*new_m == ' ') new_m++;
                if (strlen(new_m) > 0) {
                    free(h->agent->gateway->model);
                    h->agent->gateway->model = strdup(new_m);
                    printf("\033[1;32mActive model switched to: %s\033[0m\n\n", h->agent->gateway->model);
                } else {
                    printf("Current model: %s. Usage: /model <model_name>\n\n", h->agent->gateway->model);
                }
                free(line);
                continue;
            }
            if (strncmp(line, "/stream", 7) == 0) {
                const char *opt = strlen(line) > 7 ? line + 7 : "";
                while (*opt == ' ') opt++;
                if (strcmp(opt, "off") == 0 || strcmp(opt, "0") == 0 || strcmp(opt, "false") == 0) {
                    h->agent->gateway->enable_streaming = false;
                    printf("\033[1;33mReal-time SSE streaming disabled.\033[0m\n\n");
                } else {
                    h->agent->gateway->enable_streaming = true;
                    printf("\033[1;32mReal-time SSE streaming enabled.\033[0m\n\n");
                }
                free(line);
                continue;
            }
            if (strncmp(line, "/cwd", 4) == 0) {
                const char *new_d = strlen(line) > 4 ? line + 4 : "";
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
                free(line);
                continue;
            }
            printf("\033[1;31mUnknown command: %s. Type /help for available commands.\033[0m\n\n", line);
            free(line);
            continue;
        }

        c_agent_add_message(h->agent, "user", line);
        free(line);

        // Turn Execution Cycle
        bool turn_running = true;
        int max_steps = 10;

        while (turn_running && max_steps-- > 0) {
            g_reasoning_header_printed = false;
            g_content_header_printed = false;

            if (h->agent->gateway->enable_streaming) {
                model_gateway_set_streaming(h->agent->gateway, true, cli_stream_callback, NULL);
            } else {
                model_gateway_set_streaming(h->agent->gateway, false, NULL, NULL);
                printf("\033[0;33m[Thinking...]\033[0m\n");
            }

            ModelGatewayResponse resp = c_agent_step(h->agent);

            if (g_reasoning_header_printed) {
                printf("\033[0m\n");
            }

            if (!resp.has_tool_call) {
                if (!g_content_header_printed) {
                    printf("\n\033[1;34m[C Agent]\033[0m\n%s\n\n", resp.content ? resp.content : "");
                } else {
                    printf("\n\n");
                }
                turn_running = false;
            } else {
                if (g_content_header_printed) printf("\n");

                for (size_t i = 0; i < resp.tool_call_count; i++) {
                    ModelParsedToolCall *tc = &resp.tool_calls[i];
                    printf("\033[1;33m[Tool Call Request]:\033[0m %s(%s)\n", tc->name, tc->arguments_json);

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
                        if (!harness_ask_permission(tc->name, tc->arguments_json)) {
                            printf("\033[1;31m[Rejected]: Operation cancelled by operator.\033[0m\n");
                            c_agent_add_tool_result(h->agent, tc->id, tc->name, "Error: User denied permission for this tool call.");
                            continue;
                        }
                    }

                    JsonValue *args_parsed = json_parse(tc->arguments_json);
                    char *observation = matched->callback(h->agent, args_parsed);
                    json_free(args_parsed);

                    printf("\033[0;32m[Observation Output (%zu bytes)]\033[0m\n", observation ? strlen(observation) : 0);
                    c_agent_add_tool_result(h->agent, tc->id, tc->name, observation);
                    free(observation);
                }
            }
            model_gateway_response_free(&resp);
        }
    }
}

void c_harness_free(CHarness *h) {
    if (!h) return;
    for (size_t i = 0; i < h->tool_count; i++) {
        free(h->tools[i].name);
    }
    c_agent_free(h->agent);
    if (g_harness == h) g_harness = NULL;
    free(h);
}
