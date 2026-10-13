#ifndef BELYA_COMMON_H
#define BELYA_COMMON_H

/*
 * belya_common.h - POSIX.1-2008 Feature-Test Prelude & Platform Invariants
 * Enforces pure ANSI C99 / POSIX.1-2008 compliance across macOS and Linux.
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <poll.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/socket.h>

#if defined(__APPLE__)
#include <util.h>
#elif defined(__linux__)
#include <pty.h>
#endif

/* Linux Landlock LSM detection & graceful degradation */
#if defined(__linux__) && defined(__NR_landlock_create_ruleset)
#include <linux/landlock.h>
#include <sys/syscall.h>
#define BELYA_HAS_LANDLOCK 1
#else
#define BELYA_HAS_LANDLOCK 0
#endif

/* Core Architectural Boundaries & Capacity Invariants */
#define BELYA_MAX_BACKGROUND_TASKS 16
#define BELYA_TASK_RING_CAPACITY   65536   /* 64 KB per task = 1 MB max memory */
#define BELYA_MAX_NOTEBOOK_BYTES   (16 * 1024 * 1024) /* 16 MB max .ipynb limit */
#define BELYA_DYNAMIC_BOUNDARY_MARKER "\n--- SYSTEM_PROMPT_DYNAMIC_BOUNDARY ---\n"

/* Unused attribute helper */
#if defined(__GNUC__) || defined(__clang__)
#define BELYA_UNUSED __attribute__((unused))
#else
#define BELYA_UNUSED
#endif

#endif /* BELYA_COMMON_H */
