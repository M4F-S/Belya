/*
 * Drives the Telegram send path against a local fake Bot API (tests/fake_telegram.py)
 * and asserts on the wire payloads: HTML parse mode, tables converted, escaping.
 *
 *   TELEGRAM_API_BASE is set to the fake server by the test runner (make test-tgsend).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "telegram_adapter.h"

static int g_fail = 0;
static int g_checks = 0;

static void expect(int cond, const char *what) {
    g_checks++;
    if (!cond) {
        g_fail++;
        printf("  [FAIL] %s\n", what);
    }
}

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("usage: %s <api_base> <capture_file> <chat_id>\n", argv[0]);
        return 2;
    }
    setenv("TELEGRAM_API_BASE", argv[1], 1);
    const char *capture = argv[2];
    const char *chat_id = argv[3];

    TelegramBot *bot = telegram_bot_init("TESTTOKEN", chat_id);
    if (!bot) {
        printf("  [FAIL] telegram_bot_init returned NULL\n");
        return 1;
    }

    printf("[Test] Telegram send path (fake Bot API at %s)\n", argv[1]);

    /* 1. Rich send: a GFM table must not reach the wire as pipes. */
    const char *md =
        "## SUMMARY\n"
        "| Check | Result |\n"
        "|---|---|\n"
        "| Service | active |\n";
    expect(telegram_bot_send_rich(bot, chat_id, md), "send_rich reports success");

    /* 2. Escaping survives to the wire. */
    expect(telegram_bot_send_rich(bot, chat_id, "if (a < b && c > d)\n"), "send_rich escapes");

    /* 3. Long text falls back to plain chunks and still succeeds. */
    char big[9000];
    memset(big, 'A', sizeof(big));
    big[sizeof(big) - 1] = '\0';
    expect(telegram_bot_send_rich(bot, chat_id, big), "send_rich handles oversized text");

    /* 4. Read what the fake server captured and assert on it. */
    FILE *fp = fopen(capture, "r");
    if (!fp) {
        printf("  [FAIL] capture file missing: %s\n", capture);
        telegram_bot_free(bot);
        return 1;
    }
    static char buf[262144];
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    buf[n] = '\0';
    fclose(fp);

    expect(n > 0, "fake server captured at least one request");
    expect(strstr(buf, "\"parse_mode\": \"HTML\"") != NULL || strstr(buf, "\"parse_mode\":\"HTML\"") != NULL,
           "first send used HTML parse mode");
    expect(strstr(buf, "| Check |") == NULL, "table pipes did not reach the wire");
    expect(strstr(buf, "<b>SUMMARY</b>") != NULL, "heading rendered as bold tag");
    expect(strstr(buf, "&lt;") != NULL, "angle bracket escaped");
    expect(strstr(buf, "&amp;") != NULL, "ampersand escaped");
    expect(strstr(buf, "sendMessage") != NULL, "used sendMessage");

    telegram_bot_free(bot);
    printf("  telegram send path: %d checks, %d failures\n", g_checks, g_fail);
    printf("%s\n", g_fail == 0 ? "PASSED" : "FAILED");
    return g_fail == 0 ? 0 : 1;
}
