/* mcp_http.c - Stateless MCP 2026-07-28 client over Streamable HTTP.
 *
 * Upstream reference: modelcontextprotocol.io/specification/2026-07-28
 *   - no initialize handshake, no sessions
 *   - every request carries protocol version + capabilities in params._meta
 *   - MCP-Protocol-Version, Mcp-Method and (for tools/call) Mcp-Name headers
 *   - responses are JSON or text/event-stream
 */
#define _POSIX_C_SOURCE 200809L

#include "common.h"
#include "mcp_client.h"
#include "mcp_http.h"

#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MCP_HTTP_DEFAULT_TIMEOUT_MS 15000L

typedef struct {
    char  *data;
    size_t len;
} HttpBuf;

static size_t http_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    HttpBuf *b = (HttpBuf *)userdata;
    size_t n = size * nmemb;
    char *p = realloc(b->data, b->len + n + 1);
    if (!p) return 0;
    b->data = p;
    memcpy(b->data + b->len, ptr, n);
    b->len += n;
    b->data[b->len] = '\0';
    return n;
}

static JsonValue *parse_json_slice(const char *s, size_t n) {
    if (!s || n == 0) return NULL;
    char *tmp = malloc(n + 1);
    if (!tmp) return NULL;
    memcpy(tmp, s, n);
    tmp[n] = '\0';
    JsonValue *v = json_parse(tmp);
    free(tmp);
    return v;
}

static JsonValue *clone_object(JsonValue *params) {
    if (params) {
        char *s = json_serialize(params);
        JsonValue *c = s ? json_parse(s) : NULL;
        free(s);
        if (c && c->type == JSON_OBJECT) return c;
        if (c) json_free(c);
    }
    return json_create_object();
}

char *mcp_http_build_body(const char *method, JsonValue *params, int id) {
    JsonValue *req = json_create_object();
    if (!req) return NULL;

    json_obj_add(req, "jsonrpc", json_create_string("2.0"));
    json_obj_add(req, "id", json_create_number((double)id));
    json_obj_add(req, "method", json_create_string(method ? method : ""));

    JsonValue *p = clone_object(params);
    JsonValue *meta = json_create_object();
    json_obj_add(meta, "io.modelcontextprotocol/protocolVersion",
                 json_create_string(MCP_HTTP_PROTOCOL_VERSION));
    JsonValue *ci = json_create_object();
    json_obj_add(ci, "name", json_create_string("belya"));
    json_obj_add(ci, "version", json_create_string("2.1.0"));
    json_obj_add(meta, "io.modelcontextprotocol/clientInfo", ci);
    json_obj_add(meta, "io.modelcontextprotocol/clientCapabilities", json_create_object());
    json_obj_add(p, "_meta", meta);
    json_obj_add(req, "params", p);

    char *out = json_serialize(req);
    json_free(req);
    return out;
}

JsonValue *mcp_http_parse_body(const char *body) {
    if (!body || !*body) return NULL;

    if (!strstr(body, "data:")) {
        return parse_json_slice(body, strlen(body));
    }

    DynString acc = dyn_str_new();
    if (!acc.data) return NULL;
    JsonValue *last = NULL;
    const char *p = body;

    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        if (len > 0 && p[len - 1] == '\r') len--;

        if (len >= 5 && strncmp(p, "data:", 5) == 0) {
            const char *d = p + 5;
            size_t dl = len - 5;
            while (dl > 0 && (*d == ' ' || *d == '\t')) { d++; dl--; }
            if (acc.len > 0) dyn_str_append(&acc, "\n");
            char *seg = malloc(dl + 1);
            if (seg) {
                memcpy(seg, d, dl);
                seg[dl] = '\0';
                dyn_str_append(&acc, seg);
                free(seg);
            }
        } else if (len == 0 && acc.len > 0) {
            JsonValue *v = parse_json_slice(acc.data, acc.len);
            if (v) { if (last) json_free(last); last = v; }
            acc.len = 0;
            if (acc.data) acc.data[0] = '\0';
        }

        if (!eol) break;
        p = eol + 1;
    }

    if (acc.len > 0) {
        JsonValue *v = parse_json_slice(acc.data, acc.len);
        if (v) { if (last) json_free(last); last = v; }
    }

    dyn_str_free(&acc);
    return last;
}

JsonValue *mcp_http_error_for_status(long status, const char *body) {
    JsonValue *root = json_create_object();
    if (!root) return NULL;
    JsonValue *err = json_create_object();
    int code = (status == 401) ? -32001 : (status == 403) ? -32003 : -32603;
    const char *label = (status == 401) ? "Unauthorized"
                      : (status == 403) ? "Forbidden"
                      : "HTTP error";
    json_obj_add(err, "code", json_create_number((double)code));
    char msg[600];
    snprintf(msg, sizeof(msg), "HTTP %ld %s: %.400s", status, label, body ? body : "");
    json_obj_add(err, "message", json_create_string(msg));
    json_obj_add(root, "error", err);
    return root;
}

MCPClient *mcp_client_start_http(const char *url) {
    if (!url) return NULL;
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) return NULL;

    MCPClient *c = calloc(1, sizeof(MCPClient));
    if (!c) return NULL;

    c->transport = MCP_TRANSPORT_HTTP;
    c->url = xstrdup(url);
    c->request_id = 1;
    c->connected = true;
    c->stdin_fd = -1;
    c->stdout_fd = -1;

    const char *tok = getenv("MCP_HTTP_TOKEN");
    if (tok && *tok) c->bearer_token = xstrdup(tok);

    const char *t = getenv("MCP_HTTP_TIMEOUT_MS");
    c->timeout_ms = (t && *t) ? atol(t) : MCP_HTTP_DEFAULT_TIMEOUT_MS;
    if (c->timeout_ms <= 0) c->timeout_ms = MCP_HTTP_DEFAULT_TIMEOUT_MS;

    return c;
}

JsonValue *mcp_http_request(MCPClient *client, const char *method, JsonValue *params) {
    if (!client || !client->url) return NULL;

    char *body = mcp_http_build_body(method, params, client->request_id++);
    if (!body) return NULL;

    CURL *curl = curl_easy_init();
    if (!curl) { free(body); return NULL; }

    HttpBuf buf = {0};
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json, text/event-stream");
    headers = curl_slist_append(headers, "MCP-Protocol-Version: " MCP_HTTP_PROTOCOL_VERSION);

    char mh[192];
    snprintf(mh, sizeof(mh), "Mcp-Method: %.120s", method ? method : "");
    headers = curl_slist_append(headers, mh);

    /* Mcp-Name is required for tools/call (params.name), resources/read and prompts/get. */
    const char *tool_name = params ? json_obj_get_str(params, "name") : NULL;
    if (tool_name && *tool_name) {
        char nh[512];
        snprintf(nh, sizeof(nh), "Mcp-Name: %.400s", tool_name);
        headers = curl_slist_append(headers, nh);
    }

    if (client->bearer_token && *client->bearer_token) {
        char ah[512];
        snprintf(ah, sizeof(ah), "Authorization: Bearer %.400s", client->bearer_token);
        headers = curl_slist_append(headers, ah);
    }

    curl_easy_setopt(curl, CURLOPT_URL, client->url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, client->timeout_ms);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 3000L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "belya-mcp-http/1.0");

    CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    JsonValue *out = NULL;
    if (rc != CURLE_OK) {
        JsonValue *root = json_create_object();
        JsonValue *err = json_create_object();
        json_obj_add(err, "code", json_create_number(-32603));
        char msg[400];
        snprintf(msg, sizeof(msg), "MCP HTTP transport error: %s",
                 curl_easy_strerror(rc));
        json_obj_add(err, "message", json_create_string(msg));
        json_obj_add(root, "error", err);
        out = root;
    } else if (status == 401 || status == 403 || status >= 400) {
        out = mcp_http_error_for_status(status, buf.data);
    } else {
        out = mcp_http_parse_body(buf.data);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(buf.data);
    free(body);
    return out;
}
