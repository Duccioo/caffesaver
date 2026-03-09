#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>

/*
 * dunes — High-performance terminal sand dunes screensaver
 *
 * Features:
 *   - 3D Perlin-ish noise with time evolution for flowing dunes
 *   - 12-shade warm sandy palette (cream → amber → deep orange)
 *   - Bilinear interpolation across the screen
 *   - Uses foreground-colored '.' characters for textured look
 *   - Configurable speed via environment
 */

#define DEFAULT_FPS     30
#define FRAME_BUF_SIZE  (1024 * 512)
#define GRID_W          16
#define GRID_H          12
#define SPEED           5

static int    term_width   = 80;
static int    term_height  = 24;
static int    delay_us     = 1000000 / DEFAULT_FPS;
static volatile int running = 1;

static char  *frame_buffer = NULL;
static int    fb_len       = 0;

static int noise_grid[GRID_W * GRID_H];

// Warm sandy palette (12 shades)
static const int palette[] = { 230, 229, 228, 222, 221, 220, 215, 214, 209, 208, 203, 202 };
#define PALETTE_LEN 12

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

/* Deterministic hash-based pseudo-noise (replaces bash RANDOM seeding trick) */
static int hash_noise(int x, int y, int z) {
    /* Simple integer hash */
    int n = x * 374761393 + y * 668265263 + z * 1274126177;
    n = (n ^ (n >> 13)) * 1274126177;
    n = n ^ (n >> 16);
    return ((n & 0x7FFFFFFF) % 256);
}

/* 3D Perlin-ish noise via trilinear interpolation of hashed corners */
static int pnoise(int x, int y, int z) {
    int gx = x / 8, gy = y / 8, gz = z / 8;
    int dx = (x % 8) * 32, dy = (y % 8) * 32, dz = (z % 8) * 32;

    int c000 = hash_noise(gx,   gy,   gz);
    int c100 = hash_noise(gx+1, gy,   gz);
    int c010 = hash_noise(gx,   gy+1, gz);
    int c110 = hash_noise(gx+1, gy+1, gz);
    int c001 = hash_noise(gx,   gy,   gz+1);
    int c101 = hash_noise(gx+1, gy,   gz+1);
    int c011 = hash_noise(gx,   gy+1, gz+1);
    int c111 = hash_noise(gx+1, gy+1, gz+1);

    int v1 = c000 + (c100 - c000) * dx / 256;
    int v2 = c010 + (c110 - c010) * dx / 256;
    int v3 = c001 + (c101 - c001) * dx / 256;
    int v4 = c011 + (c111 - c011) * dx / 256;

    int i1 = v1 + (v2 - v1) * dy / 256;
    int i2 = v3 + (v4 - v3) * dy / 256;

    return i1 + (i2 - i1) * dz / 256;
}

static void update_grid(int time_offset) {
    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            int sx = x * 16;   // horizontal stretch for dune effect
            int sy = y * 4;
            noise_grid[y * GRID_W + x] = pnoise(sx, sy, time_offset);
        }
    }
}

static void render_frame(void) {
    update_term_size();
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

        for (int x = 0; x < term_width; x++) {
            int x_sc = (x * (GRID_W - 1) * 1000) / term_width;
            int gx   = x_sc / 1000;
            int rx   = x_sc % 1000;

            int gx1 = gx + 1 < GRID_W ? gx + 1 : gx;

            int v_tl = noise_grid[r1 + gx];
            int v_bl = noise_grid[r2 + gx];
            int v_left = v_tl + ((v_bl - v_tl) * ry) / 1000;

            int v_tr = noise_grid[r1 + gx1];
            int v_br = noise_grid[r2 + gx1];
            int v_right = v_tr + ((v_br - v_tr) * ry) / 1000;

            int val = v_left + ((v_right - v_left) * rx) / 1000;

            int p_idx = (val * PALETTE_LEN) / 256;
            if (p_idx >= PALETTE_LEN) p_idx = PALETTE_LEN - 1;
            if (p_idx < 0) p_idx = 0;

            int color = palette[p_idx];
            if (color != prev_color) {
                int n = snprintf(tmp, sizeof(tmp), "\033[38;5;%dm", color);
                fb_append(tmp, n);
                prev_color = color;
            }
            fb_append(".", 1);
        }

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

    const char *env = getenv("SCREENSAVER_DELAY");
    if (env) {
        double d = atof(env);
        if (d > 0.0) delay_us = (int)(d * 1e6);
    }

    write(STDOUT_FILENO, "\033[?25l\033[2J", 10);

    int time_offset = 0;
    while (running) {
        update_grid(time_offset);
        render_frame();
        usleep(delay_us);
        time_offset += SPEED;
    }

    const char *restore = "\033[?25h\033[0m\033[2J\033[H";
    write(STDOUT_FILENO, restore, strlen(restore));
    free(frame_buffer);
    return 0;
}
