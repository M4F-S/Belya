#include "jev_client.h"
#include <curl/curl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static size_t jev_curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total = size * nmemb;
    DynString *ds = (DynString *)userdata;
    dyn_str_append_len(ds, (const char *)ptr, total);
    return total;
}

JevClient *jev_client_init(const char *api_key) {
    if (!api_key || strlen(api_key) == 0) return NULL;
    JevClient *client = calloc(1, sizeof(JevClient));
    if (!client) return NULL;

    client->api_key = strdup(api_key);
    if (!client->api_key) {
        free(client);
        return NULL;
    }

    client->base_url = strdup("https://jevtypesafeai.com/api/v1");
    if (!client->base_url) {
        free(client->api_key);
        free(client);
        return NULL;
    }

    client->timeout_ms = 5000; /* 5 second fail-fast timeout */
    return client;
}

void jev_client_free(JevClient *client) {
    if (!client) return;
    if (client->api_key) free(client->api_key);
    if (client->base_url) free(client->base_url);
    free(client);
}

static char *jev_http_post(JevClient *client, const char *endpoint_path, const char *json_body) {
    if (!client || !client->api_key || !endpoint_path || !json_body) return NULL;

    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    char url[512];
    snprintf(url, sizeof(url), "%s%s", client->base_url, endpoint_path);

    char auth_hdr[512];
    snprintf(auth_hdr, sizeof(auth_hdr), "Authorization: Bearer %s", client->api_key);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, auth_hdr);

    DynString resp_ds = dyn_str_new();
    if (!resp_ds.data) {
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return NULL;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, jev_curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_ds);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, client->timeout_ms > 0 ? client->timeout_ms : 5000L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "BelyaAgent/7.0 (Autonomous C99 Engine)");

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || http_code < 200 || http_code >= 300) {
        dyn_str_free(&resp_ds);
        return NULL;
    }

    return resp_ds.data;
}

char *jev_parse_triage_response(const char *json_str) {
    if (!json_str) return NULL;
    JsonValue *resp_val = json_parse(json_str);
    if (!resp_val) return NULL;

    char *selected_role = NULL;
    JsonValue *answers = json_obj_get(resp_val, "answers");
    if (answers) {
        JsonValue *route_ans = json_obj_get(answers, "route");
        if (route_ans) {
            const char *c = json_obj_get_str(route_ans, "choice");
            if (c) selected_role = strdup(c);
        }
    }

    json_free(resp_val);
    return selected_role;
}

char *jev_decide_triage_role(JevClient *client, const char *user_prompt) {
    if (!client || !user_prompt) return NULL;

    JsonValue *root = json_create_object();
    if (!root) return NULL;
    json_obj_add(root, "state", json_create_string(user_prompt));

    JsonValue *q_obj = json_create_object();
    JsonValue *route_q = json_create_object();
    json_obj_add(route_q, "type", json_create_string("choice"));
    json_obj_add(route_q, "instructions", json_create_string("Select the best agent role for this task."));

    JsonValue *criteria = json_create_object();
    json_obj_add(criteria, "architect", json_create_string("high-level planning, file exploration, research, architecture design"));
    json_obj_add(criteria, "builder", json_create_string("code editing, writing files, refactoring, fixing bugs, applying patches"));
    json_obj_add(criteria, "tester", json_create_string("running tests, build verification, executing benchmarks, syntax checks"));
    json_obj_add(criteria, "direct", json_create_string("conversational greeting, question about status, capability query"));
    json_obj_add(route_q, "criteria", criteria);

    json_obj_add(q_obj, "route", route_q);
    json_obj_add(root, "questions", q_obj);

    char *req_json = json_serialize(root);
    json_free(root);
    if (!req_json) return NULL;

    char *resp_str = jev_http_post(client, "/decide", req_json);
    free(req_json);
    if (!resp_str) return NULL;

    char *selected_role = jev_parse_triage_response(resp_str);
    free(resp_str);
    return selected_role;
}

JevRiskResult *jev_parse_risk_response(const char *json_str) {
    if (!json_str) return NULL;
    JsonValue *resp_val = json_parse(json_str);
    if (!resp_val) return NULL;

    JevRiskResult *res = calloc(1, sizeof(JevRiskResult));
    if (!res) {
        json_free(resp_val);
        return NULL;
    }

    const char *action_str = json_obj_get_str(resp_val, "action");
    res->action = strdup(action_str ? action_str : "allow");
    if (!res->action) {
        free(res);
        json_free(resp_val);
        return NULL;
    }
    res->risk = json_obj_get_num(resp_val, "risk", 0.0);
    res->confidence = json_obj_get_num(resp_val, "confidence", 1.0);

    json_free(resp_val);
    return res;
}

JevRiskResult *jev_check_tool_risk(JevClient *client, const char *goal, const char *tool, const char *arguments) {
    if (!client || !tool) return NULL;

    JsonValue *root = json_create_object();
    if (!root) return NULL;
    json_obj_add(root, "goal", json_create_string(goal ? goal : "Autonomous execution"));
    json_obj_add(root, "tool", json_create_string(tool));
    json_obj_add(root, "arguments", json_create_string(arguments ? arguments : ""));
    json_obj_add(root, "context", json_create_string("Belya autonomous execution"));

    char *req_json = json_serialize(root);
    json_free(root);
    if (!req_json) return NULL;

    char *resp_str = jev_http_post(client, "/agent/risk", req_json);
    free(req_json);
    if (!resp_str) return NULL;

    JevRiskResult *res = jev_parse_risk_response(resp_str);
    free(resp_str);
    return res;
}

void jev_risk_result_free(JevRiskResult *res) {
    if (!res) return;
    if (res->action) free(res->action);
    free(res);
}

JevContextFilterResult *jev_parse_filter_response(const char *json_str) {
    if (!json_str) return NULL;
    JsonValue *resp_val = json_parse(json_str);
    if (!resp_val) return NULL;

    JevContextFilterResult *res = calloc(1, sizeof(JevContextFilterResult));
    if (!res) {
        json_free(resp_val);
        return NULL;
    }

    const char *act = json_obj_get_str(resp_val, "action");
    const char *rel = json_obj_get_str(resp_val, "relevance");
    res->action = strdup(act ? act : "keep");
    res->relevance = strdup(rel ? rel : "minor");
    if (!res->action || !res->relevance) {
        if (res->action) free(res->action);
        if (res->relevance) free(res->relevance);
        free(res);
        json_free(resp_val);
        return NULL;
    }
    res->redundant = json_obj_get_bool(resp_val, "redundant", false);
    res->confidence = json_obj_get_num(resp_val, "confidence", 1.0);

    json_free(resp_val);
    return res;
}

JevContextFilterResult *jev_filter_context_item(JevClient *client, const char *task, const char *item_text) {
    if (!client || !item_text) return NULL;

    JsonValue *root = json_create_object();
    if (!root) return NULL;
    json_obj_add(root, "task", json_create_string(task ? task : "General task"));
    json_obj_add(root, "item", json_create_string(item_text));

    char *req_json = json_serialize(root);
    json_free(root);
    if (!req_json) return NULL;

    char *resp_str = jev_http_post(client, "/context/filter", req_json);
    free(req_json);
    if (!resp_str) return NULL;

    JevContextFilterResult *res = jev_parse_filter_response(resp_str);
    free(resp_str);
    return res;
}

void jev_context_filter_result_free(JevContextFilterResult *res) {
    if (!res) return;
    if (res->action) free(res->action);
    if (res->relevance) free(res->relevance);
    free(res);
}
