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
#include <sys/time.h>

struct AgentMesh {
    char agent_name[64];
    char socket_path[256];
    bool is_server;
    int listen_fd;
    int peer_fd;
    volatile bool running;
    pthread_t listener_thread;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int next_msg_id;
    AgentMeshHandlerFn handler;
    void *userdata;

    // Pending response matching
    int pending_req_id;
    char *pending_response;
    bool pending_arrived;
};

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

    if (is_resp) {
        // Matched pending response
        pthread_mutex_lock(&mesh->lock);
        if (msg_id == mesh->pending_req_id) {
            if (mesh->pending_response) free(mesh->pending_response);
            mesh->pending_response = payload_str ? strdup(payload_str) : strdup("{}");
            mesh->pending_arrived = true;
            pthread_cond_broadcast(&mesh->cond);
        }
        pthread_mutex_unlock(&mesh->lock);
    } else {
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
            strncpy(msg.source, src ? src : "", sizeof(msg.source) - 1);
            strncpy(msg.target, dst ? dst : "", sizeof(msg.target) - 1);
            strncpy(msg.action, action ? action : "", sizeof(msg.action) - 1);
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
            json_obj_add(resp_jv, "payload", json_create_string(resp_payload ? resp_payload : "ok"));
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

    while (mesh->running) {
        struct pollfd pfd;
        pfd.fd = mesh->listen_fd;
        pfd.events = POLLIN;

        int prc = poll(&pfd, 1, 200);
        if (prc <= 0) continue;

        int conn = accept(mesh->listen_fd, NULL, NULL);
        if (conn < 0) continue;

        pthread_mutex_lock(&mesh->lock);
        if (mesh->peer_fd >= 0) close(mesh->peer_fd);
        mesh->peer_fd = conn;
        pthread_mutex_unlock(&mesh->lock);

        char buf[4096];
        size_t buf_pos = 0;

        while (mesh->running) {
            struct pollfd cpfd;
            cpfd.fd = conn;
            cpfd.events = POLLIN;
            int cprc = poll(&cpfd, 1, 200);
            if (cprc < 0) break;
            if (cprc == 0) continue;

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
        pthread_mutex_lock(&mesh->lock);
        if (mesh->peer_fd == conn) mesh->peer_fd = -1;
        pthread_mutex_unlock(&mesh->lock);
    }

    return NULL;
}

static void *mesh_client_worker(void *arg) {
    AgentMesh *mesh = (AgentMesh *)arg;
    char buf[4096];
    size_t buf_pos = 0;

    while (mesh->running && mesh->peer_fd >= 0) {
        struct pollfd pfd;
        pfd.fd = mesh->peer_fd;
        pfd.events = POLLIN;

        int prc = poll(&pfd, 1, 200);
        if (prc < 0) break;
        if (prc == 0) continue;

        ssize_t nr = read(mesh->peer_fd, buf + buf_pos, sizeof(buf) - buf_pos - 1);
        if (nr <= 0) break;
        buf_pos += (size_t)nr;
        buf[buf_pos] = '\0';

        char *line_start = buf;
        char *newline = NULL;
        while ((newline = strchr(line_start, '\n')) != NULL) {
            *newline = '\0';
            mesh_handle_incoming_line(mesh, mesh->peer_fd, line_start);
            line_start = newline + 1;
        }

        if (line_start > buf) {
            size_t remaining = buf_pos - (size_t)(line_start - buf);
            memmove(buf, line_start, remaining);
            buf_pos = remaining;
            buf[buf_pos] = '\0';
        }
    }

    return NULL;
}

AgentMesh *agent_mesh_init(const char *agent_name, const char *socket_path) {
    AgentMesh *mesh = (AgentMesh *)calloc(1, sizeof(AgentMesh));
    if (!mesh) return NULL;

    strncpy(mesh->agent_name, agent_name ? agent_name : "agent", sizeof(mesh->agent_name) - 1);
    if (socket_path && strlen(socket_path) > 0) {
        strncpy(mesh->socket_path, socket_path, sizeof(mesh->socket_path) - 1);
    } else {
        strncpy(mesh->socket_path, AGENT_MESH_DEFAULT_SOCK_PATH, sizeof(mesh->socket_path) - 1);
    }

    mesh->listen_fd = -1;
    mesh->peer_fd = -1;
    mesh->next_msg_id = 1;
    pthread_mutex_init(&mesh->lock, NULL);
    pthread_cond_init(&mesh->cond, NULL);

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

    // Check if socket already exists and connect as client
    int cli_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", mesh->socket_path);

    if (cli_fd >= 0 && connect(cli_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        // Successfully connected to existing mesh server as client
        mesh->is_server = false;
        mesh->peer_fd = cli_fd;
        mesh->running = true;
        pthread_create(&mesh->listener_thread, NULL, mesh_client_worker, mesh);
        return true;
    }
    if (cli_fd >= 0) close(cli_fd);

    // Stale socket or not running: act as server
    unlink(mesh->socket_path);

    int srv_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (srv_fd < 0) return false;

    int opt = 1;
    setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(srv_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(srv_fd);
        return false;
    }

    if (listen(srv_fd, 5) != 0) {
        close(srv_fd);
        unlink(mesh->socket_path);
        return false;
    }

    mesh->is_server = true;
    mesh->listen_fd = srv_fd;
    mesh->running = true;
    pthread_create(&mesh->listener_thread, NULL, mesh_server_worker, mesh);

    return true;
}

bool agent_mesh_send(AgentMesh *mesh, const char *target, const char *action, const char *payload_json, int *out_req_id) {
    if (!mesh || mesh->peer_fd < 0) return false;

    pthread_mutex_lock(&mesh->lock);
    int req_id = mesh->next_msg_id++;
    if (out_req_id) *out_req_id = req_id;

    JsonValue *jv = json_create_object();
    json_obj_add(jv, "mesh", json_create_string("1.0"));
    json_obj_add(jv, "id", json_create_number(req_id));
    json_obj_add(jv, "src", json_create_string(mesh->agent_name));
    json_obj_add(jv, "dst", json_create_string(target ? target : "peer"));
    json_obj_add(jv, "action", json_create_string(action ? action : "message"));
    json_obj_add(jv, "resp", json_create_bool(false));

    if (payload_json && strlen(payload_json) > 0) {
        JsonValue *parsed = json_parse(payload_json);
        if (parsed) {
            json_obj_add(jv, "payload", parsed);
        } else {
            json_obj_add(jv, "payload", json_create_string(payload_json));
        }
    } else {
        json_obj_add(jv, "payload", json_create_object());
    }

    char *msg_str = json_serialize(jv);
    json_free(jv);

    bool ok = false;
    if (msg_str) {
        ok = mesh_write_all(mesh->peer_fd, msg_str, strlen(msg_str)) &&
             mesh_write_all(mesh->peer_fd, "\n", 1);
        free(msg_str);
    }

    pthread_mutex_unlock(&mesh->lock);
    return ok;
}

char *agent_mesh_consult(AgentMesh *mesh, const char *target, const char *action, const char *query_json, int timeout_ms) {
    if (!mesh) return NULL;

    // Ensure connection
    if (mesh->peer_fd < 0 && !mesh->is_server) {
        int cli_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", mesh->socket_path);
        if (cli_fd >= 0 && connect(cli_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            mesh->peer_fd = cli_fd;
            mesh->running = true;
            pthread_create(&mesh->listener_thread, NULL, mesh_client_worker, mesh);
        } else {
            if (cli_fd >= 0) close(cli_fd);
        }
    }

    if (mesh->peer_fd < 0) return NULL;

    pthread_mutex_lock(&mesh->lock);
    int req_id = 0;
    mesh->pending_req_id = mesh->next_msg_id;
    mesh->pending_arrived = false;
    if (mesh->pending_response) {
        free(mesh->pending_response);
        mesh->pending_response = NULL;
    }
    pthread_mutex_unlock(&mesh->lock);

    if (!agent_mesh_send(mesh, target, action, query_json, &req_id)) {
        return NULL;
    }

    pthread_mutex_lock(&mesh->lock);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }

    while (!mesh->pending_arrived) {
        int rc = pthread_cond_timedwait(&mesh->cond, &mesh->lock, &ts);
        if (rc == ETIMEDOUT) break;
    }

    char *res = NULL;
    if (mesh->pending_arrived && mesh->pending_response) {
        res = strdup(mesh->pending_response);
        free(mesh->pending_response);
        mesh->pending_response = NULL;
    }
    mesh->pending_req_id = 0;
    mesh->pending_arrived = false;

    pthread_mutex_unlock(&mesh->lock);
    return res;
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
    return mesh && (mesh->peer_fd >= 0 || mesh->listen_fd >= 0);
}

void agent_mesh_stop(AgentMesh *mesh) {
    if (!mesh) return;

    mesh->running = false;
    if (mesh->peer_fd >= 0) {
        close(mesh->peer_fd);
        mesh->peer_fd = -1;
    }
    if (mesh->listen_fd >= 0) {
        close(mesh->listen_fd);
        mesh->listen_fd = -1;
    }
    pthread_join(mesh->listener_thread, NULL);
    if (mesh->is_server) {
        unlink(mesh->socket_path);
    }
}

void agent_mesh_free(AgentMesh *mesh) {
    if (!mesh) return;
    agent_mesh_stop(mesh);
    pthread_mutex_lock(&mesh->lock);
    if (mesh->pending_response) {
        free(mesh->pending_response);
        mesh->pending_response = NULL;
    }
    pthread_mutex_unlock(&mesh->lock);
    pthread_mutex_destroy(&mesh->lock);
    pthread_cond_destroy(&mesh->cond);
    free(mesh);
}
