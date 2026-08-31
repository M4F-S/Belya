#ifndef TELEGRAM_ADAPTER_H
#define TELEGRAM_ADAPTER_H

#include "c_harness.h"

typedef struct {
    char *bot_token;
    char *allowed_chat_id;
    long last_update_id;
    bool running;
} TelegramBot;

TelegramBot *telegram_bot_init(const char *bot_token, const char *allowed_chat_id);
bool telegram_bot_send_message(TelegramBot *bot, const char *chat_id, const char *text);
bool telegram_bot_send_chunks(TelegramBot *bot, const char *chat_id, const char *text);
void telegram_bot_run(TelegramBot *bot, CHarness *harness);
void telegram_bot_stop(TelegramBot *bot);
void telegram_bot_free(TelegramBot *bot);

#endif
