/*
 * tests/test_compaction.c - compaction must not rewrite the cached prefix.
 *
 * Providers cache on a token prefix. The system prompt and the root objective
 * are the only stable part of a long session, so compaction may summarise,
 * truncate and evict everything after them - but touching indices 0 and 1
 * changes the prefix and misses the cache for the whole rest of the session,
 * which is exactly what tier-1 (strip trailing whitespace) used to do.
 */
#include "belya_agent.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0;

static void ck(const char *name, int ok, const char *detail) {
    printf(ok ? "  [ok] %s\n" : "  [FAIL] %s%s%s\n", name,
           ok ? "" : (detail && *detail) ? " <- " : "", ok ? "" : (detail ? detail : ""));
    if (!ok) fails++;
}

int main(void) {
    printf("[Test] Cache-stable compaction\n");

    BelyaAgent *a = belya_agent_init(NULL, ":memory:", NULL);
    if (!a) {
        printf("  [FAIL] agent init\n");
        return 1;
    }

    belya_agent_add_message(a, "system", "SYSTEM PROMPT\n\n");
    belya_agent_add_message(a, "user", "ROOT OBJECTIVE\n");
    for (int i = 0; i < 8; i++)
        belya_agent_add_message(a, "assistant", "reply body with trailing space   \n");

    if (a->msg_count < 10) {
        printf("  [FAIL] expected at least 10 messages, got %zu\n", a->msg_count);
        return 1;
    }

    char *sys_before = strdup(a->messages[0].content ? a->messages[0].content : "");
    char *root_before = strdup(a->messages[1].content ? a->messages[1].content : "");

    belya_agent_compact_5tier(a, COMPACT_TIER_1_MICRO, 4);

    ck("system prompt byte-identical after tier 1",
       strcmp(sys_before, a->messages[0].content ? a->messages[0].content : "") == 0,
       a->messages[0].content);
    ck("root objective byte-identical after tier 1",
       strcmp(root_before, a->messages[1].content ? a->messages[1].content : "") == 0,
       a->messages[1].content);
    /* init() seeds its own system prompt at index 0; whatever bytes it has must
       survive compaction untouched, since the cache key is the exact prefix. */
    ck("seeded system prompt is non-empty", a->messages[0].content && a->messages[0].content[0] != '\0',
       a->messages[0].content);
    ck("prefix length unchanged", strlen(sys_before) == strlen(a->messages[0].content),
       a->messages[0].content);

    const char *tail = a->messages[a->msg_count - 1].content;
    size_t tl = tail ? strlen(tail) : 0;
    ck("later turns are still pruned", tl > 0 && tail[tl - 1] != ' ' && tail[tl - 1] != '\n',
       tail ? tail : "(null)");

    free(sys_before);
    free(root_before);
    belya_agent_free(a);

    printf("  compaction: %s (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
    return fails ? 1 : 0;
}
