#ifndef BELYA_CHANNEL_H
#define BELYA_CHANNEL_H

#include "common.h"
#include "belya_agent.h"
#include "belya_harness.h"

typedef enum {
    BELYA_CHAN_TELEGRAM = 0,
    BELYA_CHAN_DISCORD  = 1,
    BELYA_CHAN_SLACK    = 2
} BelyaChannelType;

typedef struct {
    char platform[32];      /* "telegram", "discord", "slack" */
    char channel_id[128];   /* Channel ID or Chat ID */
    char thread_id[128];    /* Thread ID or Sub-thread ID (if applicable) */
    char user_id[128];      /* Sender ID */
    char user_name[128];    /* Sender Name */
    char text[4096];        /* Message text */
    time_t timestamp;
} BelyaChannelMessage;

/* Discord Adapter */
typedef struct {
    char *bot_token;
    char *allowed_channel_id;
    char session_id[128];
    int heartbeat_interval_ms;
    long last_sequence;
    bool connected;
} BelyaDiscordAdapter;

BelyaDiscordAdapter *belya_discord_init(const char *bot_token, const char *allowed_channel_id);
bool belya_discord_send_message(BelyaDiscordAdapter *bot, const char *channel_id, const char *text);
bool belya_discord_handle_event(BelyaDiscordAdapter *bot, const char *event_json, BelyaChannelMessage *out_msg);
void belya_discord_free(BelyaDiscordAdapter *bot);

/* Slack Adapter */
typedef struct {
    char *bot_token;
    char *app_token;
    char *allowed_channel_id;
    bool connected;
} BelyaSlackAdapter;

BelyaSlackAdapter *belya_slack_init(const char *bot_token, const char *app_token, const char *allowed_channel_id);
bool belya_slack_send_message(BelyaSlackAdapter *bot, const char *channel_id, const char *text);
bool belya_slack_handle_webhook(BelyaSlackAdapter *bot, const char *payload_json, BelyaChannelMessage *out_msg, char *out_ack_response, size_t ack_sz);
void belya_slack_free(BelyaSlackAdapter *bot);

/* Polymorphic Omnichannel Gateway */
typedef struct BelyaOmniChannel BelyaOmniChannel;

typedef bool (*BelyaChanSendFn)(BelyaOmniChannel *chan, const char *target_id, const char *text);

struct BelyaOmniChannel {
    BelyaChannelType type;
    char name[32];
    void *adapter;
    BelyaChanSendFn send_fn;
};

BelyaOmniChannel *belya_channel_create_discord(const char *token, const char *channel_id);
BelyaOmniChannel *belya_channel_create_slack(const char *bot_token, const char *app_token, const char *channel_id);
bool belya_channel_send(BelyaOmniChannel *chan, const char *target_id, const char *text);
void belya_channel_free(BelyaOmniChannel *chan);

/* Session continuity: mapping channel thread IDs to persistent .belya/state.db session_id records */
void belya_channel_get_session_id(const BelyaChannelMessage *msg, char *out_session_id, size_t out_sz);
bool belya_channel_route_message(BelyaOmniChannel *chan, BelyaHarness *harness, const BelyaChannelMessage *msg, char *out_reply, size_t reply_sz);

/* Live Omnichannel Gateway Polling (Discord REST v10 & Slack conversations.history) */
bool belya_discord_poll_once(BelyaDiscordAdapter *bot, BelyaHarness *harness);
bool belya_slack_poll_once(BelyaSlackAdapter *bot, BelyaHarness *harness);
void belya_omnichannel_poll_env(BelyaHarness *harness);

#endif
