#include "linenoise.h"
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

#define LINENOISE_DEFAULT_HISTORY_MAX_LEN 100
#define LINENOISE_MAX_LINE 4096

static linenoiseCompletionCallback *completionCallback = NULL;

static struct termios orig_termios;
static int rawmode = 0;
static int atexit_registered = 0;
static int history_max_len = LINENOISE_DEFAULT_HISTORY_MAX_LEN;
static int history_len = 0;
static char **history = NULL;

struct linenoiseState {
    int ifd;
    int ofd;
    char *buf;
    size_t buflen;
    const char *prompt;
    size_t plen;
    size_t pos;
    size_t oldpos;
    size_t len;
    size_t cols;
    size_t maxrows;
    int history_index;
};

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
    BACKSPACE = 127
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
    linenoiseHistoryFree();
}

static int getColumns(int ifd, int ofd) {
    struct winsize ws;
    (void)ifd;
    (void)ofd;
    if (ioctl(1, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        return 80;
    }
    return ws.ws_col;
}

static size_t linenoisePromptLen(const char *prompt) {
    size_t len = 0;
    while (*prompt) {
        if (*prompt == '\x1b') {
            prompt++;
            if (*prompt == '[') {
                prompt++;
                while (*prompt && ((*prompt >= '0' && *prompt <= '9') || *prompt == ';' || *prompt == '?')) {
                    prompt++;
                }
                if (*prompt) prompt++;
            }
        } else {
            len++;
            prompt++;
        }
    }
    return len;
}

static void refreshLine(struct linenoiseState *l) {
    char seq[64];
    size_t plen = l->plen;
    char *buf = l->buf;
    size_t len = l->len;
    size_t pos = l->pos;

    while ((plen + pos) >= l->cols) {
        buf++;
        len--;
        pos--;
    }
    while (plen + len > l->cols) {
        len--;
    }

    // Cursor to left edge
    snprintf(seq, sizeof(seq), "\r");
    if (write(l->ofd, seq, strlen(seq)) == -1) return;
    // Write prompt and buffer
    if (write(l->ofd, l->prompt, strlen(l->prompt)) == -1) return;
    if (write(l->ofd, buf, len) == -1) return;
    // Erase to right
    snprintf(seq, sizeof(seq), "\x1b[0K");
    if (write(l->ofd, seq, strlen(seq)) == -1) return;
    // Move cursor to original position
    snprintf(seq, sizeof(seq), "\r\x1b[%dC", (int)(pos + plen));
    if (write(l->ofd, seq, strlen(seq)) == -1) return;
}

static int linenoiseEditInsert(struct linenoiseState *l, char c) {
    if (l->len < l->buflen) {
        if (l->len == l->pos) {
            l->buf[l->pos] = c;
            l->pos++;
            l->len++;
            l->buf[l->len] = '\0';
            refreshLine(l);
        } else {
            memmove(l->buf + l->pos + 1, l->buf + l->pos, l->len - l->pos);
            l->buf[l->pos] = c;
            l->len++;
            l->pos++;
            l->buf[l->len] = '\0';
            refreshLine(l);
        }
    }
    return 0;
}

static void linenoiseEditMoveLeft(struct linenoiseState *l) {
    if (l->pos > 0) {
        l->pos--;
        refreshLine(l);
    }
}

static void linenoiseEditMoveRight(struct linenoiseState *l) {
    if (l->pos != l->len) {
        l->pos++;
        refreshLine(l);
    }
}

static void linenoiseEditMoveHome(struct linenoiseState *l) {
    if (l->pos != 0) {
        l->pos = 0;
        refreshLine(l);
    }
}

static void linenoiseEditMoveEnd(struct linenoiseState *l) {
    if (l->pos != l->len) {
        l->pos = l->len;
        refreshLine(l);
    }
}

static void linenoiseEditBackspace(struct linenoiseState *l) {
    if (l->pos > 0 && l->len > 0) {
        memmove(l->buf + l->pos - 1, l->buf + l->pos, l->len - l->pos);
        l->pos--;
        l->len--;
        l->buf[l->len] = '\0';
        refreshLine(l);
    }
}

static void linenoiseEditDelete(struct linenoiseState *l) {
    if (l->len > 0 && l->pos < l->len) {
        memmove(l->buf + l->pos, l->buf + l->pos + 1, l->len - l->pos - 1);
        l->len--;
        l->buf[l->len] = '\0';
        refreshLine(l);
    }
}

static void linenoiseEditHistoryNext(struct linenoiseState *l, int dir) {
    if (history_len > 1) {
        free(history[history_len - 1 - l->history_index]);
        history[history_len - 1 - l->history_index] = strdup(l->buf);
        l->history_index += (dir == 1) ? 1 : -1;
        if (l->history_index < 0) {
            l->history_index = 0;
            return;
        } else if (l->history_index >= history_len) {
            l->history_index = history_len - 1;
            return;
        }
        strncpy(l->buf, history[history_len - 1 - l->history_index], l->buflen);
        l->buf[l->buflen - 1] = '\0';
        l->len = l->pos = strlen(l->buf);
        refreshLine(l);
    }
}

static int completeLine(struct linenoiseState *ls) {
    linenoiseCompletions lc = { 0, NULL };
    int nread, nwritten;
    char c = 0;

    completionCallback(ls->buf, &lc);
    if (lc.len == 0) {
        // No completion
    } else {
        size_t stop = 0, i = 0;

        while (!stop) {
            if (i < lc.len) {
                struct linenoiseState saved = *ls;
                ls->len = ls->pos = strlen(lc.cvec[i]);
                ls->buf = lc.cvec[i];
                refreshLine(ls);
                ls->len = saved.len;
                ls->pos = saved.pos;
                ls->buf = saved.buf;
            } else {
                refreshLine(ls);
            }

            nread = read(ls->ifd, &c, 1);
            if (nread <= 0) {
                break;
            }

            switch (c) {
                case TAB:
                    i = (i + 1) % (lc.len + 1);
                    break;
                case ESC:
                    if (i < lc.len) refreshLine(ls);
                    stop = 1;
                    break;
                default:
                    if (i < lc.len) {
                        nwritten = snprintf(ls->buf, ls->buflen, "%s", lc.cvec[i]);
                        ls->len = ls->pos = nwritten;
                    }
                    stop = 1;
                    break;
            }
        }
    }

    for (size_t j = 0; j < lc.len; j++) free(lc.cvec[j]);
    if (lc.cvec) free(lc.cvec);
    return c;
}

void linenoiseSetCompletionCallback(linenoiseCompletionCallback *fn) {
    completionCallback = fn;
}

void linenoiseAddCompletion(linenoiseCompletions *lc, const char *str) {
    size_t len = strlen(str);
    char *copy = malloc(len + 1);
    if (!copy) return;
    memcpy(copy, str, len + 1);
    lc->cvec = realloc(lc->cvec, sizeof(char *) * (lc->len + 1));
    lc->cvec[lc->len++] = copy;
}

static int linenoiseEdit(int stdin_fd, int stdout_fd, char *buf, size_t buflen, const char *prompt) {
    struct linenoiseState l;

    l.ifd = stdin_fd;
    l.ofd = stdout_fd;
    l.buf = buf;
    l.buflen = buflen;
    l.prompt = prompt;
    l.plen = linenoisePromptLen(prompt);
    l.oldpos = l.pos = 0;
    l.len = 0;
    l.cols = getColumns(stdin_fd, stdout_fd);
    l.maxrows = 0;
    l.history_index = 0;

    l.buf[0] = '\0';
    l.buflen--;

    linenoiseHistoryAdd("");

    if (write(l.ofd, prompt, strlen(prompt)) == -1) return -1;
    while (1) {
        char c;
        int nread = read(l.ifd, &c, 1);
        if (nread <= 0) return l.len;

        if (c == TAB && completionCallback != NULL) {
            c = completeLine(&l);
            if (c < 0) return l.len;
            if (c == 0) continue;
        }

        switch (c) {
            case ENTER:
                history_len--;
                free(history[history_len]);
                return (int)l.len;
            case CTRL_C:
                errno = EAGAIN;
                return -1;
            case BACKSPACE:
            case CTRL_H:
                linenoiseEditBackspace(&l);
                break;
            case CTRL_D:
                if (l.len > 0) {
                    linenoiseEditDelete(&l);
                } else {
                    history_len--;
                    free(history[history_len]);
                    return -1;
                }
                break;
            case CTRL_T:
                if (l.pos > 0 && l.pos < l.len) {
                    char aux = buf[l.pos - 1];
                    buf[l.pos - 1] = buf[l.pos];
                    buf[l.pos] = aux;
                    if (l.pos != l.len - 1) l.pos++;
                    refreshLine(&l);
                }
                break;
            case CTRL_B:
                linenoiseEditMoveLeft(&l);
                break;
            case CTRL_F:
                linenoiseEditMoveRight(&l);
                break;
            case CTRL_P:
                linenoiseEditHistoryNext(&l, 1);
                break;
            case CTRL_N:
                linenoiseEditHistoryNext(&l, 0);
                break;
            case ESC: {
                char seq[3];
                if (read(l.ifd, seq, 1) == 0) break;
                if (read(l.ifd, seq + 1, 1) == 0) break;
                if (seq[0] == '[') {
                    if (seq[1] >= '0' && seq[1] <= '9') {
                        if (read(l.ifd, seq + 2, 1) == 0) break;
                        if (seq[2] == '~') {
                            if (seq[1] == '3') linenoiseEditDelete(&l);
                        }
                    } else {
                        switch (seq[1]) {
                            case 'A': linenoiseEditHistoryNext(&l, 1); break; // Up
                            case 'B': linenoiseEditHistoryNext(&l, 0); break; // Down
                            case 'C': linenoiseEditMoveRight(&l); break;      // Right
                            case 'D': linenoiseEditMoveLeft(&l); break;       // Left
                            case 'H': linenoiseEditMoveHome(&l); break;       // Home
                            case 'F': linenoiseEditMoveEnd(&l); break;        // End
                        }
                    }
                }
                break;
            }
            case CTRL_U:
                buf[0] = '\0';
                l.pos = l.len = 0;
                refreshLine(&l);
                break;
            case CTRL_K:
                buf[l.pos] = '\0';
                l.len = l.pos;
                refreshLine(&l);
                break;
            case CTRL_A:
                linenoiseEditMoveHome(&l);
                break;
            case CTRL_E:
                linenoiseEditMoveEnd(&l);
                break;
            case CTRL_L:
                linenoiseClearScreen();
                refreshLine(&l);
                break;
            case CTRL_W:
                while (l.pos > 0 && buf[l.pos - 1] == ' ') linenoiseEditBackspace(&l);
                while (l.pos > 0 && buf[l.pos - 1] != ' ') linenoiseEditBackspace(&l);
                break;
            default:
                if (linenoiseEditInsert(&l, c)) return -1;
                break;
        }
    }
    return l.len;
}

static char *linenoiseRaw(const char *prompt) {
    char buf[LINENOISE_MAX_LINE];
    int count;

    if (!isatty(STDIN_FILENO)) {
        if (fgets(buf, LINENOISE_MAX_LINE, stdin) == NULL) return NULL;
        size_t len = strlen(buf);
        while (len && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
            len--;
            buf[len] = '\0';
        }
        return strdup(buf);
    }

    if (enableRawMode(STDIN_FILENO) == -1) return NULL;
    count = linenoiseEdit(STDIN_FILENO, STDOUT_FILENO, buf, LINENOISE_MAX_LINE, prompt);
    disableRawMode(STDIN_FILENO);
    printf("\n");
    if (count == -1) return NULL;
    return strdup(buf);
}

char *linenoise(const char *prompt) {
    return linenoiseRaw(prompt);
}

void linenoiseFree(void *ptr) {
    free(ptr);
}

void linenoiseClearScreen(void) {
    if (write(STDOUT_FILENO, "\x1b[H\x1b[2J", 7) <= 0) {
        // Ignore error
    }
}

int linenoiseHistoryAdd(const char *line) {
    char *linecopy;

    if (history_max_len == 0) return 0;
    if (history == NULL) {
        history = malloc(sizeof(char *) * history_max_len);
        if (history == NULL) return 0;
        memset(history, 0, (sizeof(char *) * history_max_len));
    }
    if (history_len && !strcmp(history[history_len - 1], line)) return 0;
    linecopy = strdup(line);
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

int linenoiseHistorySetMaxLen(int len) {
    if (len < 1) return 0;
    if (history) {
        int tocopy = history_len;
        char **newHistory = malloc(sizeof(char *) * len);
        if (newHistory == NULL) return 0;
        if (len < tocopy) tocopy = len;
        memcpy(newHistory, history + (history_len - tocopy), sizeof(char *) * tocopy);
        free(history);
        history = newHistory;
    }
    history_max_len = len;
    if (history_len > len) history_len = len;
    return 1;
}

void linenoiseHistoryFree(void) {
    if (history) {
        for (int j = 0; j < history_len; j++) free(history[j]);
        free(history);
        history = NULL;
        history_len = 0;
    }
}

int linenoiseHistorySave(const char *filename) {
    FILE *fp = fopen(filename, "w");
    if (!fp) return -1;
    for (int j = 0; j < history_len; j++) {
        if (strlen(history[j]) > 0) fprintf(fp, "%s\n", history[j]);
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
        if (strlen(buf) > 0) linenoiseHistoryAdd(buf);
    }
    fclose(fp);
    return 0;
}
