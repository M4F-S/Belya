#ifndef BELYA_SANDBOX_H
#define BELYA_SANDBOX_H

/*
 * Kernel-level write confinement for tool subprocesses.
 *
 * The permission classifier decides intent; this decides capability. A child
 * that calls sandbox_apply() can only create, modify or delete files beneath
 * the workspace and /tmp, whatever the command string says. Reads and execs are
 * untouched, so compilers and shells still work.
 *
 * Call in the forked child, after redirecting fds, immediately before exec.
 * Returns 0 when confined, 1 when already/unsupported (caller continues), and
 * -1 on a hard error.
 */
int sandbox_apply(const char *workspace);

/* 1 = Landlock usable on this kernel, 0 = not. */
int sandbox_supported(void);

#endif
