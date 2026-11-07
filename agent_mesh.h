#ifndef AGENT_MESH_H
#define AGENT_MESH_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AGENT_MESH_DEFAULT_SOCK_PATH "/tmp/belya_almaz_mesh.sock"

typedef struct {
    char source[64];
    char target[64];
    char action[64];
    char *payload_json;
    int request_id;
    bool is_response;
} AgentMeshMessage;

typedef struct AgentMesh AgentMesh;

// Callback signature for handling incoming mesh messages
typedef char *(*AgentMeshHandlerFn)(AgentMesh *mesh, const AgentMeshMessage *msg, void *userdata);

// Initialize Agent Mesh node ('belya' or 'almaz')
AgentMesh *agent_mesh_init(const char *agent_name, const char *socket_path);

// Register message callback handler
void agent_mesh_set_handler(AgentMesh *mesh, AgentMeshHandlerFn handler, void *userdata);

// Start non-blocking listener daemon
bool agent_mesh_start(AgentMesh *mesh);

// Send message to peer agent over mesh socket
bool agent_mesh_send(AgentMesh *mesh, const char *target, const char *action, const char *payload_json, int *out_req_id);

// Synchronous request-response pair-programming consultation with peer
char *agent_mesh_consult(AgentMesh *mesh, const char *target, const char *action, const char *query_json, int timeout_ms);

// Ping peer agent and measure roundtrip time
bool agent_mesh_ping(AgentMesh *mesh, const char *target, double *out_rtt_ms);

// Check if socket is active and connected
bool agent_mesh_is_online(AgentMesh *mesh);

// Teardown and clean shutdown
void agent_mesh_stop(AgentMesh *mesh);
void agent_mesh_free(AgentMesh *mesh);

#ifdef __cplusplus
}
#endif

#endif // AGENT_MESH_H
