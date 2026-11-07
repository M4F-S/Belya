#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "lsp_client.h"
#include "minijson.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>

LspServerType lsp_detect_server_for_file(const char *file_path) {
    if (!file_path) return LSP_SERVER_NONE;
    const char *ext = strrchr(file_path, '.');
    if (!ext) return LSP_SERVER_NONE;

    if (strcmp(ext, ".c") == 0 || strcmp(ext, ".h") == 0 ||
        strcmp(ext, ".cpp") == 0 || strcmp(ext, ".cc") == 0 ||
        strcmp(ext, ".cxx") == 0 || strcmp(ext, ".hpp") == 0) {
        return LSP_SERVER_CLANGD;
    } else if (strcmp(ext, ".py") == 0) {
        return LSP_SERVER_PYRIGHT;
    } else if (strcmp(ext, ".rs") == 0) {
        return LSP_SERVER_RUST_ANALYZER;
    } else if (strcmp(ext, ".go") == 0) {
        return LSP_SERVER_GOPLS;
    } else if (strcmp(ext, ".ts") == 0 || strcmp(ext, ".tsx") == 0 ||
               strcmp(ext, ".js") == 0 || strcmp(ext, ".jsx") == 0) {
        return LSP_SERVER_TYPESCRIPT;
    }
    return LSP_SERVER_NONE;
}

const char *lsp_server_command(LspServerType type) {
    switch (type) {
        case LSP_SERVER_CLANGD: return "clangd";
        case LSP_SERVER_PYRIGHT: return "pyright-langserver";
        case LSP_SERVER_RUST_ANALYZER: return "rust-analyzer";
        case LSP_SERVER_GOPLS: return "gopls";
        case LSP_SERVER_TYPESCRIPT: return "typescript-language-server";
        default: return NULL;
    }
}

static bool check_binary_in_path(const char *bin) {
    if (!bin) return false;
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "command -v %s >/dev/null 2>&1", bin);
    return system(cmd) == 0;
}

static bool lsp_write_all(int fd, const char *data, size_t len) {
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(fd, data + written, len - written);
        if (n <= 0) return false;
        written += (size_t)n;
    }
    return true;
}

static bool lsp_send_payload(LspClient *client, const char *payload) {
    if (!client || client->stdin_fd < 0 || !payload) return false;
    size_t len = strlen(payload);
    char header[128];
    int hlen = snprintf(header, sizeof(header), "Content-Length: %zu\r\n\r\n", len);
    if (!lsp_write_all(client->stdin_fd, header, (size_t)hlen)) return false;
    return lsp_write_all(client->stdin_fd, payload, len);
}

static char *lsp_recv_payload(LspClient *client, int timeout_ms) {
    if (!client || client->stdout_fd < 0) return NULL;

    struct pollfd pfd;
    pfd.fd = client->stdout_fd;
    pfd.events = POLLIN;

    // 1. Read header line by line until \r\n\r\n
    char header_buf[1024];
    size_t header_pos = 0;
    long content_len = -1;

    while (header_pos < sizeof(header_buf) - 1) {
        int prc = poll(&pfd, 1, timeout_ms);
        if (prc <= 0) return NULL;

        char c;
        ssize_t nr = read(client->stdout_fd, &c, 1);
        if (nr <= 0) return NULL;

        header_buf[header_pos++] = c;
        header_buf[header_pos] = '\0';

        if (header_pos >= 4 &&
            header_buf[header_pos - 4] == '\r' && header_buf[header_pos - 3] == '\n' &&
            header_buf[header_pos - 2] == '\r' && header_buf[header_pos - 1] == '\n') {
            break;
        }
    }

    // Parse Content-Length
    const char *cl = strstr(header_buf, "Content-Length:");
    if (!cl) cl = strstr(header_buf, "content-length:");
    if (cl) {
        cl += 15;
        while (*cl == ' ') cl++;
        content_len = atol(cl);
    }
    if (content_len <= 0 || content_len > 10 * 1024 * 1024) return NULL;

    // 2. Read exactly content_len bytes
    char *body = (char *)malloc((size_t)content_len + 1);
    if (!body) return NULL;

    size_t body_read = 0;
    while (body_read < (size_t)content_len) {
        int prc = poll(&pfd, 1, timeout_ms);
        if (prc <= 0) {
            free(body);
            return NULL;
        }
        ssize_t nr = read(client->stdout_fd, body + body_read, (size_t)content_len - body_read);
        if (nr <= 0) {
            free(body);
            return NULL;
        }
        body_read += (size_t)nr;
    }
    body[content_len] = '\0';
    return body;
}

LspClient *lsp_client_spawn(LspServerType type, const char *root_dir) {
    const char *cmd = lsp_server_command(type);
    if (!cmd || !check_binary_in_path(cmd)) {
        return NULL;
    }

    int in_pipe[2];
    int out_pipe[2];
    if (pipe(in_pipe) != 0) return NULL;
    if (pipe(out_pipe) != 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        return NULL;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        return NULL;
    }

    if (pid == 0) {
        // Child
        close(in_pipe[1]);
        close(out_pipe[0]);
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        close(in_pipe[0]);
        close(out_pipe[1]);

        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        if (type == LSP_SERVER_PYRIGHT) {
            execlp("pyright-langserver", "pyright-langserver", "--stdio", (char *)NULL);
            execlp("pyright", "pyright", "--langserver", (char *)NULL);
        } else if (type == LSP_SERVER_TYPESCRIPT) {
            execlp("typescript-language-server", "typescript-language-server", "--stdio", (char *)NULL);
        } else {
            execlp(cmd, cmd, (char *)NULL);
        }
        _exit(127);
    }

    // Parent
    close(in_pipe[0]);
    close(out_pipe[1]);

    LspClient *c = (LspClient *)calloc(1, sizeof(LspClient));
    if (!c) {
        close(in_pipe[1]);
        close(out_pipe[0]);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return NULL;
    }

    c->pid = pid;
    c->stdin_fd = in_pipe[1];
    c->stdout_fd = out_pipe[0];
    c->server_type = type;
    c->next_request_id = 1;
    if (root_dir) {
        strncpy(c->root_dir, root_dir, sizeof(c->root_dir) - 1);
    } else {
        getcwd(c->root_dir, sizeof(c->root_dir));
    }

    return c;
}

bool lsp_client_initialize(LspClient *client) {
    if (!client || client->initialized) return false;

    JsonValue *req = json_create_object();
    json_obj_add(req, "jsonrpc", json_create_string("2.0"));
    json_obj_add(req, "id", json_create_number(client->next_request_id++));
    json_obj_add(req, "method", json_create_string("initialize"));

    JsonValue *params = json_create_object();
    json_obj_add(params, "processId", json_create_number((double)getpid()));

    char root_uri[1100];
    snprintf(root_uri, sizeof(root_uri), "file://%s", client->root_dir);
    json_obj_add(params, "rootUri", json_create_string(root_uri));

    JsonValue *caps = json_create_object();
    JsonValue *text_doc = json_create_object();
    JsonValue *publish_diag = json_create_object();
    json_obj_add(publish_diag, "relatedInformation", json_create_bool(true));
    json_obj_add(text_doc, "publishDiagnostics", publish_diag);
    json_obj_add(caps, "textDocument", text_doc);
    json_obj_add(params, "capabilities", caps);

    json_obj_add(req, "params", params);

    char *req_json = json_serialize(req);
    json_free(req);

    if (!req_json) return false;
    bool sent = lsp_send_payload(client, req_json);
    free(req_json);
    if (!sent) return false;

    // Receive initialize response
    char *resp = lsp_recv_payload(client, 3000);
    if (!resp) return false;
    free(resp);

    // Send initialized notification
    JsonValue *notif = json_create_object();
    json_obj_add(notif, "jsonrpc", json_create_string("2.0"));
    json_obj_add(notif, "method", json_create_string("initialized"));
    json_obj_add(notif, "params", json_create_object());
    char *notif_json = json_serialize(notif);
    json_free(notif);

    if (notif_json) {
        lsp_send_payload(client, notif_json);
        free(notif_json);
    }

    client->initialized = true;
    return true;
}

bool lsp_client_did_open(LspClient *client, const char *file_path, const char *content) {
    if (!client || !client->initialized || !file_path) return false;

    char file_uri[2048];
    if (file_path[0] == '/') {
        snprintf(file_uri, sizeof(file_uri), "file://%s", file_path);
    } else {
        snprintf(file_uri, sizeof(file_uri), "file://%s/%s", client->root_dir, file_path);
    }

    char *owned_content = NULL;
    if (!content) {
        FILE *f = fopen(file_path, "rb");
        if (!f) return false;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz >= 0 && sz < 5 * 1024 * 1024) {
            owned_content = (char *)malloc((size_t)sz + 1);
            if (owned_content) {
                size_t nr = fread(owned_content, 1, (size_t)sz, f);
                owned_content[nr] = '\0';
            }
        }
        fclose(f);
        if (!owned_content) return false;
        content = owned_content;
    }

    const char *lang_id = "c";
    const char *ext = strrchr(file_path, '.');
    if (ext) {
        if (strcmp(ext, ".cpp") == 0 || strcmp(ext, ".cc") == 0) lang_id = "cpp";
        else if (strcmp(ext, ".py") == 0) lang_id = "python";
        else if (strcmp(ext, ".rs") == 0) lang_id = "rust";
        else if (strcmp(ext, ".go") == 0) lang_id = "go";
        else if (strcmp(ext, ".ts") == 0) lang_id = "typescript";
        else if (strcmp(ext, ".js") == 0) lang_id = "javascript";
    }

    JsonValue *notif = json_create_object();
    json_obj_add(notif, "jsonrpc", json_create_string("2.0"));
    json_obj_add(notif, "method", json_create_string("textDocument/didOpen"));

    JsonValue *params = json_create_object();
    JsonValue *td = json_create_object();
    json_obj_add(td, "uri", json_create_string(file_uri));
    json_obj_add(td, "languageId", json_create_string(lang_id));
    json_obj_add(td, "version", json_create_number(1));
    json_obj_add(td, "text", json_create_string(content));
    json_obj_add(params, "textDocument", td);
    json_obj_add(notif, "params", params);

    char *n_json = json_serialize(notif);
    json_free(notif);
    if (owned_content) free(owned_content);

    if (!n_json) return false;
    bool sent = lsp_send_payload(client, n_json);
    free(n_json);
    return sent;
}

char *lsp_client_get_diagnostics(LspClient *client, const char *file_path, int timeout_ms) {
    if (!client || !client->initialized || !file_path) return NULL;

    // Listen for published diagnostics message
    int remaining = timeout_ms;
    while (remaining > 0) {
        char *msg = lsp_recv_payload(client, remaining < 300 ? remaining : 300);
        remaining -= 300;
        if (!msg) continue;

        JsonValue *jv = json_parse(msg);
        free(msg);
        if (!jv) continue;

        const char *method = json_obj_get_str(jv, "method");
        if (method && strcmp(method, "textDocument/publishDiagnostics") == 0) {
            JsonValue *params = json_obj_get(jv, "params");
            JsonValue *diag_arr = params ? json_obj_get(params, "diagnostics") : NULL;

            if (diag_arr && diag_arr->type == JSON_ARRAY && diag_arr->u.array.count > 0) {
                // Format structured diagnostic report
                char *out = json_serialize(diag_arr);
                json_free(jv);
                return out;
            }
        }
        json_free(jv);
    }

    return NULL;
}

char *lsp_client_definition(LspClient *client, const char *file_path, int line, int character, int timeout_ms) {
    if (!client || !client->initialized || !file_path) return NULL;

    char file_uri[2048];
    if (file_path[0] == '/') {
        snprintf(file_uri, sizeof(file_uri), "file://%s", file_path);
    } else {
        snprintf(file_uri, sizeof(file_uri), "file://%s/%s", client->root_dir, file_path);
    }

    int req_id = client->next_request_id++;
    JsonValue *req = json_create_object();
    json_obj_add(req, "jsonrpc", json_create_string("2.0"));
    json_obj_add(req, "id", json_create_number(req_id));
    json_obj_add(req, "method", json_create_string("textDocument/definition"));

    JsonValue *params = json_create_object();
    JsonValue *td = json_create_object();
    json_obj_add(td, "uri", json_create_string(file_uri));
    json_obj_add(params, "textDocument", td);

    JsonValue *pos = json_create_object();
    json_obj_add(pos, "line", json_create_number(line > 0 ? line - 1 : 0));
    json_obj_add(pos, "character", json_create_number(character > 0 ? character - 1 : 0));
    json_obj_add(params, "position", pos);

    json_obj_add(req, "params", params);

    char *req_json = json_serialize(req);
    json_free(req);
    if (!req_json) return NULL;

    if (!lsp_send_payload(client, req_json)) {
        free(req_json);
        return NULL;
    }
    free(req_json);

    // Read response
    int remaining = timeout_ms;
    while (remaining > 0) {
        char *resp = lsp_recv_payload(client, remaining < 300 ? remaining : 300);
        remaining -= 300;
        if (!resp) continue;

        JsonValue *jv = json_parse(resp);
        free(resp);
        if (!jv) continue;

        double resp_id = json_obj_get_num(jv, "id", -1);
        if ((int)resp_id == req_id) {
            JsonValue *result = json_obj_get(jv, "result");
            char *serialized = result ? json_serialize(result) : NULL;
            json_free(jv);
            return serialized;
        }
        json_free(jv);
    }

    return NULL;
}

char *lsp_client_references(LspClient *client, const char *file_path, int line, int character, int timeout_ms) {
    if (!client || !client->initialized || !file_path) return NULL;

    char file_uri[2048];
    if (file_path[0] == '/') {
        snprintf(file_uri, sizeof(file_uri), "file://%s", file_path);
    } else {
        snprintf(file_uri, sizeof(file_uri), "file://%s/%s", client->root_dir, file_path);
    }

    int req_id = client->next_request_id++;
    JsonValue *req = json_create_object();
    json_obj_add(req, "jsonrpc", json_create_string("2.0"));
    json_obj_add(req, "id", json_create_number(req_id));
    json_obj_add(req, "method", json_create_string("textDocument/references"));

    JsonValue *params = json_create_object();
    JsonValue *td = json_create_object();
    json_obj_add(td, "uri", json_create_string(file_uri));
    json_obj_add(params, "textDocument", td);

    JsonValue *pos = json_create_object();
    json_obj_add(pos, "line", json_create_number(line > 0 ? line - 1 : 0));
    json_obj_add(pos, "character", json_create_number(character > 0 ? character - 1 : 0));
    json_obj_add(params, "position", pos);

    JsonValue *ctx = json_create_object();
    json_obj_add(ctx, "includeDeclaration", json_create_bool(true));
    json_obj_add(params, "context", ctx);

    json_obj_add(req, "params", params);

    char *req_json = json_serialize(req);
    json_free(req);
    if (!req_json) return NULL;

    if (!lsp_send_payload(client, req_json)) {
        free(req_json);
        return NULL;
    }
    free(req_json);

    int remaining = timeout_ms;
    while (remaining > 0) {
        char *resp = lsp_recv_payload(client, remaining < 300 ? remaining : 300);
        remaining -= 300;
        if (!resp) continue;

        JsonValue *jv = json_parse(resp);
        free(resp);
        if (!jv) continue;

        double resp_id = json_obj_get_num(jv, "id", -1);
        if ((int)resp_id == req_id) {
            JsonValue *result = json_obj_get(jv, "result");
            char *serialized = result ? json_serialize(result) : NULL;
            json_free(jv);
            return serialized;
        }
        json_free(jv);
    }

    return NULL;
}

void lsp_client_shutdown(LspClient *client) {
    if (!client) return;

    if (client->initialized) {
        JsonValue *req = json_create_object();
        json_obj_add(req, "jsonrpc", json_create_string("2.0"));
        json_obj_add(req, "id", json_create_number(client->next_request_id++));
        json_obj_add(req, "method", json_create_string("shutdown"));
        char *r_json = json_serialize(req);
        json_free(req);
        if (r_json) {
            lsp_send_payload(client, r_json);
            free(r_json);
            char *resp = lsp_recv_payload(client, 500);
            if (resp) free(resp);
        }

        JsonValue *exit_notif = json_create_object();
        json_obj_add(exit_notif, "jsonrpc", json_create_string("2.0"));
        json_obj_add(exit_notif, "method", json_create_string("exit"));
        char *e_json = json_serialize(exit_notif);
        json_free(exit_notif);
        if (e_json) {
            lsp_send_payload(client, e_json);
            free(e_json);
        }
    }

    if (client->stdin_fd >= 0) {
        close(client->stdin_fd);
        client->stdin_fd = -1;
    }
    if (client->stdout_fd >= 0) {
        close(client->stdout_fd);
        client->stdout_fd = -1;
    }

    if (client->pid > 0) {
        kill(client->pid, SIGTERM);
        usleep(10000);
        waitpid(client->pid, NULL, WNOHANG);
    }
}

void lsp_client_free(LspClient *client) {
    if (!client) return;
    lsp_client_shutdown(client);
    free(client);
}

char *lsp_preflight_diagnostics(const char *file_path) {
    if (!file_path) return NULL;
    LspServerType type = lsp_detect_server_for_file(file_path);
    if (type == LSP_SERVER_NONE) return NULL;

    const char *cmd = lsp_server_command(type);
    if (!cmd || !check_binary_in_path(cmd)) return NULL;

    LspClient *client = lsp_client_spawn(type, NULL);
    if (!client) return NULL;

    if (!lsp_client_initialize(client)) {
        lsp_client_free(client);
        return NULL;
    }

    if (!lsp_client_did_open(client, file_path, NULL)) {
        lsp_client_free(client);
        return NULL;
    }

    char *raw_diag = lsp_client_get_diagnostics(client, file_path, 800);
    lsp_client_free(client);

    if (!raw_diag) return NULL;

    // Check if errors exist in raw_diag
    JsonValue *jv = json_parse(raw_diag);
    free(raw_diag);
    if (!jv) return NULL;

    if (jv->type != JSON_ARRAY || jv->u.array.count == 0) {
        json_free(jv);
        return NULL;
    }

    // Format structured compiler diagnostic
    char report[4096];
    size_t pos = 0;
    pos += (size_t)snprintf(report + pos, sizeof(report) - pos,
                           "[LSP Language Server Diagnostics: %s (%s)]\n", file_path, cmd);

    bool has_error = false;
    for (size_t i = 0; i < jv->u.array.count && pos < sizeof(report) - 200; i++) {
        JsonValue *item = jv->u.array.items[i];
        double sev = json_obj_get_num(item, "severity", 1);
        const char *msg = json_obj_get_str(item, "message");
        JsonValue *rng = json_obj_get(item, "range");
        JsonValue *start = rng ? json_obj_get(rng, "start") : NULL;
        int line = start ? (int)json_obj_get_num(start, "line", 0) + 1 : 1;
        int col = start ? (int)json_obj_get_num(start, "character", 0) + 1 : 1;

        const char *sev_str = (sev == 1) ? "ERROR" : (sev == 2 ? "WARNING" : "INFO");
        if (sev == 1 || sev == 2) has_error = true;

        pos += (size_t)snprintf(report + pos, sizeof(report) - pos,
                               "  • %s line %d:%d: %s\n",
                               sev_str, line, col, msg ? msg : "Unknown diagnostic");
    }

    json_free(jv);

    if (!has_error) return NULL;

    char *result = (char *)malloc(pos + 1);
    if (result) {
        memcpy(result, report, pos);
        result[pos] = '\0';
    }
    return result;
}
