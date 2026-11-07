#ifndef LSP_CLIENT_H
#define LSP_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LSP_SERVER_NONE = 0,
    LSP_SERVER_CLANGD,         // C/C++ (clangd)
    LSP_SERVER_PYRIGHT,        // Python (pyright-langserver / pyright)
    LSP_SERVER_RUST_ANALYZER,  // Rust (rust-analyzer)
    LSP_SERVER_GOPLS,          // Go (gopls)
    LSP_SERVER_TYPESCRIPT,     // JS/TS (typescript-language-server)
    LSP_SERVER_CUSTOM
} LspServerType;

typedef struct {
    pid_t pid;
    int stdin_fd;
    int stdout_fd;
    char root_dir[1024];
    int next_request_id;
    bool initialized;
    LspServerType server_type;
} LspClient;

// Auto-detect which LSP server type is suited for a given source file extension
LspServerType lsp_detect_server_for_file(const char *file_path);

// Get default binary/command name for a server type
const char *lsp_server_command(LspServerType type);

// Spawn a new stdio LSP language server child process
LspClient *lsp_client_spawn(LspServerType type, const char *root_dir);

// Perform LSP 'initialize' handshake and send 'initialized' notification
bool lsp_client_initialize(LspClient *client);

// Notify language server of open file
bool lsp_client_did_open(LspClient *client, const char *file_path, const char *content);

// Query diagnostics for a document. Returns structured JSON string (caller must free), or NULL if none.
char *lsp_client_get_diagnostics(LspClient *client, const char *file_path, int timeout_ms);

// Query definition location (file, line, character)
char *lsp_client_definition(LspClient *client, const char *file_path, int line, int character, int timeout_ms);

// Query symbol references
char *lsp_client_references(LspClient *client, const char *file_path, int line, int character, int timeout_ms);

// Clean shutdown and termination
void lsp_client_shutdown(LspClient *client);
void lsp_client_free(LspClient *client);

// High-level Pre-Flight Compiler Watchdog hook:
// Analyzes file with stdio LSP if server is installed; returns structured compiler diagnostic or NULL.
char *lsp_preflight_diagnostics(const char *file_path);

#ifdef __cplusplus
}
#endif

#endif // LSP_CLIENT_H
