#include "model_adapter.h"
#include <curl/curl.h>
#include <unistd.h>

typedef struct {
    ModelGateway *gw;
    DynString line_buf;
    DynString accum_content;
    DynString accum_reasoning;
    DynString raw_fallback;
    ModelParsedToolCall *tool_calls;
    size_t tool_call_count;
    size_t tool_call_cap;
    bool has_tool_call;
    size_t prompt_tokens;
    size_t completion_tokens;
    size_t cached_tokens;
    bool printed_reasoning_header;
    bool printed_content_header;
} StreamContext;

static void stream_process_line(StreamContext *ctx, const char *line) {
    if (!line || strlen(line) == 0) return;
    if (strncmp(line, "data: [DONE]", 12) == 0) return;
    if (strncmp(line, "data: ", 6) != 0) return;

    const char *json_payload = line + 6;
    JsonValue *root = json_parse(json_payload);
    if (!root) return;

    JsonValue *usage = json_obj_get(root, "usage");
    if (usage) {
        ctx->prompt_tokens = (size_t)json_obj_get_num(usage, "prompt_tokens", (double)ctx->prompt_tokens);
        ctx->completion_tokens = (size_t)json_obj_get_num(usage, "completion_tokens", (double)ctx->completion_tokens);
        JsonValue *ptd = json_obj_get(usage, "prompt_tokens_details");
        if (ptd) {
            ctx->cached_tokens = (size_t)json_obj_get_num(ptd, "cached_tokens", (double)ctx->cached_tokens);
        }
    }

    JsonValue *choices = json_obj_get(root, "choices");
    if (choices && choices->type == JSON_ARRAY && choices->u.array.count > 0) {
        JsonValue *choice = choices->u.array.items[0];
        JsonValue *delta = json_obj_get(choice, "delta");
        if (delta) {
            // 1. Reasoning Tokens
            const char *reasoning = json_obj_get_str(delta, "reasoning_content");
            if (!reasoning) reasoning = json_obj_get_str(delta, "thinking");
            if (reasoning && strlen(reasoning) > 0) {
                dyn_str_append(&ctx->accum_reasoning, reasoning);
                if (ctx->gw->stream_cb) {
                    ctx->gw->stream_cb(reasoning, true, ctx->gw->stream_userdata);
                } else {
                    if (!ctx->printed_reasoning_header) {
                        printf("\n\033[0;36m[Reasoning]:\033[0m\n");
                        ctx->printed_reasoning_header = true;
                    }
                    printf("\033[0;36m%s\033[0m", reasoning);
                    fflush(stdout);
                }
            }

            // 2. Content Tokens
            const char *content = json_obj_get_str(delta, "content");
            if (content && strlen(content) > 0) {
                dyn_str_append(&ctx->accum_content, content);
                if (ctx->gw->stream_cb) {
                    ctx->gw->stream_cb(content, false, ctx->gw->stream_userdata);
                } else {
                    if (ctx->printed_reasoning_header && !ctx->printed_content_header) {
                        printf("\n\n\033[1;34m[C Agent]\033[0m\n");
                        ctx->printed_content_header = true;
                    }
                    printf("%s", content);
                    fflush(stdout);
                }
            }

            // 3. Tool Calls Delta
            JsonValue *tc_arr = json_obj_get(delta, "tool_calls");
            if (tc_arr && tc_arr->type == JSON_ARRAY) {
                ctx->has_tool_call = true;
                for (size_t i = 0; i < tc_arr->u.array.count; i++) {
                    JsonValue *tc_item = tc_arr->u.array.items[i];
                    size_t idx = (size_t)json_obj_get_num(tc_item, "index", (double)ctx->tool_call_count);
                    
                    if (idx >= ctx->tool_call_cap) {
                        size_t new_cap = idx + 4;
                        ctx->tool_calls = realloc(ctx->tool_calls, sizeof(ModelParsedToolCall) * new_cap);
                        for (size_t k = ctx->tool_call_cap; k < new_cap; k++) {
                            memset(&ctx->tool_calls[k], 0, sizeof(ModelParsedToolCall));
                        }
                        ctx->tool_call_cap = new_cap;
                    }
                    if (idx >= ctx->tool_call_count) {
                        ctx->tool_call_count = idx + 1;
                    }

                    const char *t_id = json_obj_get_str(tc_item, "id");
                    if (t_id) {
                        if (ctx->tool_calls[idx].id) free(ctx->tool_calls[idx].id);
                        ctx->tool_calls[idx].id = strdup(t_id);
                    } else if (!ctx->tool_calls[idx].id) {
                        ctx->tool_calls[idx].id = strdup("call_default");
                    }

                    JsonValue *fn = json_obj_get(tc_item, "function");
                    if (fn) {
                        const char *f_name = json_obj_get_str(fn, "name");
                        if (f_name) {
                            if (!ctx->tool_calls[idx].name) {
                                ctx->tool_calls[idx].name = strdup(f_name);
                            } else {
                                size_t nlen = strlen(ctx->tool_calls[idx].name) + strlen(f_name) + 1;
                                ctx->tool_calls[idx].name = realloc(ctx->tool_calls[idx].name, nlen);
                                strcat(ctx->tool_calls[idx].name, f_name);
                            }
                        }
                        const char *f_args = json_obj_get_str(fn, "arguments");
                        if (f_args) {
                            if (!ctx->tool_calls[idx].arguments_json) {
                                ctx->tool_calls[idx].arguments_json = strdup(f_args);
                            } else {
                                size_t alen = strlen(ctx->tool_calls[idx].arguments_json) + strlen(f_args) + 1;
                                ctx->tool_calls[idx].arguments_json = realloc(ctx->tool_calls[idx].arguments_json, alen);
                                strcat(ctx->tool_calls[idx].arguments_json, f_args);
                            }
                        }
                    }
                }
            }
        }
    }

    json_free(root);
}

static size_t curl_stream_sink_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total = size * nmemb;
    StreamContext *ctx = (StreamContext *)userdata;
    const char *p = (const char *)ptr;
    dyn_str_append_len(&ctx->raw_fallback, p, total);

    for (size_t i = 0; i < total; i++) {
        char c = p[i];
        if (c == '\n') {
            stream_process_line(ctx, ctx->line_buf.data);
            dyn_str_clear(&ctx->line_buf);
        } else if (c != '\r') {
            char tmp[2] = {c, '\0'};
            dyn_str_append(&ctx->line_buf, tmp);
        }
    }
    return total;
}

static size_t curl_sink_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total = size * nmemb;
    DynString *ds = (DynString *)userdata;
    dyn_str_append_len(ds, (const char *)ptr, total);
    return total;
}

static ModelGatewayResponse openai_chat_complete(ModelGateway *self, const JsonValue *messages_json, const JsonValue *tools_schema) {
    ModelGatewayResponse res = {0};

    JsonValue *payload = json_create_object();
    json_obj_add(payload, "model", json_create_string(self->model));
    json_obj_add(payload, "messages", (JsonValue *)messages_json); // Shared reference
    if (tools_schema && tools_schema->type == JSON_ARRAY && tools_schema->u.array.count > 0) {
        json_obj_add(payload, "tools", (JsonValue *)tools_schema);
        json_obj_add(payload, "tool_choice", json_create_string("auto"));
    }
    if (self->streaming) {
        json_obj_add(payload, "stream", json_create_bool(true));
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

        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        char auth_header[512];
        if (self->api_key && strlen(self->api_key) > 0 && strcmp(self->api_key, "none") != 0) {
            snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", self->api_key);
            headers = curl_slist_append(headers, auth_header);
        }

        if (self->prompt_caching) {
            // Only send anthropic prompt caching header for anthropic endpoints
            if (self->endpoint && strstr(self->endpoint, "anthropic") != NULL) {
                headers = curl_slist_append(headers, "anthropic-beta: prompt-caching-2024-07-31");
            }
        }

        long timeout = self->timeout_sec > 0 ? (long)self->timeout_sec : 120L;
        curl_easy_setopt(curl, CURLOPT_URL, self->endpoint);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);

        StreamContext stream_ctx;
        memset(&stream_ctx, 0, sizeof(StreamContext));
        stream_ctx.gw = self;
        stream_ctx.line_buf = dyn_str_new();
        stream_ctx.accum_content = dyn_str_new();
        stream_ctx.accum_reasoning = dyn_str_new();
        stream_ctx.raw_fallback = dyn_str_new();

        DynString non_stream_body = dyn_str_new();

        if (self->streaming) {
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_stream_sink_cb);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &stream_ctx);
        } else {
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_sink_cb);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &non_stream_body);
        }

        CURLcode code = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (code != CURLE_OK) {
            dyn_str_free(&stream_ctx.line_buf);
            dyn_str_free(&stream_ctx.accum_content);
            dyn_str_free(&stream_ctx.accum_reasoning);
            dyn_str_free(&stream_ctx.raw_fallback);
            dyn_str_free(&non_stream_body);

            if (attempt < max_attempts - 1 && (code == CURLE_OPERATION_TIMEDOUT || code == CURLE_COULDNT_CONNECT)) {
                sleep(sleep_sec);
                sleep_sec *= 2;
                continue;
            }
            DynString err_ds = dyn_str_new();
            dyn_str_appendf(&err_ds, "Network Error: %s", curl_easy_strerror(code));
            res.content = err_ds.data;
            free(json_body);
            return res;
        }

        if ((http_code == 429 || http_code >= 500) && attempt < max_attempts - 1) {
            dyn_str_free(&stream_ctx.line_buf);
            dyn_str_free(&stream_ctx.accum_content);
            dyn_str_free(&stream_ctx.accum_reasoning);
            dyn_str_free(&stream_ctx.raw_fallback);
            dyn_str_free(&non_stream_body);
            sleep(sleep_sec);
            sleep_sec *= 2;
            continue;
        }

        // Process Streaming Output
        if (self->streaming) {
            // Process any leftover trailing line
            if (stream_ctx.line_buf.len > 0) {
                stream_process_line(&stream_ctx, stream_ctx.line_buf.data);
            }
            dyn_str_free(&stream_ctx.line_buf);

            // Ensure non-null name and args on tool calls
            for (size_t k = 0; k < stream_ctx.tool_call_count; k++) {
                if (!stream_ctx.tool_calls[k].name) stream_ctx.tool_calls[k].name = strdup("");
                if (!stream_ctx.tool_calls[k].arguments_json) stream_ctx.tool_calls[k].arguments_json = strdup("{}");
            }

            res.content = stream_ctx.accum_content.len > 0 ? stream_ctx.accum_content.data : NULL;
            res.reasoning_content = stream_ctx.accum_reasoning.len > 0 ? stream_ctx.accum_reasoning.data : NULL;
            res.tool_calls = stream_ctx.tool_calls;
            res.tool_call_count = stream_ctx.tool_call_count;
            res.has_tool_call = stream_ctx.has_tool_call;
            res.prompt_tokens = stream_ctx.prompt_tokens;
            res.completion_tokens = stream_ctx.completion_tokens;
            res.cached_tokens = stream_ctx.cached_tokens;

            if (!res.content && !res.has_tool_call) {
                // If stream was empty, check if raw_fallback was an error json
                JsonValue *err_root = json_parse(stream_ctx.raw_fallback.data);
                if (err_root) {
                    JsonValue *err_obj = json_obj_get(err_root, "error");
                    if (err_obj) {
                        const char *m = json_obj_get_str(err_obj, "message");
                        DynString err_ds = dyn_str_new();
                        dyn_str_appendf(&err_ds, "API Error (HTTP %ld): %s", http_code, m ? m : "Unknown error");
                        res.content = err_ds.data;
                    }
                    json_free(err_root);
                }
                if (!res.content) {
                    DynString empty_ds = dyn_str_new();
                    dyn_str_appendf(&empty_ds, "Empty response from stream (HTTP %ld).", http_code);
                    res.content = empty_ds.data;
                }
            }
            dyn_str_free(&stream_ctx.raw_fallback);
            free(json_body);
            return res;
        }

        // Non-streaming processing
        JsonValue *root = json_parse(non_stream_body.data);
        dyn_str_free(&non_stream_body);
        if (!root) {
            res.content = strdup("Error: Failed to parse upstream model JSON response.");
            free(json_body);
            return res;
        }

        JsonValue *err_obj = json_obj_get(root, "error");
        if (err_obj) {
            const char *err_msg = json_obj_get_str(err_obj, "message");
            DynString err_ds = dyn_str_new();
            dyn_str_appendf(&err_ds, "API Error (HTTP %ld): %s", http_code, err_msg ? err_msg : "Unknown error");
            res.content = err_ds.data;
            json_free(root);
            free(json_body);
            return res;
        }

        JsonValue *usage = json_obj_get(root, "usage");
        if (usage) {
            res.prompt_tokens = (size_t)json_obj_get_num(usage, "prompt_tokens", 0);
            res.completion_tokens = (size_t)json_obj_get_num(usage, "completion_tokens", 0);
            JsonValue *ptd = json_obj_get(usage, "prompt_tokens_details");
            if (ptd) {
                res.cached_tokens = (size_t)json_obj_get_num(ptd, "cached_tokens", 0);
            }
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
        free(json_body);
        return res;
    }

    free(json_body);
    res.content = strdup("Error: Maximum retry attempts exceeded.");
    return res;
}

static bool is_known_tool(const char *name, const char *const *known, size_t count) {
    if (!name || strlen(name) == 0) return false;
    if (!known || count == 0) return true;
    for (size_t i = 0; i < count; i++) {
        if (known[i] && strcmp(known[i], name) == 0) return true;
    }
    return false;
}

static void try_add_scavenged_call(const char *json_str, const char *const *known_tools, size_t known_count,
                                   ModelParsedToolCall **out_calls, size_t *count, size_t *cap) {
    if (!json_str) return;
    JsonValue *root = json_parse(json_str);
    if (!root) return;

    if (root->type == JSON_OBJECT) {
        const char *tname = json_obj_get_str(root, "name");
        if (!tname) tname = json_obj_get_str(root, "tool");
        if (!tname) tname = json_obj_get_str(root, "action");

        if (tname && is_known_tool(tname, known_tools, known_count)) {
            JsonValue *args_val = json_obj_get(root, "arguments");
            if (!args_val) args_val = json_obj_get(root, "parameters");
            if (!args_val) args_val = json_obj_get(root, "args");
            if (!args_val) args_val = json_obj_get(root, "action_input");

            char *args_json = NULL;
            if (args_val) {
                if (args_val->type == JSON_STRING) {
                    args_json = strdup(args_val->u.string);
                } else {
                    args_json = json_serialize(args_val);
                }
            } else {
                args_json = strdup("{}");
            }

            if (*count >= *cap) {
                *cap = (*cap == 0) ? 4 : (*cap * 2);
                *out_calls = realloc(*out_calls, sizeof(ModelParsedToolCall) * (*cap));
            }

            char id_buf[64];
            snprintf(id_buf, sizeof(id_buf), "scavenged_%zu", *count + 1);
            (*out_calls)[*count].id = strdup(id_buf);
            (*out_calls)[*count].name = strdup(tname);
            (*out_calls)[*count].arguments_json = args_json ? args_json : strdup("{}");
            (*count)++;
        }
    }
    json_free(root);
}

size_t model_gateway_scavenge_tool_calls(const char *content, const char *reasoning,
                                         const char *const *known_tool_names, size_t known_count,
                                         ModelParsedToolCall **out_calls) {
    if (!out_calls) return 0;
    *out_calls = NULL;
    size_t count = 0;
    size_t cap = 0;

    const char *sources[2] = {reasoning, content};
    for (int s = 0; s < 2; s++) {
        const char *src = sources[s];
        if (!src || strlen(src) == 0) continue;
        size_t initial_count = count;

        // 1. Check for <tool_call> ... </tool_call> tags
        const char *p = src;
        while ((p = strstr(p, "<tool_call>")) != NULL) {
            p += strlen("<tool_call>");
            const char *end_tag = strstr(p, "</tool_call>");
            if (!end_tag) break;
            size_t tag_len = (size_t)(end_tag - p);
            char *sub = malloc(tag_len + 1);
            if (sub) {
                memcpy(sub, p, tag_len);
                sub[tag_len] = '\0';

                size_t offset = 0;
                char *json_obj_str = extract_json_object(sub, &offset);
                if (json_obj_str) {
                    try_add_scavenged_call(json_obj_str, known_tool_names, known_count, out_calls, &count, &cap);
                    free(json_obj_str);
                } else {
                    try_add_scavenged_call(sub, known_tool_names, known_count, out_calls, &count, &cap);
                }
                free(sub);
            }
            p = end_tag + strlen("</tool_call>");
        }

        // 2. If no <tool_call> tags were found in this source, scan for raw JSON objects
        if (count == initial_count) {
            p = src;
            while ((p = strchr(p, '{')) != NULL) {
                size_t next_off = 0;
                char *json_obj_str = extract_json_object(p, &next_off);
                if (json_obj_str && next_off > 0) {
                    size_t prev_count = count;
                    try_add_scavenged_call(json_obj_str, known_tool_names, known_count, out_calls, &count, &cap);
                    free(json_obj_str);
                    if (count > prev_count) {
                        p += next_off;
                        continue;
                    }
                }
                p++;
            }
        }
    }

    return count;
}

ModelGateway *model_gateway_init(const char *endpoint, const char *api_key, const char *model) {
    ModelGateway *gw = calloc(1, sizeof(ModelGateway));
    gw->endpoint = strdup(endpoint);
    gw->api_key = strdup(api_key ? api_key : "");
    gw->model = strdup(model ? model : "hermes-3");
    gw->timeout_sec = 120;
    gw->max_retries = 3;
    gw->streaming = true;
    const char *pc_env = getenv("PROMPT_CACHING");
    if (pc_env && (strcmp(pc_env, "true") == 0 || strcmp(pc_env, "1") == 0)) {
        gw->prompt_caching = true;
    }
    gw->chat_complete = openai_chat_complete;
    return gw;
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
        if (resp->tool_calls[i].id) free(resp->tool_calls[i].id);
        if (resp->tool_calls[i].name) free(resp->tool_calls[i].name);
        if (resp->tool_calls[i].arguments_json) free(resp->tool_calls[i].arguments_json);
    }
    if (resp->tool_calls) free(resp->tool_calls);
    memset(resp, 0, sizeof(ModelGatewayResponse));
}
