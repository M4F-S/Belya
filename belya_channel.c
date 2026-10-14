#include "common.h"
#include "belya_channel.h"
#include <curl/curl.h>
#include <time.h>
#include <unistd.h>

static size_t channel_curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total = size * nmemb;
    DynString *ds = (DynString *)userdata;
    dyn_str_append_len(ds, (const char *)ptr, total);
    return total;
}

/* -------------------------------------------------------------------------- */
/* Discord Adapter Implementation                                             */
/* -------------------------------------------------------------------------- */

BelyaDiscordAdapter *belya_discord_init(const char *bot_token, const char *allowed_channel_id) {
    if (!bot_token || strlen(bot_token) == 0) return NULL;

    BelyaDiscordAdapter *bot = calloc(1, sizeof(BelyaDiscordAdapter));
    if (!bot) return NULL;

    bot->bot_token = xstrdup(bot_token);
    bot->allowed_channel_id = allowed_channel_id ? xstrdup(allowed_channel_id) : NULL;
    bot->last_sequence = 0;
    bot->heartbeat_interval_ms = 41250;
    bot->connected = false;

    return bot;
}

void belya_discord_free(BelyaDiscordAdapter *bot) {
    if (!bot) return;
    if (bot->bot_token) free(bot->bot_token);
    if (bot->allowed_channel_id) free(bot->allowed_channel_id);
    free(bot);
}

bool belya_discord_send_message(BelyaDiscordAdapter *bot, const char *channel_id, const char *text) {
    if (!bot || !channel_id || !text) return false;

    // Fast-path for testing/mocking
    if (bot->bot_token && (strncmp(bot->bot_token, "mock_", 5) == 0 || strncmp(bot->bot_token, "test_", 5) == 0)) {
        return true;
    }

    CURL *curl = curl_easy_init();
    if (!curl) return false;

    char url[512];
    snprintf(url, sizeof(url), "https://discord.com/api/v10/channels/%s/messages", channel_id);

    JsonValue *payload = json_create_object();
    json_obj_add(payload, "content", json_create_string(text));
    char *json_body = json_serialize(payload);
    json_free(payload);

    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bot %s", bot->bot_token);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, auth_header);

    DynString response = dyn_str_new();

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, channel_curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    free(json_body);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    dyn_str_free(&response);

    return (res == CURLE_OK && http_code >= 200 && http_code < 300);
}

bool belya_discord_handle_event(BelyaDiscordAdapter *bot, const char *event_json, BelyaChannelMessage *out_msg) {
    if (!bot || !event_json || !out_msg) return false;

    JsonValue *root = json_parse(event_json);
    if (!root) return false;

    memset(out_msg, 0, sizeof(BelyaChannelMessage));
    snprintf(out_msg->platform, sizeof(out_msg->platform), "discord");
    out_msg->timestamp = time(NULL);

    int op = (int)json_obj_get_num(root, "op", -1);
    long seq = (long)json_obj_get_num(root, "s", 0);
    if (seq > 0) bot->last_sequence = seq;

    // Opcode 10: Hello -> Heartbeat Interval
    if (op == 10) {
        JsonValue *d = json_obj_get(root, "d");
        if (d) {
            bot->heartbeat_interval_ms = (int)json_obj_get_num(d, "heartbeat_interval", 41250);
            bot->connected = true;
        }
        json_free(root);
        return false;
    }

    // Opcode 0: Dispatch
    if (op == 0) {
        const char *t = json_obj_get_str(root, "t");
        if (t && strcmp(t, "MESSAGE_CREATE") == 0) {
            JsonValue *d = json_obj_get(root, "d");
            if (d) {
                const char *cid = json_obj_get_str(d, "channel_id");
                const char *content = json_obj_get_str(d, "content");
                if (cid) snprintf(out_msg->channel_id, sizeof(out_msg->channel_id), "%s", cid);
                if (content) snprintf(out_msg->text, sizeof(out_msg->text), "%s", content);

                // Thread / Message reference check
                JsonValue *thread = json_obj_get(d, "thread");
                if (thread) {
                    const char *tid = json_obj_get_str(thread, "id");
                    if (tid) snprintf(out_msg->thread_id, sizeof(out_msg->thread_id), "%s", tid);
                }

                JsonValue *author = json_obj_get(d, "author");
                if (author) {
                    const char *uid = json_obj_get_str(author, "id");
                    const char *uname = json_obj_get_str(author, "username");
                    if (uid) snprintf(out_msg->user_id, sizeof(out_msg->user_id), "%s", uid);
                    if (uname) snprintf(out_msg->user_name, sizeof(out_msg->user_name), "%s", uname);
                }

                json_free(root);
                return true;
            }
        }
    }

    json_free(root);
    return false;
}

/* -------------------------------------------------------------------------- */
/* Slack Adapter Implementation                                               */
/* -------------------------------------------------------------------------- */

BelyaSlackAdapter *belya_slack_init(const char *bot_token, const char *app_token, const char *allowed_channel_id) {
    if (!bot_token || strlen(bot_token) == 0) return NULL;

    BelyaSlackAdapter *bot = calloc(1, sizeof(BelyaSlackAdapter));
    if (!bot) return NULL;

    bot->bot_token = xstrdup(bot_token);
    bot->app_token = app_token ? xstrdup(app_token) : NULL;
    bot->allowed_channel_id = allowed_channel_id ? xstrdup(allowed_channel_id) : NULL;
    bot->connected = false;

    return bot;
}

void belya_slack_free(BelyaSlackAdapter *bot) {
    if (!bot) return;
    if (bot->bot_token) free(bot->bot_token);
    if (bot->app_token) free(bot->app_token);
    if (bot->allowed_channel_id) free(bot->allowed_channel_id);
    free(bot);
}

bool belya_slack_send_message(BelyaSlackAdapter *bot, const char *channel_id, const char *text) {
    if (!bot || !channel_id || !text) return false;

    // Fast-path for testing/mocking
    if (bot->bot_token && (strncmp(bot->bot_token, "mock_", 5) == 0 || strncmp(bot->bot_token, "test_", 5) == 0)) {
        return true;
    }

    CURL *curl = curl_easy_init();
    if (!curl) return false;

    const char *url = "https://slack.com/api/chat.postMessage";

    JsonValue *payload = json_create_object();
    json_obj_add(payload, "channel", json_create_string(channel_id));
    json_obj_add(payload, "text", json_create_string(text));
    char *json_body = json_serialize(payload);
    json_free(payload);

    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", bot->bot_token);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json; charset=utf-8");
    headers = curl_slist_append(headers, auth_header);

    DynString response = dyn_str_new();

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, channel_curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    free(json_body);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    dyn_str_free(&response);

    return (res == CURLE_OK && http_code >= 200 && http_code < 300);
}

bool belya_slack_handle_webhook(BelyaSlackAdapter *bot, const char *payload_json, BelyaChannelMessage *out_msg, char *out_ack_response, size_t ack_sz) {
    (void)bot;
    if (!payload_json || !out_ack_response || ack_sz == 0) return false;

    JsonValue *root = json_parse(payload_json);
    if (!root) return false;

    const char *type = json_obj_get_str(root, "type");
    if (!type) {
        json_free(root);
        return false;
    }

    // 1. URL Verification Challenge (Slack Setup) -> return < 100 ms
    if (strcmp(type, "url_verification") == 0) {
        const char *challenge = json_obj_get_str(root, "challenge");
        snprintf(out_ack_response, ack_sz, "{\"challenge\":\"%s\"}", challenge ? challenge : "");
        json_free(root);
        return false; // Handled as handshake challenge
    }

    // 2. Event Callback
    if (strcmp(type, "event_callback") == 0) {
        snprintf(out_ack_response, ack_sz, "{\"ok\":true}");

        if (out_msg) {
            memset(out_msg, 0, sizeof(BelyaChannelMessage));
            snprintf(out_msg->platform, sizeof(out_msg->platform), "slack");
            out_msg->timestamp = time(NULL);

            JsonValue *event = json_obj_get(root, "event");
            if (event) {
                const char *cid = json_obj_get_str(event, "channel");
                const char *text = json_obj_get_str(event, "text");
                const char *uid = json_obj_get_str(event, "user");
                const char *thread_ts = json_obj_get_str(event, "thread_ts");

                if (cid) snprintf(out_msg->channel_id, sizeof(out_msg->channel_id), "%s", cid);
                if (text) snprintf(out_msg->text, sizeof(out_msg->text), "%s", text);
                if (uid) snprintf(out_msg->user_id, sizeof(out_msg->user_id), "%s", uid);
                if (thread_ts) snprintf(out_msg->thread_id, sizeof(out_msg->thread_id), "%s", thread_ts);

                json_free(root);
                return true;
            }
        }
    }

    snprintf(out_ack_response, ack_sz, "{\"ok\":true}");
    json_free(root);
    return false;
}

/* -------------------------------------------------------------------------- */
/* Polymorphic Omnichannel Gateway Implementation                           */
/* -------------------------------------------------------------------------- */

static bool omni_discord_send(BelyaOmniChannel *chan, const char *target_id, const char *text) {
    if (!chan || !chan->adapter) return false;
    return belya_discord_send_message((BelyaDiscordAdapter *)chan->adapter, target_id, text);
}

static bool omni_slack_send(BelyaOmniChannel *chan, const char *target_id, const char *text) {
    if (!chan || !chan->adapter) return false;
    return belya_slack_send_message((BelyaSlackAdapter *)chan->adapter, target_id, text);
}

BelyaOmniChannel *belya_channel_create_discord(const char *token, const char *channel_id) {
    BelyaDiscordAdapter *ad = belya_discord_init(token, channel_id);
    if (!ad) return NULL;

    BelyaOmniChannel *chan = calloc(1, sizeof(BelyaOmniChannel));
    if (!chan) {
        belya_discord_free(ad);
        return NULL;
    }

    chan->type = BELYA_CHAN_DISCORD;
    snprintf(chan->name, sizeof(chan->name), "discord");
    chan->adapter = ad;
    chan->send_fn = omni_discord_send;
    return chan;
}

BelyaOmniChannel *belya_channel_create_slack(const char *bot_token, const char *app_token, const char *channel_id) {
    BelyaSlackAdapter *ad = belya_slack_init(bot_token, app_token, channel_id);
    if (!ad) return NULL;

    BelyaOmniChannel *chan = calloc(1, sizeof(BelyaOmniChannel));
    if (!chan) {
        belya_slack_free(ad);
        return NULL;
    }

    chan->type = BELYA_CHAN_SLACK;
    snprintf(chan->name, sizeof(chan->name), "slack");
    chan->adapter = ad;
    chan->send_fn = omni_slack_send;
    return chan;
}

bool belya_channel_send(BelyaOmniChannel *chan, const char *target_id, const char *text) {
    if (!chan || !chan->send_fn) return false;
    return chan->send_fn(chan, target_id, text);
}

void belya_channel_free(BelyaOmniChannel *chan) {
    if (!chan) return;
    if (chan->adapter) {
        if (chan->type == BELYA_CHAN_DISCORD) {
            belya_discord_free((BelyaDiscordAdapter *)chan->adapter);
        } else if (chan->type == BELYA_CHAN_SLACK) {
            belya_slack_free((BelyaSlackAdapter *)chan->adapter);
        }
    }
    free(chan);
}

/* -------------------------------------------------------------------------- */
/* Cross-Platform Session Continuity                                         */
/* -------------------------------------------------------------------------- */

void belya_channel_get_session_id(const BelyaChannelMessage *msg, char *out_session_id, size_t out_sz) {
    if (!msg || !out_session_id || out_sz == 0) return;

    if (strlen(msg->thread_id) > 0) {
        snprintf(out_session_id, out_sz, "%s_%s_%s", msg->platform, msg->channel_id, msg->thread_id);
    } else {
        snprintf(out_session_id, out_sz, "%s_%s", msg->platform, msg->channel_id);
    }
}

bool belya_channel_route_message(BelyaOmniChannel *chan, BelyaHarness *harness, const BelyaChannelMessage *msg, char *out_reply, size_t reply_sz) {
    if (!harness || !harness->agent || !msg || !out_reply || reply_sz == 0) return false;

    char session_id[256];
    belya_channel_get_session_id(msg, session_id, sizeof(session_id));

    // Restore prior session continuity if exists
    belya_agent_load_session(harness->agent, session_id);

    // Format formatted prompt with user identity
    char formatted_prompt[4300];
    snprintf(formatted_prompt, sizeof(formatted_prompt), "[User: %s (id: %s)] %s",
             strlen(msg->user_name) > 0 ? msg->user_name : "anonymous",
             strlen(msg->user_id) > 0 ? msg->user_id : "unknown",
             msg->text);

    // Execute agent reasoning step
    belya_agent_add_message(harness->agent, "user", formatted_prompt);
    ModelGatewayResponse resp = belya_agent_step(harness->agent);

    if (resp.content && strlen(resp.content) > 0) {
        snprintf(out_reply, reply_sz, "%s", resp.content);
    } else {
        snprintf(out_reply, reply_sz, "I have received your request from %s.", msg->platform);
    }
    model_gateway_response_free(&resp);

    // Persist updated conversation session to SQLite state store
    belya_agent_save_session(harness->agent, session_id, session_id);

    // If channel provided, send response back
    if (chan && strlen(msg->channel_id) > 0) {
        belya_channel_send(chan, msg->channel_id, out_reply);
    }

    return true;
}
