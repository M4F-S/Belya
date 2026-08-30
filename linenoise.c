#define _POSIX_C_SOURCE 200809L
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "linenoise.h"

#define LINENOISE_DEFAULT_HISTORY_MAX_LEN 100
#define LINENOISE_MAX_LINE 4096

static linenoiseCompletionCallback *completionCallback = NULL;
static linenoiseHintsCallback *hintsCallback = NULL;
static linenoiseFreeHintsCallback *freeHintsCallback = NULL;

static struct termios orig_termios;
static int rawmode = 0;
static int atexit_registered = 0;
static int history_max_len = LINENOISE_DEFAULT_HISTORY_MAX_LEN;
static int history_len = 0;
static char **history = NULL;

enum KEY_ACTION {
    KEY_NULL = 0,
    CTRL_A = 1,
    CTRL_B = 2,
    CTRL_C = 3,
    CTRL_D = 4,
    CTRL_E = 5,
    CTRL_F = 6,
    CTRL_H = 8,
    TAB = 9,
    CTRL_K = 11,
    CTRL_L = 12,
    ENTER = 13,
    CTRL_N = 14,
    CTRL_P = 16,
    CTRL_T = 20,
    CTRL_U = 21,
    CTRL_W = 23,
    ESC = 27,
    BACKSPACE =  127
};

static void linenoiseAtExit(void);

static int enableRawMode(int fd) {
    struct termios raw;
    if (!isatty(STDIN_FILENO)) goto fatal;
    if (!atexit_registered) {
        atexit(linenoiseAtExit);
        atexit_registered = 1;
    }
    if (tcgetattr(fd, &orig_termios) == -1) goto fatal;

    raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSAFLUSH, &raw) < 0) goto fatal;
    rawmode = 1;
    return 0;

fatal:
    errno = ENOTTY;
    return -1;
}

static void disableRawMode(int fd) {
    if (rawmode && tcsetattr(fd, TCSAFLUSH, &orig_termios) != -1)
        rawmode = 0;
}

static void linenoiseAtExit(void) {
    disableRawMode(STDIN_FILENO);
}

static int getColumns(int ifd, int ofd) {
    struct winsize ws;
    if (ioctl(1, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        (void)ifd; (void)ofd;
        return 80;
    }
    return ws.ws_col;
}

struct current {
    char *buf;
    size_t buflen;
    const char *prompt;
    size_t plen;
    size_t pos;
    size_t len;
    size_t cols;
    size_t maxrows;
    int history_index;
};

static void refreshLine(struct current *c) {
    char seq[64];
    size_t plen = c->plen;
    char *buf = c->buf;
    size_t len = c->len;
    size_t pos = c->pos;

    while ((plen + pos) >= c->cols) {
        buf++;
        len--;
        pos--;
    }
    while (plen + len > c->cols) {
        len--;
    }

    char out[LINENOISE_MAX_LINE + 128];
    int out_len = 0;

    // Cursor to left edge
    out_len += snprintf(out + out_len, sizeof(out) - out_len, "\r");
    // Write prompt
    out_len += snprintf(out + out_len, sizeof(out) - out_len, "%s", c->prompt);
    // Write buffer
    out_len += snprintf(out + out_len, sizeof(out) - out_len, "%.*s", (int)len, buf);
    // Erase to right
    out_len += snprintf(out + out_len, sizeof(out) - out_len, "\x1b[0K");
    // Move cursor to original position
    snprintf(seq, sizeof(seq), "\r\x1b[%dC", (int)(pos + c->plen));
    out_len += snprintf(out + out_len, sizeof(out) - out_len, "%s", seq);

    if (write(STDOUT_FILENO, out, out_len) == -1) {}
}

static int linenoiseEditInsert(struct current *c, char ch) {
    if (c->len < c->buflen) {
        if (c->len == c->pos) {
            c->buf[c->pos] = ch;
            c->pos++;
            c->len++;
            c->buf[c->len] = '\0';
            refreshLine(c);
        } else {
            memmove(c->buf + c->pos + 1, c->buf + c->pos, c->len - c->pos);
            c->buf[c->pos] = ch;
            c->len++;
            c->pos++;
            c->buf[c->len] = '\0';
            refreshLine(c);
        }
    }
    return 0;
}

static void linenoiseEditMoveLeft(struct current *c) {
    if (c->pos > 0) {
        c->pos--;
        refreshLine(c);
    }
}

static void linenoiseEditMoveRight(struct current *c) {
    if (c->pos != c->len) {
        c->pos++;
        refreshLine(c);
    }
}

static void linenoiseEditMoveHome(struct current *c) {
    if (c->pos != 0) {
        c->pos = 0;
        refreshLine(c);
    }
}

static void linenoiseEditMoveEnd(struct current *c) {
    if (c->pos != c->len) {
        c->pos = c->len;
        refreshLine(c);
    }
}

static void linenoiseEditBackspace(struct current *c) {
    if (c->pos > 0 && c->len > 0) {
        memmove(c->buf + c->pos - 1, c->buf + c->pos, c->len - c->pos);
        c->pos--;
        c->len--;
        c->buf[c->len] = '\0';
        refreshLine(c);
    }
}

static void linenoiseEditDelete(struct current *c) {
    if (c->len > 0 && c->pos < c->len) {
        memmove(c->buf + c->pos, c->buf + c->pos + 1, c->len - c->pos - 1);
        c->len--;
        c->buf[c->len] = '\0';
        refreshLine(c);
    }
}

static void linenoiseEditHistoryNext(struct current *c, int dir) {
    if (history_len > 1) {
        free(history[history_len - 1 - c->history_index]);
        history[history_len - 1 - c->history_index] = strdup(c->buf);
        c->history_index += (dir == 1) ? 1 : -1;
        if (c->history_index < 0) {
            c->history_index = 0;
            return;
        } else if (c->history_index >= history_len) {
            c->history_index = history_len - 1;
            return;
        }
        strncpy(c->buf, history[history_len - 1 - c->history_index], c->buflen);
        c->buf[c->buflen - 1] = '\0';
        c->len = c->pos = strlen(c->buf);
        refreshLine(c);
    }
}

static int completeLine(struct current *c) {
    linenoiseCompletions lc = { 0, NULL };
    int c_char = 0;

    if (completionCallback == NULL) return 0;
    completionCallback(c->buf, &lc);
    if (lc.len == 0) {
        // No completion
    } else {
        size_t stop = 0, i = 0;
        while (!stop) {
            if (i < lc.len) {
                struct current saved = *c;
                c->len = c->pos = strlen(lc.cvec[i]);
                c->buf = lc.cvec[i];
                refreshLine(c);
                c->len = saved.len;
                c->pos = saved.pos;
                c->buf = saved.buf;
            } else {
                refreshLine(c);
            }

            char ch;
            if (read(STDIN_FILENO, &ch, 1) <= 0) return -1;
            switch (ch) {
                case TAB:
                    i = (i + 1) % (lc.len + 1);
                    break;
                case ESC:
                    if (i < lc.len) refreshLine(c);
                    stop = 1;
                    break;
                default:
                    if (i < lc.len) {
                        int nwritten = snprintf(c->buf, c->buflen, "%s", lc.cvec[i]);
                        c->len = c->pos = (size_t)nwritten;
                    }
                    stop = 1;
                    c_char = ch;
                    break;
            }
        }
    }

    for (size_t j = 0; j < lc.len; j++) free(lc.cvec[j]);
    if (lc.cvec) free(lc.cvec);
    return c_char;
}

static int linenoiseEdit(int stdin_fd, int stdout_fd, char *buf, size_t buflen, const char *prompt) {
    struct current c;
    c.buf = buf;
    c.buflen = buflen;
    c.prompt = prompt;
    c.plen = strlen(prompt);
    c.pos = 0;
    c.len = 0;
    c.cols = getColumns(stdin_fd, stdout_fd);
    c.maxrows = 0;
    c.history_index = 0;

    c.buf[0] = '\0';
    c.buflen--;

    linenoiseHistoryAdd("");

    if (write(stdout_fd, prompt, c.plen) == -1) return -1;

    while (1) {
        char ch;
        int nread = read(stdin_fd, &ch, 1);
        if (nread <= 0) return c.len;

        if (ch == TAB && completionCallback != NULL) {
            int c_char = completeLine(&c);
            if (c_char < 0) return c.len;
            if (c_char == 0) continue;
            ch = (char)c_char;
        }

        switch (ch) {
            case ENTER:
                history_len--;
                free(history[history_len]);
                return (int)c.len;
            case CTRL_C:
                errno = EAGAIN;
                return -1;
            case BACKSPACE:
            case CTRL_H:
                linenoiseEditBackspace(&c);
                break;
            case CTRL_D:
                if (c.len > 0) {
                    linenoiseEditDelete(&c);
                } else {
                    history_len--;
                    free(history[history_len]);
                    return -1;
                }
                break;
            case CTRL_U:
                c.buf[0] = '\0';
                c.pos = c.len = 0;
                refreshLine(&c);
                break;
            case CTRL_A:
                linenoiseEditMoveHome(&c);
                break;
            case CTRL_E:
                linenoiseEditMoveEnd(&c);
                break;
            case ESC: {
                char seq[3];
                if (read(stdin_fd, seq, 1) == 0) break;
                if (read(stdin_fd, seq + 1, 1) == 0) break;

                if (seq[0] == '[') {
                    if (seq[1] >= '0' && seq[1] <= '9') {
                        // Extended seq like Delete key [3~
                        if (read(stdin_fd, seq + 2, 1) == 0) break;
                        if (seq[1] == '3' && seq[2] == '~') {
                            linenoiseEditDelete(&c);
                        }
                    } else {
                        switch (seq[1]) {
                            case 'A': // Up
                                linenoiseEditHistoryNext(&c, 1);
                                break;
                            case 'B': // Down
                                linenoiseEditHistoryNext(&c, 0);
                                break;
                            case 'C': // Right
                                linenoiseEditMoveRight(&c);
                                break;
                            case 'D': // Left
                                linenoiseEditMoveLeft(&c);
                                break;
                            case 'H': // Home
                                linenoiseEditMoveHome(&c);
                                break;
                            case 'F': // End
                                linenoiseEditMoveEnd(&c);
                                break;
                        }
                    }
                }
                break;
            }
            default:
                if (linenoiseEditInsert(&c, ch)) return -1;
                break;
        }
    }
    return c.len;
}

static char *linenoiseRaw(const char *prompt) {
    char buf[LINENOISE_MAX_LINE];
    int count;

    if (enableRawMode(STDIN_FILENO) == -1) return NULL;
    count = linenoiseEdit(STDIN_FILENO, STDOUT_FILENO, buf, LINENOISE_MAX_LINE, prompt);
    disableRawMode(STDIN_FILENO);
    printf("\n");
    if (count == -1) return NULL;
    return strdup(buf);
}

char *linenoise(const char *prompt) {
    if (!isatty(STDIN_FILENO)) {
        char buf[LINENOISE_MAX_LINE];
        if (fgets(buf, sizeof(buf), stdin) == NULL) return NULL;
        buf[strcspn(buf, "\r\n")] = '\0';
        return strdup(buf);
    }
    return linenoiseRaw(prompt);
}

void linenoiseFree(void *ptr) {
    if (ptr) free(ptr);
}

void linenoiseSetCompletionCallback(linenoiseCompletionCallback *fn) {
    completionCallback = fn;
}

void linenoiseSetHintsCallback(linenoiseHintsCallback *fn) {
    hintsCallback = fn;
}

void linenoiseSetFreeHintsCallback(linenoiseFreeHintsCallback *fn) {
    freeHintsCallback = fn;
}

void linenoiseAddCompletion(linenoiseCompletions *lc, const char *str) {
    size_t len = strlen(str);
    char *copy = malloc(len + 1);
    if (!copy) return;
    memcpy(copy, str, len + 1);
    lc->cvec = realloc(lc->cvec, sizeof(char *) * (lc->len + 1));
    lc->cvec[lc->len++] = copy;
}

int linenoiseHistoryAdd(const char *line) {
    if (history_max_len == 0) return 0;
    if (history == NULL) {
        history = malloc(sizeof(char *) * history_max_len);
        if (history == NULL) return 0;
        memset(history, 0, sizeof(char *) * history_max_len);
    }
    if (history_len && !strcmp(history[history_len - 1], line)) return 0;

    char *linecopy = strdup(line);
    if (!linecopy) return 0;
    if (history_len == history_max_len) {
        free(history[0]);
        memmove(history, history + 1, sizeof(char *) * (history_max_len - 1));
        history_len--;
    }
    history[history_len] = linecopy;
    history_len++;
    return 1;
}

int linenoiseHistorySave(const char *filename) {
    FILE *fp = fopen(filename, "w");
    if (!fp) return -1;
    for (int j = 0; j < history_len; j++) {
        fprintf(fp, "%s\n", history[j]);
    }
    fclose(fp);
    return 0;
}

int linenoiseHistoryLoad(const char *filename) {
    FILE *fp = fopen(filename, "r");
    char buf[LINENOISE_MAX_LINE];
    if (!fp) return -1;
    while (fgets(buf, LINENOISE_MAX_LINE, fp) != NULL) {
        char *p = strchr(buf, '\r');
        if (!p) p = strchr(buf, '\n');
        if (p) *p = '\0';
        linenoiseHistoryAdd(buf);
    }
    fclose(fp);
    return 0;
}
