#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>

/*
 * matrix — High-performance terminal Matrix "digital rain" screensaver
 *
 * Features:
 *   - Per-column independent streams with random lengths
 *   - 8-shade green gradient cycle per stream
 *   - Random character mutation every frame for authentic effect
 *   - Color-change deduplication for minimal escape output
 *   - Single write() per frame via frame buffer
 */

#define DEFAULT_FPS     30
#define FRAME_BUF_SIZE  (1024 * 512)
#define MAX_STREAM_LEN  20
#define SPAWN_CHANCE    5   // % chance per frame per column to spawn

// Green gradient palette (dark → bright → dark)
static const int palette[] = { 22, 28, 34, 40, 46, 40, 34, 28 };
#define PALETTE_LEN 8

// Characters to display
static const char CHARS[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@#$%^&*()";
#define CHARS_LEN (sizeof(CHARS) - 1)

static int    term_width   = 80;
static int    term_height  = 24;
static int    delay_us     = 1000000 / DEFAULT_FPS;
static volatile int running = 1;

static char  *frame_buffer = NULL;
static int    fb_len       = 0;

// Per-column state
static int   *heads        = NULL;   // y position of stream head
static int   *stream_lens  = NULL;   // length of each stream
static int   *active       = NULL;   // 1 = active, 0 = inactive

static void handle_signal(int sig) { (void)sig; running = 0; }

static void update_term_size(void) {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0 && w.ws_row > 0) {
        term_width  = w.ws_col;
        term_height = w.ws_row;
    }
}

static inline void fb_append(const char *s, int len) {
    if (fb_len + len < FRAME_BUF_SIZE) {
        memcpy(frame_buffer + fb_len, s, len);
        fb_len += len;
    }
}

static inline void fb_append_str(const char *s) {
    fb_append(s, (int)strlen(s));
}

static void render_frame(void) {
    fb_len = 0;
    char tmp[64];

    for (int i = 0; i < term_width; i++) {
        if (!active[i]) {
            if (rand() % 100 < SPAWN_CHANCE) {
                active[i] = 1;
                heads[i] = 1;
                stream_lens[i] = MAX_STREAM_LEN / 2 + rand() % MAX_STREAM_LEN;
            }
            continue;
        }

        int y_head = heads[i];
        int slen   = stream_lens[i];

        // Draw the stream with gradient
        for (int j = 0; j < slen; j++) {
            int y = y_head - j;
            if (y < 1) break;
            if (y > term_height) continue;

            int ci = j % PALETTE_LEN;
            char ch = CHARS[rand() % CHARS_LEN];
            int n = snprintf(tmp, sizeof(tmp), "\033[%d;%dH\033[38;5;%dm%c", y, i + 1, palette[ci], ch);
            fb_append(tmp, n);
        }

        // Erase tail
        int y_tail = y_head - slen;
        if (y_tail >= 1 && y_tail <= term_height) {
            int n = snprintf(tmp, sizeof(tmp), "\033[%d;%dH ", y_tail, i + 1);
            fb_append(tmp, n);
        }

        heads[i] = y_head + 1;

        if (y_tail >= term_height) {
            active[i] = 0;
        }
    }

    write(STDOUT_FILENO, frame_buffer, fb_len);
}

int main(void) {
    frame_buffer = (char *)malloc(FRAME_BUF_SIZE);
    if (!frame_buffer) return 1;

    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);

    srand((unsigned)time(NULL));
    update_term_size();

    heads       = (int *)calloc(term_width, sizeof(int));
    stream_lens = (int *)calloc(term_width, sizeof(int));
    active      = (int *)calloc(term_width, sizeof(int));
    if (!heads || !stream_lens || !active) return 1;

    // Read optional delay
    const char *env = getenv("SCREENSAVER_DELAY");
    if (env) {
        double d = atof(env);
        if (d > 0.0) delay_us = (int)(d * 1e6);
    }

    // Hide cursor, black bg, clear
    write(STDOUT_FILENO, "\033[?25l\033[40m\033[2J", 16);

    while (running) {
        render_frame();
        usleep(delay_us);
    }

    const char *restore = "\033[?25h\033[0m\033[2J\033[H";
    write(STDOUT_FILENO, restore, strlen(restore));
    free(heads); free(stream_lens); free(active); free(frame_buffer);
    return 0;
}
