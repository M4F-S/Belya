#ifndef MODEL_ADAPTER_H
#define MODEL_ADAPTER_H

#include "common.h"
#include "minijson.h"

typedef struct {
    char *id;
    char *name;
    char *arguments_json;
} ModelParsedToolCall;

typedef struct {
    char *content;
    char *reasoning_content;
    ModelParsedToolCall *tool_calls;
    size_t tool_call_count;
    bool has_tool_call;
    size_t prompt_tokens;
    size_t completion_tokens;
    size_t cached_tokens;
} ModelGatewayResponse;

typedef void (*TokenStreamCallback)(const char *token, bool is_reasoning, void *userdata);

typedef struct ModelGateway {
    char *endpoint;
    char *api_key;
    char *model;
    int timeout_sec;
    int max_retries;
    bool streaming;
    bool prompt_caching;
    TokenStreamCallback stream_cb;
    void *stream_userdata;
    void *curl_handle; // Persistent CURL handle for HTTP Keep-Alive
    ModelGatewayResponse (*chat_complete)(struct ModelGateway *self, const JsonValue *messages_json, const JsonValue *tools_schema);
} ModelGateway;

ModelGateway *model_gateway_init(const char *endpoint, const char *api_key, const char *model);
void model_gateway_free(ModelGateway *gw);
void model_gateway_response_free(ModelGatewayResponse *resp);

size_t model_gateway_scavenge_tool_calls(const char *content, const char *reasoning,
                                         const char *const *known_tool_names, size_t known_count,
                                         ModelParsedToolCall **out_calls);

#endif
