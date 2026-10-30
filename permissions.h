#ifndef BELYA_PERMISSIONS_H
#define BELYA_PERMISSIONS_H

#include <stdbool.h>
#include <stddef.h>

/* Tiered tool authorisation.
 *
 * Every registered tool currently ships as PERM_ALLOW, so the ASK path in the
 * dispatch loop can never fire and `bash` is unrestricted. A static per-tool
 * tier cannot tell `ls` from `rm -rf /` anyway, so classification looks at the
 * call itself: the command for bash, the path for file tools.
 *
 * DENY  - refuse, regardless of mode, approver or --yolo.
 * ASK   - needs an approver; what happens without one depends on the mode.
 * ALLOW - run without prompting (safe, read-only, or in-workspace write).
 */
typedef enum {
    PERM_TIER_ALLOW = 0,
    PERM_TIER_ASK,
    PERM_TIER_DENY
} PermTier;

/* PERMISSIVE keeps today's behaviour for unattended callers (Telegram daemon,
 * cron): an ASK with no approver runs and is logged. STRICT turns the same call
 * into a denial instead of a silent stdin prompt. */
typedef enum {
    BELYA_PERM_PERMISSIVE = 0,
    BELYA_PERM_STRICT
} BelyaPermissionMode;

/* Classify a call before execution. Pure: no I/O, no allocation, safe to call
 * from tests. args_json may be NULL. */
PermTier permission_classify(const char *tool_name, const char *args_json);

/* True when `cmd` is a single, un-chained invocation of a read-only or
 * build-only command. Any shell metacharacter disqualifies it. */
bool permission_is_safe_command(const char *cmd);

/* True for paths that must never be written by an agent: VCS internals,
 * shell startup files, ssh material, system directories. */
bool permission_is_protected_path(const char *path);

/* Extract a JSON string field ("command", "path", ...) into buf. Returns false
 * when the key is absent. Kept dependency-free so permissions.c can be tested
 * without the model stack. */
bool permission_arg_string(const char *args_json, const char *key, char *buf, size_t buflen);

const char *permission_tier_name(PermTier tier);

#endif /* BELYA_PERMISSIONS_H */
