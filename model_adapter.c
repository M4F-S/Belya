#include "model_adapter.h"
#include <curl/curl.h>
#include <unistd.h>

typedef struct {
    ModelGateway *gw;
    DynString raw_body;
    DynString line_buffer;
    DynString accumulated_content;
    DynString accumulated_reasoning;
    ModelParsedToolCall *tool_calls;
    DynString *tool_args;
    size_t tool_call_count;
    size_t tool_call_cap;
    bool is_sse_stream;
    bool received_done;
} StreamContext;

static void stream_ctx_init(StreamContext *ctx, ModelGateway *gw) {
    memset(ctx, 0, sizeof(StreamContext));
    ctx->gw = gw;
    ctx->raw_body = dyn_str_new();
    ctx->line_buffer = dyn_str_new();
    ctx->accumulated_content = dyn_str_new();
    ctx->accumulated_reasoning = dyn_str_new();
}

static void stream_ctx_free(StreamContext *ctx) {
    dyn_str_free(&ctx->raw_body);
    dyn_str_free(&ctx->line_buffer);
    dyn_str_free(&ctx->accumulated_content);
    dyn_str_free(&ctx->accumulated_reasoning);
    for (size_t i = 0; i < ctx->tool_call_count; i++) {
        dyn_str_free(&ctx->tool_args[i]);
    }
    if (ctx->tool_args) free(ctx->tool_args);
    if (ctx->tool_calls) free(ctx->tool_calls);
}

static void stream_process_sse_line(StreamContext *ctx, const char *line) {
    while (*line == ' ') line++;
    if (strncmp(line, "data:", 5) != 0) return;
    const char *data_str = line + 5;
    while (*data_str == ' ') data_str++;
    if (!*data_str) return;

    if (strncmp(data_str, "[DONE]", 6) == 0) {
        ctx->received_done = true;
        return;
    }

    JsonValue *root = json_parse(data_str);
    if (!root) return;

    ctx->is_sse_stream = true;

    JsonValue *choices = json_obj_get(root, "choices");
    if (choices && choices->type == JSON_ARRAY && choices->u.array.count > 0) {
        JsonValue *choice = choices->u.array.items[0];
        JsonValue *delta = json_obj_get(choice, "delta");
        if (delta) {
            // Text delta
            const char *c_str = json_obj_get_str(delta, "content");
            if (c_str && strlen(c_str) > 0) {
                dyn_str_append(&ctx->accumulated_content, c_str);
                if (ctx->gw->stream_callback) {
                    ctx->gw->stream_callback(c_str, false, ctx->gw->stream_userdata);
                }
            }

            // Reasoning delta
            const char *r_str = json_obj_get_str(delta, "reasoning_content");
            if (!r_str) r_str = json_obj_get_str(delta, "thinking");
            if (r_str && strlen(r_str) > 0) {
                dyn_str_append(&ctx->accumulated_reasoning, r_str);
                if (ctx->gw->stream_callback) {
                    ctx->gw->stream_callback(r_str, true, ctx->gw->stream_userdata);
                }
            }

            // Tool call deltas
            JsonValue *tc_arr = json_obj_get(delta, "tool_calls");
            if (tc_arr && tc_arr->type == JSON_ARRAY) {
                for (size_t k = 0; k < tc_arr->u.array.count; k++) {
                    JsonValue *tc_item = tc_arr->u.array.items[k];
                    size_t idx = (size_t)json_obj_get_num(tc_item, "index", (double)ctx->tool_call_count);
                    if (idx >= ctx->tool_call_count) {
                        size_t needed = idx + 1;
                        if (needed > ctx->tool_call_cap) {
                            size_t new_cap = ctx->tool_call_cap == 0 ? 4 : ctx->tool_call_cap * 2;
                            while (needed > new_cap) new_cap *= 2;
                            ctx->tool_calls = realloc(ctx->tool_calls, sizeof(ModelParsedToolCall) * new_cap);
                            ctx->tool_args = realloc(ctx->tool_args, sizeof(DynString) * new_cap);
                            for (size_t j = ctx->tool_call_cap; j < new_cap; j++) {
                                memset(&ctx->tool_calls[j], 0, sizeof(ModelParsedToolCall));
                                ctx->tool_args[j] = dyn_str_new();
                            }
                            ctx->tool_call_cap = new_cap;
                        }
                        ctx->tool_call_count = needed;
                    }

                    const char *id_str = json_obj_get_str(tc_item, "id");
                    if (id_str && !ctx->tool_calls[idx].id) {
                        ctx->tool_calls[idx].id = strdup(id_str);
                    }

                    JsonValue *fn = json_obj_get(tc_item, "function");
                    if (fn) {
                        const char *name_str = json_obj_get_str(fn, "name");
                        if (name_str && !ctx->tool_calls[idx].name) {
                            ctx->tool_calls[idx].name = strdup(name_str);
                        }
                        const char *arg_chunk = json_obj_get_str(fn, "arguments");
                        if (arg_chunk && strlen(arg_chunk) > 0) {
                            dyn_str_append(&ctx->tool_args[idx], arg_chunk);
                        }
                    }
                }
            }
        }
    }

    json_free(root);
}

static size_t curl_sink_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total = size * nmemb;
    StreamContext *ctx = (StreamContext *)userdata;
    dyn_str_append_len(&ctx->raw_body, (const char *)ptr, total);

    if (ctx->gw->enable_streaming) {
        dyn_str_append_len(&ctx->line_buffer, (const char *)ptr, total);
        char *start = ctx->line_buffer.data;
        char *nl;
        while ((nl = strchr(start, '\n')) != NULL) {
            *nl = '\0';
            stream_process_sse_line(ctx, start);
            start = nl + 1;
        }
        if (start != ctx->line_buffer.data) {
            size_t remaining = strlen(start);
            memmove(ctx->line_buffer.data, start, remaining);
            ctx->line_buffer.data[remaining] = '\0';
            ctx->line_buffer.len = remaining;
        }
    }
    return total;
}

static ModelGatewayResponse openai_chat_complete(ModelGateway *self, const JsonValue *messages_json, const JsonValue *tools_schema) {
    ModelGatewayResponse res = {0};

    JsonValue *payload = json_create_object();
    json_obj_add(payload, "model", json_create_string(self->model));
    json_obj_add(payload, "messages", (JsonValue *)messages_json); // Shared reference
    if (self->enable_streaming) {
        json_obj_add(payload, "stream", json_create_bool(true));
    }
    if (tools_schema && tools_schema->type == JSON_ARRAY && tools_schema->u.array.count > 0) {
        json_obj_add(payload, "tools", (JsonValue *)tools_schema);
        json_obj_add(payload, "tool_choice", json_create_string("auto"));
    }

    char *json_body = json_serialize(payload);

    // Detach shared references safely by key before freeing root wrapper
    for (size_t i = 0; i < payload->u.object.count; i++) {
        if (payload->u.object.members[i].key &&
            (strcmp(payload->u.object.members[i].key, "messages") == 0 ||
             strcmp(payload->u.object.members[i].key, "tools") == 0)) {
            payload->u.object.members[i].value = NULL;
        }
    }
    json_free(payload);

    int max_attempts = self->max_retries > 0 ? self->max_retries : 1;
    unsigned int sleep_sec = 1;

    for (int attempt = 0; attempt < max_attempts; attempt++) {
        CURL *curl = curl_easy_init();
        if (!curl) {
            res.content = strdup("System Error: Failed to initialize libcurl.");
            free(json_body);
            return res;
        }

        StreamContext stream_ctx;
        stream_ctx_init(&stream_ctx, self);

        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        if (self->enable_streaming) {
            headers = curl_slist_append(headers, "Accept: text/event-stream");
        }

        char auth_header[512];
        if (self->api_key && strlen(self->api_key) > 0 && strcmp(self->api_key, "none") != 0) {
            snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", self->api_key);
            headers = curl_slist_append(headers, auth_header);
        }

        long timeout = self->timeout_sec > 0 ? (long)self->timeout_sec : 120L;
        curl_easy_setopt(curl, CURLOPT_URL, self->endpoint);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_sink_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &stream_ctx);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);

        CURLcode code = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (code != CURLE_OK) {
            if (attempt < max_attempts - 1 && (code == CURLE_OPERATION_TIMEDOUT || code == CURLE_COULDNT_CONNECT)) {
                stream_ctx_free(&stream_ctx);
                sleep(sleep_sec);
                sleep_sec *= 2;
                continue;
            }
            DynString err_ds = dyn_str_new();
            dyn_str_appendf(&err_ds, "Network Error: %s", curl_easy_strerror(code));
            res.content = err_ds.data;
            stream_ctx_free(&stream_ctx);
            free(json_body);
            return res;
        }

        // Retry on 429 or 5xx
        if ((http_code == 429 || http_code >= 500) && attempt < max_attempts - 1) {
            stream_ctx_free(&stream_ctx);
            sleep(sleep_sec);
            sleep_sec *= 2;
            continue;
        }

        // Check if stream mode captured data
        if (stream_ctx.is_sse_stream) {
            if (stream_ctx.accumulated_content.len > 0) {
                res.content = strdup(stream_ctx.accumulated_content.data);
            }
            if (stream_ctx.accumulated_reasoning.len > 0) {
                res.reasoning_content = strdup(stream_ctx.accumulated_reasoning.data);
            }
            if (stream_ctx.tool_call_count > 0) {
                res.has_tool_call = true;
                res.tool_call_count = stream_ctx.tool_call_count;
                res.tool_calls = calloc(res.tool_call_count, sizeof(ModelParsedToolCall));
                for (size_t i = 0; i < res.tool_call_count; i++) {
                    res.tool_calls[i].id = stream_ctx.tool_calls[i].id ? strdup(stream_ctx.tool_calls[i].id) : strdup("call_default");
                    res.tool_calls[i].name = stream_ctx.tool_calls[i].name ? strdup(stream_ctx.tool_calls[i].name) : strdup("unknown");
                    res.tool_calls[i].arguments_json = stream_ctx.tool_args[i].len > 0 ? strdup(stream_ctx.tool_args[i].data) : strdup("{}");
                    if (stream_ctx.tool_calls[i].id) free(stream_ctx.tool_calls[i].id);
                    if (stream_ctx.tool_calls[i].name) free(stream_ctx.tool_calls[i].name);
                }
            }
            if (!res.content && !res.has_tool_call) {
                res.content = strdup("Completed streaming response.");
            }
            stream_ctx_free(&stream_ctx);
            free(json_body);
            return res;
        }

        // Fallback: Non-streaming / error JSON parsing
        JsonValue *root = json_parse(stream_ctx.raw_body.data);
        if (!root) {
            DynString err_ds = dyn_str_new();
            dyn_str_appendf(&err_ds, "Error: Failed to parse upstream model JSON (HTTP %ld).", http_code);
            res.content = err_ds.data;
            stream_ctx_free(&stream_ctx);
            free(json_body);
            return res;
        }

        // Check for API Error object
        JsonValue *err_obj = json_obj_get(root, "error");
        if (err_obj) {
            const char *err_msg = json_obj_get_str(err_obj, "message");
            if (!err_msg) err_msg = json_obj_get_str(root, "message");
            DynString err_ds = dyn_str_new();
            dyn_str_appendf(&err_ds, "API Error (HTTP %ld): %s", http_code, err_msg ? err_msg : "Unknown error");
            res.content = err_ds.data;
            json_free(root);
            stream_ctx_free(&stream_ctx);
            free(json_body);
            return res;
        }

        JsonValue *choices = json_obj_get(root, "choices");
        if (choices && choices->type == JSON_ARRAY && choices->u.array.count > 0) {
            JsonValue *choice = choices->u.array.items[0];
            JsonValue *message = json_obj_get(choice, "message");
            if (message) {
                const char *content_str = json_obj_get_str(message, "content");
                if (content_str) res.content = strdup(content_str);

                const char *reasoning = json_obj_get_str(message, "reasoning_content");
                if (!reasoning) reasoning = json_obj_get_str(message, "thinking");
                if (reasoning && strlen(reasoning) > 0) {
                    res.reasoning_content = strdup(reasoning);
                }

                JsonValue *tc_arr = json_obj_get(message, "tool_calls");
                if (tc_arr && tc_arr->type == JSON_ARRAY && tc_arr->u.array.count > 0) {
                    res.has_tool_call = true;
                    res.tool_call_count = tc_arr->u.array.count;
                    res.tool_calls = calloc(res.tool_call_count, sizeof(ModelParsedToolCall));
                    for (size_t i = 0; i < res.tool_call_count; i++) {
                        JsonValue *tc_item = tc_arr->u.array.items[i];
                        const char *t_id = json_obj_get_str(tc_item, "id");
                        JsonValue *fn = json_obj_get(tc_item, "function");
                        const char *f_name = fn ? json_obj_get_str(fn, "name") : "unknown";
                        const char *f_args = fn ? json_obj_get_str(fn, "arguments") : "{}";

                        res.tool_calls[i].id = strdup(t_id ? t_id : "call_default");
                        res.tool_calls[i].name = strdup(f_name ? f_name : "");
                        res.tool_calls[i].arguments_json = strdup(f_args ? f_args : "{}");
                    }
                }
            }
        }

        if (!res.content && !res.has_tool_call) {
            DynString empty_ds = dyn_str_new();
            dyn_str_appendf(&empty_ds, "Empty model response (HTTP %ld).", http_code);
            res.content = empty_ds.data;
        }

        json_free(root);
        stream_ctx_free(&stream_ctx);
        free(json_body);
        return res;
    }

    free(json_body);
    res.content = strdup("Error: Maximum retry attempts exceeded.");
    return res;
}

ModelGateway *model_gateway_init(const char *endpoint, const char *api_key, const char *model) {
    ModelGateway *gw = calloc(1, sizeof(ModelGateway));
    gw->endpoint = strdup(endpoint);
    gw->api_key = strdup(api_key ? api_key : "");
    gw->model = strdup(model ? model : "hermes-3");
    gw->timeout_sec = 120;
    gw->max_retries = 3;
    gw->enable_streaming = true;
    gw->chat_complete = openai_chat_complete;
    return gw;
}

void model_gateway_set_streaming(ModelGateway *gw, bool enable, TokenStreamCallback cb, void *userdata) {
    if (!gw) return;
    gw->enable_streaming = enable;
    gw->stream_callback = cb;
    gw->stream_userdata = userdata;
}

void model_gateway_free(ModelGateway *gw) {
    if (!gw) return;
    free(gw->endpoint);
    free(gw->api_key);
    free(gw->model);
    free(gw);
}

void model_gateway_response_free(ModelGatewayResponse *resp) {
    if (!resp) return;
    if (resp->content) free(resp->content);
    if (resp->reasoning_content) free(resp->reasoning_content);
    for (size_t i = 0; i < resp->tool_call_count; i++) {
        free(resp->tool_calls[i].id);
        free(resp->tool_calls[i].name);
        free(resp->tool_calls[i].arguments_json);
    }
    if (resp->tool_calls) free(resp->tool_calls);
    memset(resp, 0, sizeof(ModelGatewayResponse));
}
