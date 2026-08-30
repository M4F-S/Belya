#include "model_adapter.h"
#include <curl/curl.h>
#include <unistd.h>

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

        DynString response_body = dyn_str_new();
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");

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
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);

        CURLcode code = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (code != CURLE_OK) {
            if (attempt < max_attempts - 1 && (code == CURLE_OPERATION_TIMEDOUT || code == CURLE_COULDNT_CONNECT)) {
                dyn_str_free(&response_body);
                sleep(sleep_sec);
                sleep_sec *= 2;
                continue;
            }
            DynString err_ds = dyn_str_new();
            dyn_str_appendf(&err_ds, "Network Error: %s", curl_easy_strerror(code));
            res.content = err_ds.data;
            dyn_str_free(&response_body);
            free(json_body);
            return res;
        }

        // Check if rate limited or server error
        if ((http_code == 429 || http_code >= 500) && attempt < max_attempts - 1) {
            dyn_str_free(&response_body);
            sleep(sleep_sec);
            sleep_sec *= 2;
            continue;
        }

        // Parse Response
        JsonValue *root = json_parse(response_body.data);
        dyn_str_free(&response_body);
        if (!root) {
            res.content = strdup("Error: Failed to parse upstream model JSON response.");
            free(json_body);
            return res;
        }

        // Check for API Error object: {"error": {"message": "..."}}
        JsonValue *err_obj = json_obj_get(root, "error");
        if (err_obj) {
            const char *err_msg = json_obj_get_str(err_obj, "message");
            if (!err_msg) err_msg = json_obj_get_str(root, "message");
            DynString err_ds = dyn_str_new();
            dyn_str_appendf(&err_ds, "API Error (HTTP %ld): %s", http_code, err_msg ? err_msg : "Unknown error");
            res.content = err_ds.data;
            json_free(root);
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
        free(resp->tool_calls[i].id);
        free(resp->tool_calls[i].name);
        free(resp->tool_calls[i].arguments_json);
    }
    if (resp->tool_calls) free(resp->tool_calls);
    memset(resp, 0, sizeof(ModelGatewayResponse));
}
