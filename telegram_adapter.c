#include "telegram_adapter.h"
#include <curl/curl.h>
#include <signal.h>
#include <unistd.h>

static volatile bool g_telegram_interrupted = false;

static void telegram_sig_handler(int sig) {
    (void)sig;
    g_telegram_interrupted = true;
}

static size_t tg_curl_sink_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total = size * nmemb;
    DynString *ds = (DynString *)userdata;
    dyn_str_append_len(ds, (const char *)ptr, total);
    return total;
}

static JsonValue *telegram_http_post(TelegramBot *bot, const char *method, const JsonValue *payload) {
    if (!bot || !bot->bot_token || !method) return NULL;

    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    char url[512];
    snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/%s", bot->bot_token, method);

    char *json_body = payload ? json_serialize(payload) : strdup("{}");

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    DynString response_body = dyn_str_new();

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, tg_curl_sink_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 35L);

    CURLcode code = curl_easy_perform(curl);

    free(json_body);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (code != CURLE_OK) {
        dyn_str_free(&response_body);
        return NULL;
    }

    JsonValue *root = json_parse(response_body.data);
    dyn_str_free(&response_body);
    return root;
}

static bool telegram_is_authorized(TelegramBot *bot, const char *chat_id) {
    if (!bot->allowed_chat_id || strlen(bot->allowed_chat_id) == 0) {
        // Zero-trust: Refuse all if allowed_chat_id is not explicitly configured
        return false;
    }
    if (!chat_id) return false;

    // Check exact match or comma-separated list
    char *list_copy = strdup(bot->allowed_chat_id);
    char *token = strtok(list_copy, ", ");
    bool authorized = false;

    while (token) {
        if (strcmp(token, chat_id) == 0) {
            authorized = true;
            break;
        }
        token = strtok(NULL, ", ");
    }
    free(list_copy);
    return authorized;
}

TelegramBot *telegram_bot_init(const char *bot_token, const char *allowed_chat_id) {
    if (!bot_token || strlen(bot_token) == 0) return NULL;

    TelegramBot *bot = calloc(1, sizeof(TelegramBot));
    bot->bot_token = strdup(bot_token);
    bot->allowed_chat_id = strdup(allowed_chat_id ? allowed_chat_id : "");
    bot->last_update_id = 0;
    bot->running = false;
    return bot;
}

bool telegram_bot_send_message(TelegramBot *bot, const char *chat_id, const char *text) {
    if (!bot || !chat_id || !text) return false;

    JsonValue *payload = json_create_object();
    json_obj_add(payload, "chat_id", json_create_string(chat_id));
    json_obj_add(payload, "text", json_create_string(text));

    JsonValue *resp = telegram_http_post(bot, "sendMessage", payload);
    json_free(payload);

    if (!resp) return false;
    bool ok = json_obj_get_bool(resp, "ok", false);
    json_free(resp);
    return ok;
}

bool telegram_bot_send_chunks(TelegramBot *bot, const char *chat_id, const char *text) {
    if (!bot || !chat_id || !text) return false;

    size_t len = strlen(text);
    const size_t CHUNK_SIZE = 3800; // Telegram limit is 4096, stay safely below

    if (len <= CHUNK_SIZE) {
        return telegram_bot_send_message(bot, chat_id, text);
    }

    size_t offset = 0;
    while (offset < len) {
        size_t take = (len - offset > CHUNK_SIZE) ? CHUNK_SIZE : (len - offset);
        char *chunk = malloc(take + 1);
        memcpy(chunk, text + offset, take);
        chunk[take] = '\0';

        telegram_bot_send_message(bot, chat_id, chunk);
        free(chunk);

        offset += take;
        usleep(50000); // 50ms delay between consecutive Telegram messages
    }
    return true;
}

typedef struct {
    TelegramBot *bot;
    char current_chat_id[64];
} TelegramPromptContext;

static bool telegram_permission_prompt_callback(CHarness *h, const char *name, const char *args, void *userdata) {
    (void)h;
    TelegramPromptContext *ctx = (TelegramPromptContext *)userdata;
    if (!ctx || !ctx->bot || strlen(ctx->current_chat_id) == 0) return false;

    char prompt_text[1024];
    snprintf(prompt_text, sizeof(prompt_text),
        "🛡️ <b>Security Gate: Authorization Required</b>\n\n"
        "<b>Tool:</b> <code>%s</code>\n"
        "<b>Arguments:</b>\n<code>%s</code>",
        name, args ? args : "{}");

    // Build Inline Keyboard
    JsonValue *payload = json_create_object();
    json_obj_add(payload, "chat_id", json_create_string(ctx->current_chat_id));
    json_obj_add(payload, "text", json_create_string(prompt_text));
    json_obj_add(payload, "parse_mode", json_create_string("HTML"));

    JsonValue *markup = json_create_object();
    JsonValue *keyboard = json_create_array();
    JsonValue *row = json_create_array();

    JsonValue *btn_app = json_create_object();
    json_obj_add(btn_app, "text", json_create_string("✅ Approve"));
    json_obj_add(btn_app, "callback_data", json_create_string("approve"));
    json_arr_add(row, btn_app);

    JsonValue *btn_deny = json_create_object();
    json_obj_add(btn_deny, "text", json_create_string("❌ Deny"));
    json_obj_add(btn_deny, "callback_data", json_create_string("deny"));
    json_arr_add(row, btn_deny);

    json_arr_add(keyboard, row);
    json_obj_add(markup, "inline_keyboard", keyboard);
    json_obj_add(payload, "reply_markup", markup);

    JsonValue *send_resp = telegram_http_post(ctx->bot, "sendMessage", payload);
    json_free(payload);

    if (!send_resp) return false;

    // Extract message_id to edit later
    JsonValue *res_obj = json_obj_get(send_resp, "result");
    double msg_id = res_obj ? json_obj_get_num(res_obj, "message_id", 0) : 0;
    json_free(send_resp);

    // Poll for callback_query response (up to 60 seconds timeout)
    int poll_attempts = 30; // 30 * 2s = 60s
    bool decision = false;
    bool answered = false;

    while (!answered && poll_attempts-- > 0 && !g_telegram_interrupted) {
        JsonValue *up_payload = json_create_object();
        json_obj_add(up_payload, "offset", json_create_number(ctx->bot->last_update_id + 1));
        json_obj_add(up_payload, "timeout", json_create_number(2));

        JsonValue *up_resp = telegram_http_post(ctx->bot, "getUpdates", up_payload);
        json_free(up_payload);

        if (up_resp) {
            JsonValue *updates = json_obj_get(up_resp, "result");
            if (updates && updates->type == JSON_ARRAY) {
                for (size_t i = 0; i < updates->u.array.count; i++) {
                    JsonValue *up = updates->u.array.items[i];
                    long u_id = (long)json_obj_get_num(up, "update_id", 0);
                    if (u_id > ctx->bot->last_update_id) {
                        ctx->bot->last_update_id = u_id;
                    }

                    JsonValue *cb = json_obj_get(up, "callback_query");
                    if (cb) {
                        const char *cb_id = json_obj_get_str(cb, "id");
                        const char *data = json_obj_get_str(cb, "data");

                        if (data) {
                            if (strcmp(data, "approve") == 0) {
                                decision = true;
                                answered = true;
                            } else if (strcmp(data, "deny") == 0) {
                                decision = false;
                                answered = true;
                            }

                            // Answer callback query
                            if (cb_id) {
                                JsonValue *ans_p = json_create_object();
                                json_obj_add(ans_p, "callback_query_id", json_create_string(cb_id));
                                json_obj_add(ans_p, "text", json_create_string(decision ? "Approved" : "Denied"));
                                JsonValue *ans_r = telegram_http_post(ctx->bot, "answerCallbackQuery", ans_p);
                                json_free(ans_p);
                                if (ans_r) json_free(ans_r);
                            }
                            break;
                        }
                    }
                }
            }
            json_free(up_resp);
        }
    }

    // Edit message with decision result
    if (msg_id > 0) {
        char edit_text[1024];
        snprintf(edit_text, sizeof(edit_text),
            "🛡️ <b>Security Gate:</b> %s\n"
            "<b>Tool:</b> <code>%s</code>\n"
            "<b>Decision:</b> %s",
            name, args ? args : "{}", decision ? "✅ <b>APPROVED</b>" : "❌ <b>DENIED</b>");

        JsonValue *edit_p = json_create_object();
        json_obj_add(edit_p, "chat_id", json_create_string(ctx->current_chat_id));
        json_obj_add(edit_p, "message_id", json_create_number(msg_id));
        json_obj_add(edit_p, "text", json_create_string(edit_text));
        json_obj_add(edit_p, "parse_mode", json_create_string("HTML"));

        JsonValue *edit_r = telegram_http_post(ctx->bot, "editMessageText", edit_p);
        json_free(edit_p);
        if (edit_r) json_free(edit_r);
    }

    return decision;
}

void telegram_bot_run(TelegramBot *bot, CHarness *harness) {
    if (!bot || !harness) return;

    bot->running = true;
    g_telegram_interrupted = false;
    signal(SIGINT, telegram_sig_handler);
    signal(SIGTERM, telegram_sig_handler);

    TelegramPromptContext prompt_ctx;
    prompt_ctx.bot = bot;
    memset(prompt_ctx.current_chat_id, 0, sizeof(prompt_ctx.current_chat_id));

    harness->permission_prompt_fn = telegram_permission_prompt_callback;
    harness->permission_userdata = &prompt_ctx;

    printf("\033[1;32m=== CHarness Telegram Bot Daemon Online ===\033[0m\n");
    printf("Allowed Chat ID: \033[1;36m%s\033[0m\n", strlen(bot->allowed_chat_id) > 0 ? bot->allowed_chat_id : "[None - Zero Trust Mode]");
    printf("Model:           \033[1;33m%s\033[0m\n", harness->agent->gateway->model);
    printf("Listening for Telegram updates via long polling...\n\n");

    while (bot->running && !g_telegram_interrupted) {
        JsonValue *poll_p = json_create_object();
        json_obj_add(poll_p, "offset", json_create_number(bot->last_update_id + 1));
        json_obj_add(poll_p, "timeout", json_create_number(20));

        JsonValue *resp = telegram_http_post(bot, "getUpdates", poll_p);
        json_free(poll_p);

        if (!resp) {
            sleep(2); // Network sleep
            continue;
        }

        JsonValue *updates = json_obj_get(resp, "result");
        if (updates && updates->type == JSON_ARRAY) {
            for (size_t i = 0; i < updates->u.array.count; i++) {
                JsonValue *up = updates->u.array.items[i];
                long u_id = (long)json_obj_get_num(up, "update_id", 0);
                if (u_id > bot->last_update_id) {
                    bot->last_update_id = u_id;
                }

                JsonValue *msg = json_obj_get(up, "message");
                if (!msg) continue;

                JsonValue *chat = json_obj_get(msg, "chat");
                if (!chat) continue;

                double cid_num = json_obj_get_num(chat, "id", 0);
                char chat_id_str[64];
                snprintf(chat_id_str, sizeof(chat_id_str), "%.0f", cid_num);

                const char *text = json_obj_get_str(msg, "text");
                if (!text || strlen(text) == 0) continue;

                // Security Authorization Check
                if (!telegram_is_authorized(bot, chat_id_str)) {
                    printf("\033[1;31m[Security Alert] Blocked unauthorized message from Chat ID: %s\033[0m\n", chat_id_str);
                    char warn[256];
                    snprintf(warn, sizeof(warn), "⛔ <b>Access Denied:</b> Your Telegram Chat ID (<code>%s</code>) is not authorized on this VPS.", chat_id_str);
                    telegram_bot_send_message(bot, chat_id_str, warn);
                    continue;
                }

                strncpy(prompt_ctx.current_chat_id, chat_id_str, sizeof(prompt_ctx.current_chat_id) - 1);

                // Handle Telegram Commands
                if (strcmp(text, "/start") == 0 || strcmp(text, "/help") == 0) {
                    char welcome[1024];
                    snprintf(welcome, sizeof(welcome),
                        "🤖 <b>CHarness Autonomous VPS Agent Online</b>\n\n"
                        "<b>Active Model:</b> <code>%s</code>\n"
                        "<b>CWD:</b> <code>%s</code>\n"
                        "<b>Context Messages:</b> <code>%zu</code>\n\n"
                        "<b>Commands:</b>\n"
                        "/status - System status & RAM usage\n"
                        "/tools - List registered VPS tools\n"
                        "/rules - Active .agentrules\n"
                        "/clear - Clear conversation history\n"
                        "/compact - Compact older turns\n\n"
                        "Send any instructions directly to start autonomous execution!",
                        harness->agent->gateway->model, harness->cwd, harness->agent->msg_count);
                    telegram_bot_send_message(bot, chat_id_str, welcome);
                    continue;
                }

                if (strcmp(text, "/status") == 0) {
                    char status_msg[512];
                    snprintf(status_msg, sizeof(status_msg),
                        "📊 <b>System Status</b>\n"
                        "• Model: <code>%s</code>\n"
                        "• CWD: <code>%s</code>\n"
                        "• Memory Table: <code>%s</code>\n"
                        "• Context: <code>%zu messages</code>",
                        harness->agent->gateway->model, harness->cwd,
                        harness->agent->has_fts5 ? "SQLite FTS5 (BM25)" : "Standard SQLite",
                        harness->agent->msg_count);
                    telegram_bot_send_message(bot, chat_id_str, status_msg);
                    continue;
                }

                if (strcmp(text, "/tools") == 0) {
                    DynString td = dyn_str_new();
                    dyn_str_appendf(&td, "🛠️ <b>Registered Tools (%zu):</b>\n\n", harness->tool_count);
                    for (size_t t = 0; t < harness->tool_count; t++) {
                        dyn_str_appendf(&td, "• <code>%s</code> [%s]: %s\n",
                            harness->tools[t].name,
                            harness->tools[t].security == PERM_ALLOW ? "ALLOW" : "ASK",
                            t < harness->agent->schema_count ? harness->agent->schemas[t].description : "");
                    }
                    telegram_bot_send_chunks(bot, chat_id_str, td.data);
                    dyn_str_free(&td);
                    continue;
                }

                if (strcmp(text, "/clear") == 0) {
                    c_agent_clear_history(harness->agent);
                    telegram_bot_send_message(bot, chat_id_str, "🧹 Conversation history cleared. System prompt preserved.");
                    continue;
                }

                if (strcmp(text, "/compact") == 0) {
                    c_agent_compact_history(harness->agent, 10);
                    char cmsg[128];
                    snprintf(cmsg, sizeof(cmsg), "📦 History compacted to %zu messages.", harness->agent->msg_count);
                    telegram_bot_send_message(bot, chat_id_str, cmsg);
                    continue;
                }

                // Process User Turn
                printf("\033[1;35m[Telegram Input from %s]:\033[0m %s\n", chat_id_str, text);
                c_agent_add_message(harness->agent, "user", text);

                bool turn_running = true;
                int max_steps = 10;

                while (turn_running && max_steps-- > 0 && !g_telegram_interrupted) {
                    ModelGatewayResponse resp = c_agent_step(harness->agent);

                    if (!resp.has_tool_call) {
                        if (resp.content && strlen(resp.content) > 0) {
                            telegram_bot_send_chunks(bot, chat_id_str, resp.content);
                        } else {
                            telegram_bot_send_message(bot, chat_id_str, "Action completed.");
                        }
                        turn_running = false;
                    } else {
                        for (size_t k = 0; k < resp.tool_call_count; k++) {
                            ModelParsedToolCall *tc = &resp.tool_calls[k];
                            printf("[Bot Tool Call]: %s(%s)\n", tc->name, tc->arguments_json);

                            CHarnessRegisteredTool *matched = NULL;
                            for (size_t t = 0; t < harness->tool_count; t++) {
                                if (strcmp(harness->tools[t].name, tc->name) == 0) {
                                    matched = &harness->tools[t];
                                    break;
                                }
                            }

                            if (!matched || matched->security == PERM_DENY) {
                                c_agent_add_tool_result(harness->agent, tc->id, tc->name, "Error: Tool blocked by security policy.");
                                continue;
                            }

                            if (matched->security == PERM_ASK_USER) {
                                bool permitted = false;
                                if (harness->permission_prompt_fn) {
                                    permitted = harness->permission_prompt_fn(harness, tc->name, tc->arguments_json, harness->permission_userdata);
                                }
                                if (!permitted) {
                                    c_agent_add_tool_result(harness->agent, tc->id, tc->name, "Error: User denied permission for this tool call.");
                                    continue;
                                }
                            }

                            JsonValue *args_parsed = json_parse(tc->arguments_json);
                            char *obs = NULL;
                            if (matched->mcp_client) {
                                obs = mcp_client_call_tool(matched->mcp_client, tc->name, args_parsed);
                            } else if (matched->callback) {
                                obs = matched->callback(harness->agent, args_parsed);
                            }
                            json_free(args_parsed);

                            c_agent_add_tool_result(harness->agent, tc->id, tc->name, obs ? obs : "Success");
                            if (obs) free(obs);
                        }
                    }
                    model_gateway_response_free(&resp);
                }
            }
        }
        json_free(resp);
    }

    printf("\033[1;33m[Telegram Bot] Service shutting down gracefully.\033[0m\n");
}

void telegram_bot_stop(TelegramBot *bot) {
    if (bot) bot->running = false;
}

void telegram_bot_free(TelegramBot *bot) {
    if (!bot) return;
    if (bot->bot_token) free(bot->bot_token);
    if (bot->allowed_chat_id) free(bot->allowed_chat_id);
    free(bot);
}
