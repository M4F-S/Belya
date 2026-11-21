#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "agent_mesh.h"
#include "minijson.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/time.h>

struct AgentMesh {
    char agent_name[64];
    char socket_path[108];
    bool is_server;
    bool is_client;
    bool listener_started;
    int listen_fd;
    volatile bool running;
    pthread_t listener_thread;
    pthread_mutex_t lock;
    int next_msg_id;
    AgentMeshHandlerFn handler;
    void *userdata;
};

static void safe_copy_str(char *dst, const char *src, size_t dst_size) {
    if (!dst || dst_size == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t i = 0;
    while (i + 1 < dst_size && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static double mesh_get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

static bool mesh_write_all(int fd, const char *data, size_t len) {
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(fd, data + written, len - written);
        if (n <= 0) return false;
        written += (size_t)n;
    }
    return true;
}

static void mesh_handle_incoming_line(AgentMesh *mesh, int client_fd, const char *line) {
    if (!mesh || !line || strlen(line) == 0) return;

    JsonValue *jv = json_parse(line);
    if (!jv) return;

    const char *src = json_obj_get_str(jv, "src");
    const char *dst = json_obj_get_str(jv, "dst");
    const char *action = json_obj_get_str(jv, "action");
    int msg_id = (int)json_obj_get_num(jv, "id", 0);
    bool is_resp = json_obj_get_bool(jv, "resp", false);
    JsonValue *p_val = json_obj_get(jv, "payload");
    char *payload_str = p_val ? json_serialize(p_val) : NULL;

    if (!is_resp) {
        // Incoming request or notification
        if (action && strcmp(action, "ping") == 0) {
            // Reply pong
            JsonValue *pong = json_create_object();
            json_obj_add(pong, "mesh", json_create_string("1.0"));
            json_obj_add(pong, "id", json_create_number(msg_id));
            json_obj_add(pong, "src", json_create_string(mesh->agent_name));
            json_obj_add(pong, "dst", json_create_string(src ? src : "peer"));
            json_obj_add(pong, "action", json_create_string("pong"));
            json_obj_add(pong, "resp", json_create_bool(true));
            json_obj_add(pong, "payload", json_create_string("pong"));
            char *serialized = json_serialize(pong);
            json_free(pong);
            if (serialized) {
                mesh_write_all(client_fd, serialized, strlen(serialized));
                mesh_write_all(client_fd, "\n", 1);
                free(serialized);
            }
        } else if (mesh->handler) {
            AgentMeshMessage msg;
            safe_copy_str(msg.source, src ? src : "", sizeof(msg.source));
            safe_copy_str(msg.target, dst ? dst : "", sizeof(msg.target));
            safe_copy_str(msg.action, action ? action : "", sizeof(msg.action));
            msg.payload_json = payload_str;
            msg.request_id = msg_id;
            msg.is_response = false;

            char *resp_payload = mesh->handler(mesh, &msg, mesh->userdata);

            JsonValue *resp_jv = json_create_object();
            json_obj_add(resp_jv, "mesh", json_create_string("1.0"));
            json_obj_add(resp_jv, "id", json_create_number(msg_id));
            json_obj_add(resp_jv, "src", json_create_string(mesh->agent_name));
            json_obj_add(resp_jv, "dst", json_create_string(src ? src : "peer"));
            json_obj_add(resp_jv, "action", json_create_string(action ? action : "reply"));
            json_obj_add(resp_jv, "resp", json_create_bool(true));
            if (resp_payload && (resp_payload[0] == '{' || resp_payload[0] == '[')) {
                JsonValue *p_parsed = json_parse(resp_payload);
                if (p_parsed) {
                    json_obj_add(resp_jv, "payload", p_parsed);
                } else {
                    json_obj_add(resp_jv, "payload", json_create_string(resp_payload));
                }
            } else {
                json_obj_add(resp_jv, "payload", json_create_string(resp_payload ? resp_payload : "ok"));
            }
            char *serialized = json_serialize(resp_jv);
            json_free(resp_jv);
            if (resp_payload) free(resp_payload);

            if (serialized) {
                mesh_write_all(client_fd, serialized, strlen(serialized));
                mesh_write_all(client_fd, "\n", 1);
                free(serialized);
            }
        }
    }

    if (payload_str) free(payload_str);
    json_free(jv);
}

static void *mesh_server_worker(void *arg) {
    AgentMesh *mesh = (AgentMesh *)arg;

    while (mesh->running && mesh->listen_fd >= 0) {
        struct pollfd pfd;
        pfd.fd = mesh->listen_fd;
        pfd.events = POLLIN;

        int prc = poll(&pfd, 1, 200);
        if (prc < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (prc == 0) continue;

        int conn = accept(mesh->listen_fd, NULL, NULL);
        if (conn < 0) continue;

        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        setsockopt(conn, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(conn, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        char buf[8192];
        size_t buf_pos = 0;

        while (mesh->running) {
            struct pollfd cpfd;
            cpfd.fd = conn;
            cpfd.events = POLLIN;
            int cprc = poll(&cpfd, 1, 500);
            if (cprc < 0) break;
            if (cprc == 0) break;

            ssize_t nr = read(conn, buf + buf_pos, sizeof(buf) - buf_pos - 1);
            if (nr <= 0) break;
            buf_pos += (size_t)nr;
            buf[buf_pos] = '\0';

            char *line_start = buf;
            char *newline = NULL;
            while ((newline = strchr(line_start, '\n')) != NULL) {
                *newline = '\0';
                mesh_handle_incoming_line(mesh, conn, line_start);
                line_start = newline + 1;
            }

            if (line_start > buf) {
                size_t remaining = buf_pos - (size_t)(line_start - buf);
                memmove(buf, line_start, remaining);
                buf_pos = remaining;
                buf[buf_pos] = '\0';
            }
        }

        close(conn);
    }

    return NULL;
}

AgentMesh *agent_mesh_init(const char *agent_name, const char *socket_path) {
    AgentMesh *mesh = (AgentMesh *)calloc(1, sizeof(AgentMesh));
    if (!mesh) return NULL;

    safe_copy_str(mesh->agent_name, agent_name ? agent_name : "agent", sizeof(mesh->agent_name));

    if (socket_path && strlen(socket_path) > 0) {
        safe_copy_str(mesh->socket_path, socket_path, sizeof(mesh->socket_path));
    } else {
        const char *env_path = getenv("AGENT_MESH_SOCK_PATH");
        if (env_path && strlen(env_path) > 0) {
            safe_copy_str(mesh->socket_path, env_path, sizeof(mesh->socket_path));
        } else if (access("/run/agent-mesh", W_OK) == 0) {
            snprintf(mesh->socket_path, sizeof(mesh->socket_path), "/run/agent-mesh/%.80s.sock", mesh->agent_name);
        } else {
            snprintf(mesh->socket_path, sizeof(mesh->socket_path), "/tmp/%.80s_mesh.sock", mesh->agent_name);
        }
    }

    mesh->listen_fd = -1;
    mesh->next_msg_id = 1;
    pthread_mutex_init(&mesh->lock, NULL);

    return mesh;
}

void agent_mesh_set_handler(AgentMesh *mesh, AgentMeshHandlerFn handler, void *userdata) {
    if (!mesh) return;
    pthread_mutex_lock(&mesh->lock);
    mesh->handler = handler;
    mesh->userdata = userdata;
    pthread_mutex_unlock(&mesh->lock);
}

bool agent_mesh_start(AgentMesh *mesh) {
    if (!mesh) return false;

    // Check if socket already exists and has a live server
    int test_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", mesh->socket_path);

    if (test_fd >= 0 && connect(test_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        close(test_fd);
        // Another server is already active on this exact path (e.g. multi-agent unit test sharing socket)
        mesh->is_server = false;
        mesh->is_client = true;
        mesh->running = true;
        return true;
    }
    if (test_fd >= 0) close(test_fd);

    // Stale socket or fresh startup: bind as server
    unlink(mesh->socket_path);

    int srv_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (srv_fd < 0) return false;

    int opt = 1;
    setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(srv_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(srv_fd);
        return false;
    }
    chmod(mesh->socket_path, 0666);

    if (listen(srv_fd, 16) != 0) {
        close(srv_fd);
        unlink(mesh->socket_path);
        return false;
    }

    // Maintain backward-compatible /run/agent-mesh/mesh.sock symlink pointing to belya or active node
    if (access("/run/agent-mesh", W_OK) == 0) {
        if (strcmp(mesh->agent_name, "belya") == 0 || access("/run/agent-mesh/mesh.sock", F_OK) != 0) {
            unlink("/run/agent-mesh/mesh.sock");
            if (symlink(mesh->socket_path, "/run/agent-mesh/mesh.sock") != 0) {
                // Best-effort symlink for backward compatibility
            }
        }
    }

    mesh->is_server = true;
    mesh->is_client = false;
    mesh->listen_fd = srv_fd;
    mesh->running = true;
    if (pthread_create(&mesh->listener_thread, NULL, mesh_server_worker, mesh) == 0) {
        mesh->listener_started = true;
    }

    return true;
}

static bool resolve_target_socket(const AgentMesh *mesh, const char *target, char *out_path, size_t out_max) {
    if (!out_path || out_max < 16) return false;

    const char *tgt = (target && strlen(target) > 0) ? target : "peer";
    if (strcmp(tgt, "peer") == 0) {
        tgt = (strcmp(mesh->agent_name, "belya") == 0) ? "almaz" : "belya";
    }

    // Direct filesystem path
    if (tgt[0] == '/') {
        safe_copy_str(out_path, tgt, out_max);
        return true;
    }

    // If running in client mode on a shared socket (e.g. test harness)
    if (mesh->is_client && mesh->socket_path[0] != '\0') {
        if (access(mesh->socket_path, F_OK) == 0) {
            safe_copy_str(out_path, mesh->socket_path, out_max);
            return true;
        }
    }

    // Check dedicated socket in /run/agent-mesh/<target>.sock
    char cand1[108];
    snprintf(cand1, sizeof(cand1), "/run/agent-mesh/%.80s.sock", tgt);
    if (access(cand1, F_OK) == 0) {
        safe_copy_str(out_path, cand1, out_max);
        return true;
    }

    // Check /tmp/<target>_mesh.sock
    char cand2[108];
    snprintf(cand2, sizeof(cand2), "/tmp/%.80s_mesh.sock", tgt);
    if (access(cand2, F_OK) == 0) {
        safe_copy_str(out_path, cand2, out_max);
        return true;
    }

    // Check instance custom socket if not self
    if (mesh->socket_path[0] != '\0' && access(mesh->socket_path, F_OK) == 0) {
        safe_copy_str(out_path, mesh->socket_path, out_max);
        return true;
    }

    // Check shared mesh.sock
    if (access("/run/agent-mesh/mesh.sock", F_OK) == 0) {
        safe_copy_str(out_path, "/run/agent-mesh/mesh.sock", out_max);
        return true;
    }

    // Default fallback
    safe_copy_str(out_path, cand1, out_max);
    return true;
}

char *agent_mesh_consult(AgentMesh *mesh, const char *target, const char *action, const char *query_json, int timeout_ms) {
    if (!mesh) return NULL;

    char target_sock[108];
    if (!resolve_target_socket(mesh, target, target_sock, sizeof(target_sock))) {
        return NULL;
    }

    int cli_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (cli_fd < 0) return NULL;

    struct timeval tv;
    tv.tv_sec = timeout_ms > 0 ? (timeout_ms / 1000) : 4;
    tv.tv_usec = timeout_ms > 0 ? ((timeout_ms % 1000) * 1000) : 0;
    if (tv.tv_sec == 0 && tv.tv_usec < 500000) {
        tv.tv_usec = 500000;
    }
    setsockopt(cli_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(cli_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", target_sock);

    if (connect(cli_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(cli_fd);
        return NULL;
    }

    pthread_mutex_lock(&mesh->lock);
    int req_id = mesh->next_msg_id++;
    pthread_mutex_unlock(&mesh->lock);

    JsonValue *jv = json_create_object();
    json_obj_add(jv, "mesh", json_create_string("1.0"));
    json_obj_add(jv, "id", json_create_number(req_id));
    json_obj_add(jv, "src", json_create_string(mesh->agent_name));
    json_obj_add(jv, "dst", json_create_string(target ? target : "peer"));
    json_obj_add(jv, "action", json_create_string(action ? action : "consult"));
    json_obj_add(jv, "resp", json_create_bool(false));

    if (query_json && strlen(query_json) > 0) {
        JsonValue *parsed = json_parse(query_json);
        if (parsed) {
            json_obj_add(jv, "payload", parsed);
        } else {
            json_obj_add(jv, "payload", json_create_string(query_json));
        }
    } else {
        json_obj_add(jv, "payload", json_create_object());
    }

    char *msg_str = json_serialize(jv);
    json_free(jv);

    if (!msg_str) {
        close(cli_fd);
        return NULL;
    }

    bool sent = mesh_write_all(cli_fd, msg_str, strlen(msg_str)) &&
                mesh_write_all(cli_fd, "\n", 1);
    free(msg_str);

    if (!sent) {
        close(cli_fd);
        return NULL;
    }

    // Wait for response line with timeout
    struct pollfd pfd;
    pfd.fd = cli_fd;
    pfd.events = POLLIN;

    int prc = poll(&pfd, 1, timeout_ms > 0 ? timeout_ms : 4000);
    if (prc <= 0) {
        close(cli_fd);
        return NULL;
    }

    char resp_buf[8192];
    ssize_t nr = read(cli_fd, resp_buf, sizeof(resp_buf) - 1);
    close(cli_fd);

    if (nr <= 0) return NULL;
    resp_buf[nr] = '\0';

    char *nl = strchr(resp_buf, '\n');
    if (nl) *nl = '\0';

    JsonValue *resp_jv = json_parse(resp_buf);
    if (!resp_jv) return strdup(resp_buf);

    JsonValue *p_val = json_obj_get(resp_jv, "payload");
    char *res = NULL;
    if (p_val) {
        res = json_serialize(p_val);
    } else {
        res = json_serialize(resp_jv);
    }
    json_free(resp_jv);

    return res;
}

bool agent_mesh_send(AgentMesh *mesh, const char *target, const char *action, const char *payload_json, int *out_req_id) {
    char *res = agent_mesh_consult(mesh, target, action, payload_json, 2000);
    if (res) {
        free(res);
        if (out_req_id) *out_req_id = 1;
        return true;
    }
    return false;
}

bool agent_mesh_ping(AgentMesh *mesh, const char *target, double *out_rtt_ms) {
    if (!mesh) return false;
    double t0 = mesh_get_time_ms();
    char *res = agent_mesh_consult(mesh, target, "ping", "{}", 2000);
    double t1 = mesh_get_time_ms();
    if (res) {
        if (out_rtt_ms) *out_rtt_ms = t1 - t0;
        free(res);
        return true;
    }
    return false;
}

bool agent_mesh_is_online(AgentMesh *mesh) {
    return mesh && (mesh->listen_fd >= 0 || mesh->is_client || mesh->running);
}

void agent_mesh_stop(AgentMesh *mesh) {
    if (!mesh) return;

    mesh->running = false;
    if (mesh->listen_fd >= 0) {
        close(mesh->listen_fd);
        mesh->listen_fd = -1;
    }
    if (mesh->listener_started) {
        pthread_join(mesh->listener_thread, NULL);
        mesh->listener_started = false;
    }
    if (mesh->is_server) {
        unlink(mesh->socket_path);
    }
}

void agent_mesh_free(AgentMesh *mesh) {
    if (!mesh) return;
    agent_mesh_stop(mesh);
    pthread_mutex_destroy(&mesh->lock);
    free(mesh);
}

const char *agent_mesh_get_socket_path(const AgentMesh *mesh) {
    return mesh ? mesh->socket_path : AGENT_MESH_DEFAULT_SOCK_PATH;
}
