/* Unit tests for the markdown -> Telegram HTML renderer (pure function). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../telegram_render.h"

static int g_fail = 0;
static int g_checks = 0;

static void expect(int cond, const char *what) {
    g_checks++;
    if (!cond) {
        g_fail++;
        printf("  [FAIL] %s\n", what);
    }
}

static void expect_contains(const char *hay, const char *needle, const char *what) {
    g_checks++;
    if (!hay || !strstr(hay, needle)) {
        g_fail++;
        printf("  [FAIL] %s\n    got: %s\n", what, hay ? hay : "(null)");
    }
}

static void expect_absent(const char *hay, const char *needle, const char *what) {
    g_checks++;
    if (hay && strstr(hay, needle)) {
        g_fail++;
        printf("  [FAIL] %s\n    got: %s\n", what, hay);
    }
}

int main(void) {
    printf("[Test] Telegram markdown -> HTML renderer\n");

    /* A GFM table: the exact shape Belya emitted to the live chat. */
    const char *table =
        "## SUMMARY\n"
        "| Check | Result |\n"
        "|---|---|\n"
        "| Service | active |\n"
        "| Disk | 75% used |\n"
        "---\n"
        "Done.\n";
    char *h = telegram_render_html(table);
    expect_contains(h, "<b>SUMMARY</b>", "heading becomes bold");
    expect_absent(h, "|", "no pipes survive");
    expect_absent(h, "---", "separator row and hr are dropped");
    expect_contains(h, "\xE2\x80\xA2", "table rows become bullets");
    expect_contains(h, "<b>Service</b>", "first column is bolded");
    expect_contains(h, "active", "cell values survive");
    expect_contains(h, "75% used", "numeric cell survives");
    expect_contains(h, "Done.", "trailing prose survives");
    free(h);

    /* Escaping: the model emits angle brackets and ampersands. */
    h = telegram_render_html("if (a < b && c > d) { x(); }\n");
    expect_contains(h, "&lt;", "< is escaped");
    expect_contains(h, "&amp;", "& is escaped");
    expect_contains(h, "&gt;", "> is escaped");
    free(h);

    /* Inline code and bold. */
    h = telegram_render_html("run `make test` then **verify** it\n");
    expect_contains(h, "<code>make test</code>", "backticks become code tags");
    expect_contains(h, "<b>verify</b>", "double asterisks become bold");
    free(h);

    /* Bullets: "- item" must not keep its ASCII dash. */
    h = telegram_render_html("- one\n- two\n");
    expect_absent(h, "- one", "ascii bullet replaced");
    expect_contains(h, "\xE2\x80\xA2 one", "bullet glyph used");
    free(h);

    /* Fenced code blocks keep their content verbatim (fences are plain here). */
    h = telegram_render_html("```\nls -la | grep x\n```\n");
    expect_contains(h, "ls -la | grep x", "code fence content preserved");
    free(h);

    /* Empty and NULL handling. */
    h = telegram_render_html("");
    expect(h != NULL && h[0] == '\0', "empty input -> empty output");
    free(h);
    expect(telegram_render_html(NULL) == NULL, "NULL input -> NULL");

    /* Regression: a whitespace-only line immediately before a table used to
     * underflow the row length and read past the buffer (ASAN: heap overflow,
     * gdb: SIGSEGV in split_row). */
    h = telegram_render_html("x\n   \n|a|b|\n|---|---|\n|c|d|\n");
    expect(h != NULL, "whitespace-only line before a table does not crash");
    expect_contains(h, "c", "table after a blank line still renders its rows");
    free(h);

    h = telegram_render_html("\n\n| a | b |\n|---|---|\n| c | d |\n");
    expect(h != NULL, "leading blank lines before a table do not crash");
    free(h);

    h = telegram_render_html("\t\t\n| a |\n|---|\n| v |\n");
    expect(h != NULL, "tab-only line before a table does not crash");
    free(h);

    /* Regression: a line that merely starts with a pipe is prose, not a table. */
    h = telegram_render_html("| grep foo\n");
    expect_contains(h, "grep foo", "pipe-led non-table line survives");
    free(h);

    /* Fenced code keeps its pipes and markers verbatim. */
    h = telegram_render_html("```\n| grep foo\nx = **kwargs\n```\n");
    expect_contains(h, "| grep foo", "pipe inside a fence is not eaten");
    expect_contains(h, "**kwargs", "bold markers inside a fence stay literal");
    expect_absent(h, "<b>kwargs</b>", "no markdown inside a fence");
    free(h);

    printf("  telegram renderer: %d checks, %d failures\n", g_checks, g_fail);
    printf("%s\n", g_fail == 0 ? "PASSED" : "FAILED");
    return g_fail == 0 ? 0 : 1;
}
