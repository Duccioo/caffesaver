#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>

/*
 * perlin-pixel — High-performance grayscale noise screensaver
 *
 * Features:
 *   - 16x10 noise grid with bilinear interpolation
 *   - 5-shade grayscale posterized palette for contour-line look
 *   - Half-width resolution with double-space for square pixels
 *   - Color-change deduplication for minimal escape sequences
 *   - Smooth easing between target noise values
 */

#define DEFAULT_FPS     30
#define FRAME_BUF_SIZE  (1024 * 512)
#define GRID_W          16
#define GRID_H          10

typedef struct { int current, target; } Cell;

static Cell   grid[GRID_W * GRID_H];
static int    term_width   = 80;
static int    term_height  = 24;
static int    delay_us     = 1000000 / DEFAULT_FPS;
static volatile int running = 1;
static char  *frame_buffer = NULL;
static int    fb_len       = 0;

// 5-shade grayscale palette
static const int palette[] = { 232, 237, 242, 247, 252 };
#define PALETTE_LEN 5

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

static void init_grid(void) {
    for (int i = 0; i < GRID_W * GRID_H; i++) {
        grid[i].current = rand() % 256;
        grid[i].target  = rand() % 256;
    }
}

static void update_grid(void) {
    for (int i = 0; i < GRID_W * GRID_H; i++) {
        int diff = grid[i].target - grid[i].current;
        if (diff == 0) {
            grid[i].target = rand() % 256;
        } else {
            int step = diff / 16;
            if (step == 0) step = (diff > 0) ? 1 : -1;
            grid[i].current += step;
        }
    }
}

static void render_frame(void) {
    update_term_size();

    int calc_width = term_width / 2;
    if (calc_width < 1) calc_width = 1;

    fb_len = 0;
    fb_append_str("\033[H");

    char tmp[64];
    int prev_color = -1;

    for (int y = 0; y < term_height; y++) {
        int y_sc = (y * (GRID_H - 1) * 1000) / term_height;
        int gy   = y_sc / 1000;
        int ry   = y_sc % 1000;
        int r1   = gy * GRID_W;
        int r2   = (gy + 1 < GRID_H ? gy + 1 : gy) * GRID_W;

        for (int x = 0; x < calc_width; x++) {
            int x_sc = (x * (GRID_W - 1) * 1000) / calc_width;
            int gx   = x_sc / 1000;
            int rx   = x_sc % 1000;

            int gx1 = gx + 1 < GRID_W ? gx + 1 : gx;

            int v_tl = grid[r1 + gx].current;
            int v_bl = grid[r2 + gx].current;
            int v_left = v_tl + ((v_bl - v_tl) * ry) / 1000;

            int v_tr = grid[r1 + gx1].current;
            int v_br = grid[r2 + gx1].current;
            int v_right = v_tr + ((v_br - v_tr) * ry) / 1000;

            int val = v_left + ((v_right - v_left) * rx) / 1000;

            int p_idx = (val * PALETTE_LEN) / 256;
            if (p_idx >= PALETTE_LEN) p_idx = PALETTE_LEN - 1;
            if (p_idx < 0) p_idx = 0;

            int color = palette[p_idx];
            if (color != prev_color) {
                int n = snprintf(tmp, sizeof(tmp), "\033[48;5;%dm", color);
                fb_append(tmp, n);
                prev_color = color;
            }
            fb_append("  ", 2);
        }

        // Handle odd terminal width
        if (term_width & 1) {
            fb_append(" ", 1);
        }

        fb_append_str("\033[0m");
        prev_color = -1;
        if (y < term_height - 1) fb_append("\n", 1);
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
    init_grid();

    const char *env = getenv("SCREENSAVER_DELAY");
    if (env) {
        double d = atof(env);
        if (d > 0.0) delay_us = (int)(d * 1e6);
    }

    write(STDOUT_FILENO, "\033[?25l\033[2J", 10);

    while (running) {
        update_grid();
        render_frame();
        usleep(delay_us);
    }

    const char *restore = "\033[?25h\033[0m\033[2J\033[H";
    write(STDOUT_FILENO, restore, strlen(restore));
    free(frame_buffer);
    return 0;
}
