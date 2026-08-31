#include "mcp_client.h"
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <signal.h>
#include <fcntl.h>

static char *read_line_timeout(int fd, int timeout_sec) {
    DynString ds = dyn_str_new();
    struct timeval start, now;
    gettimeofday(&start, NULL);

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    while (1) {
        char c;
        ssize_t bytes = read(fd, &c, 1);
        if (bytes > 0) {
            if (c == '\n') break;
            if (c != '\r') {
                char tmp[2] = {c, '\0'};
                dyn_str_append(&ds, tmp);
            }
        } else {
            gettimeofday(&now, NULL);
            double elapsed = (now.tv_sec - start.tv_sec) + (now.tv_usec - start.tv_usec) / 1000000.0;
            if (elapsed > timeout_sec) break;
            struct timespec ts = {0, 5000000L};
            nanosleep(&ts, NULL);
        }
    }
    if (ds.len == 0) {
        dyn_str_free(&ds);
        return NULL;
    }
    return ds.data;
}

MCPClient *mcp_client_start(const char *command_line) {
    if (!command_line || strlen(command_line) == 0) return NULL;

    int stdin_pipe[2];
    int stdout_pipe[2];

    if (pipe(stdin_pipe) == -1 || pipe(stdout_pipe) == -1) return NULL;

    pid_t pid = fork();
    if (pid == -1) {
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        return NULL;
    }

    if (pid == 0) {
        // Child
        close(stdin_pipe[1]);
        dup2(stdin_pipe[0], STDIN_FILENO);
        close(stdin_pipe[0]);

        close(stdout_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        close(stdout_pipe[1]);

        execl("/bin/sh", "sh", "-c", command_line, (char *)NULL);
        _exit(127);
    }

    // Parent
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);

    MCPClient *client = calloc(1, sizeof(MCPClient));
    client->command = strdup(command_line);
    client->stdin_fd = stdin_pipe[1];
    client->stdout_fd = stdout_pipe[0];
    client->pid = pid;
    client->request_id = 1;

    // Send initialize request
    JsonValue *init_req = json_create_object();
    json_obj_add(init_req, "jsonrpc", json_create_string("2.0"));
    json_obj_add(init_req, "id", json_create_number(client->request_id++));
    json_obj_add(init_req, "method", json_create_string("initialize"));

    JsonValue *params = json_create_object();
    json_obj_add(params, "protocolVersion", json_create_string("2024-11-05"));
    json_obj_add(params, "capabilities", json_create_object());
    JsonValue *cinfo = json_create_object();
    json_obj_add(cinfo, "name", json_create_string("charness"));
    json_obj_add(cinfo, "version", json_create_string("2.0"));
    json_obj_add(params, "clientInfo", cinfo);
    json_obj_add(init_req, "params", params);

    char *req_str = json_serialize(init_req);
    json_free(init_req);

    write(client->stdin_fd, req_str, strlen(req_str));
    write(client->stdin_fd, "\n", 1);
    free(req_str);

    // Read initialize response
    char *resp_str = read_line_timeout(client->stdout_fd, 5);
    if (!resp_str) {
        mcp_client_close(client);
        return NULL;
    }

    JsonValue *init_resp = json_parse(resp_str);
    free(resp_str);
    if (!init_resp) {
        mcp_client_close(client);
        return NULL;
    }
    json_free(init_resp);

    // Send initialized notification
    const char *notif = "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n";
    write(client->stdin_fd, notif, strlen(notif));

    client->connected = true;
    return client;
}

JsonValue *mcp_client_request(MCPClient *client, const char *method, JsonValue *params) {
    if (!client || !client->connected) return NULL;

    JsonValue *req = json_create_object();
    json_obj_add(req, "jsonrpc", json_create_string("2.0"));
    json_obj_add(req, "id", json_create_number(client->request_id++));
    json_obj_add(req, "method", json_create_string(method));
    if (params) {
        json_obj_add(req, "params", params);
    }

    char *req_str = json_serialize(req);
    json_free(req);

    write(client->stdin_fd, req_str, strlen(req_str));
    write(client->stdin_fd, "\n", 1);
    free(req_str);

    char *resp_str = read_line_timeout(client->stdout_fd, 15);
    if (!resp_str) return NULL;

    JsonValue *resp = json_parse(resp_str);
    free(resp_str);
    return resp;
}

JsonValue *mcp_client_list_tools(MCPClient *client) {
    JsonValue *resp = mcp_client_request(client, "tools/list", NULL);
    if (!resp) return NULL;

    JsonValue *result = json_obj_get(resp, "result");
    if (!result) {
        json_free(resp);
        return NULL;
    }

    JsonValue *tools = json_obj_get(result, "tools");
    if (!tools) {
        json_free(resp);
        return NULL;
    }

    // Clone tools array so we can free the wrapper resp
    char *tools_str = json_serialize(tools);
    json_free(resp);

    JsonValue *tools_clone = json_parse(tools_str);
    free(tools_str);
    return tools_clone;
}

char *mcp_client_call_tool(MCPClient *client, const char *tool_name, const JsonValue *arguments) {
    if (!client || !client->connected) return strdup("Error: MCP client not connected.");

    JsonValue *params = json_create_object();
    json_obj_add(params, "name", json_create_string(tool_name));
    json_obj_add(params, "arguments", (JsonValue *)(arguments ? arguments : json_create_object()));

    JsonValue *resp = mcp_client_request(client, "tools/call", params);

    // Detach arguments if shared
    params->u.object.members[1].value = NULL;
    json_free(params);

    if (!resp) return strdup("Error: MCP server request timed out or returned empty response.");

    JsonValue *err_obj = json_obj_get(resp, "error");
    if (err_obj) {
        const char *m = json_obj_get_str(err_obj, "message");
        DynString err_ds = dyn_str_new();
        dyn_str_appendf(&err_ds, "MCP Error: %s", m ? m : "Unknown error");
        json_free(resp);
        return err_ds.data;
    }

    JsonValue *result = json_obj_get(resp, "result");
    if (!result) {
        json_free(resp);
        return strdup("Error: Missing result in MCP response.");
    }

    JsonValue *content = json_obj_get(result, "content");
    DynString out = dyn_str_new();

    if (content && content->type == JSON_ARRAY) {
        for (size_t i = 0; i < content->u.array.count; i++) {
            JsonValue *item = content->u.array.items[i];
            const char *text = json_obj_get_str(item, "text");
            if (text) {
                dyn_str_append(&out, text);
            }
        }
    } else {
        char *serialized = json_serialize(result);
        dyn_str_append(&out, serialized);
        free(serialized);
    }

    json_free(resp);
    if (out.len == 0) dyn_str_append(&out, "Tool execution completed with empty result.");
    return out.data;
}

void mcp_client_close(MCPClient *client) {
    if (!client) return;
    client->connected = false;
    close(client->stdin_fd);
    close(client->stdout_fd);
    if (client->pid > 0) {
        kill(client->pid, SIGTERM);
        struct timespec ts = {0, 50000000L};
        nanosleep(&ts, NULL);
        waitpid(client->pid, NULL, WNOHANG);
    }
    if (client->command) free(client->command);
    free(client);
}
