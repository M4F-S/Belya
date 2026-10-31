#ifndef TELEGRAM_RENDER_H
#define TELEGRAM_RENDER_H

/*
 * Markdown -> Telegram HTML.
 *
 * The agent writes GitHub-flavoured markdown. Telegram renders none of it: a
 * GFM table arrives as a wall of pipes, "## Heading" as literal hashes. This
 * converts the dialect the model actually emits into the subset Telegram's
 * HTML parse mode accepts.
 *
 * Returns a malloc'd string the caller owns. Never returns NULL for non-NULL
 * input (falls back to an escaped copy).
 */
char *telegram_render_html(const char *markdown);

#endif
