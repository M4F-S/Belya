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

/* Plan mode: read everything, write nothing outside /tmp and /dev/null.
 * Return: 0 = boundary established, 1 = platform has no such boundary,
 * -1 = the kernel refused a step (caller must NOT run the command unconfined).
 * Every syscall is checked: a boundary that silently fails to apply is worse
 * than none, because callers read 0 as "restricted". */
int sandbox_apply_plan_mode(void);

/* Child-process wrapper: call this instead of sandbox_apply() in a forked child.
 *
 * sandbox_apply() reports failure, but every caller ignored the return value, so
 * a kernel that advertises Landlock and then refuses to confine the process left
 * it running unconfined with no trace. This applies the boundary and decides
 * whether running anyway is legitimate: it returns for a confined child and for
 * one the kernel cannot confine at all (no Landlock, or BELYA_SANDBOX=0), and
 * _exit(126) when a boundary was available and would not take.
 */
void sandbox_confine_child(const char *workspace);

/* The decision behind sandbox_confine_child(), pure so it can be tested:
 * apply_rc is sandbox_apply()'s return value, abi is Landlock's ABI (-1 when the
 * kernel has none). 1 = the child may run unconfined, 0 = it must not. */
int sandbox_allow_unconfined(int apply_rc, int abi);

/* 1 = Landlock usable on this kernel, 0 = not. */
int sandbox_supported(void);

#endif
