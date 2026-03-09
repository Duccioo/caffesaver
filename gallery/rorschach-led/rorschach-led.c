#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <math.h>

/*
 * rorschach-led — High-performance terminal Rorschach inkblot screensaver
 *
 * Optimized C rewrite of the original bash version.
 * Features:
 *   - 20x20 internal noise grid (vs 10x10 in bash) for smoother blobs
 *   - Bilinear interpolation for pixel-perfect gradients
 *   - 256-color palette with warm accent tones
 *   - Smooth easing transitions between target values
 *   - 5 symmetry modes (Vertical, Horizontal, Diagonal, Quad, Kaleidoscope)
 *   - Periodically morphs symmetry mode for organic evolution
 *   - Full-screen double-buffered output via single write() call
 *   - Reads SCREENSAVER_DELAY env var for configurable framerate
 */

// ── Configuration ──────────────────────────────────────────────────────
#define DEFAULT_FPS     30
#define GRID_W          20
#define GRID_H          20
#define FRAME_BUF_SIZE  (1024 * 512)  // 512KB frame buffer (plenty)
#define MORPH_INTERVAL  1200          // frames between symmetry mode morphs (40s at 30fps)

// ── Palette ────────────────────────────────────────────────────────────
// 256-color indices: transparent(default bg), dark grays → light, orange accent
static const int palette_256[] = { 16, 237, 240, 245, 252, 208 };
#define PALETTE_LEN 6

// ── Types ──────────────────────────────────────────────────────────────
typedef struct { int current, target; } Cell;

// ── Globals ────────────────────────────────────────────────────────────
static Cell   grid[GRID_W * GRID_H];
static int    sym_mode       = 0;
static int    term_width     = 80;
static int    term_height    = 24;
static int    delay_us       = 1000000 / DEFAULT_FPS;
static int    frame_count    = 0;
static volatile int running  = 1;

static char  *frame_buffer   = NULL;
static int    fb_len         = 0;

// ── Signal handling ────────────────────────────────────────────────────
static void handle_signal(int sig) {
    (void)sig;
    running = 0;
}

// ── Terminal helpers ───────────────────────────────────────────────────
static void update_term_size(void) {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0 && w.ws_row > 0) {
        term_width  = w.ws_col;
        term_height = w.ws_row;
    }
}

// ── Frame buffer helpers ───────────────────────────────────────────────
static inline void fb_append(const char *s, int len) {
    if (fb_len + len < FRAME_BUF_SIZE) {
        memcpy(frame_buffer + fb_len, s, len);
        fb_len += len;
    }
}

static inline void fb_append_str(const char *s) {
    fb_append(s, (int)strlen(s));
}

// ── Symmetry ───────────────────────────────────────────────────────────
static void symmetrize_grid(void) {
    int r, c, src, tgt;

    // Diagonal mirror
    if (sym_mode == 2 || sym_mode == 4) {
        for (r = 0; r < GRID_H; r++)
            for (c = r + 1; c < GRID_W; c++) {
                src = r * GRID_W + c;
                tgt = c * GRID_W + r;
                grid[tgt] = grid[src];
            }
    }
    // Vertical mirror (left → right)
    if (sym_mode == 0 || sym_mode == 3 || sym_mode == 4) {
        for (r = 0; r < GRID_H; r++)
            for (c = 0; c < GRID_W / 2; c++) {
                src = r * GRID_W + c;
                tgt = r * GRID_W + (GRID_W - 1 - c);
                grid[tgt] = grid[src];
            }
    }
    // Horizontal mirror (top → bottom)
    if (sym_mode == 1 || sym_mode == 3 || sym_mode == 4) {
        for (r = 0; r < GRID_H / 2; r++)
            for (c = 0; c < GRID_W; c++) {
                src = r * GRID_W + c;
                tgt = (GRID_H - 1 - r) * GRID_W + c;
                grid[tgt] = grid[src];
            }
    }
}

static void init_grid(void) {
    sym_mode = rand() % 5;
    for (int i = 0; i < GRID_W * GRID_H; i++) {
        grid[i].current = rand() % 256;
        grid[i].target  = rand() % 256;
    }
    symmetrize_grid();
}

// ── Grid evolution ─────────────────────────────────────────────────────
static void update_grid(void) {
    for (int i = 0; i < GRID_W * GRID_H; i++) {
        int diff = grid[i].target - grid[i].current;
        if (diff == 0) {
            grid[i].target = rand() % 256;
        } else {
            int step = diff / 20;   // slow, organic easing
            if (step == 0) step = (diff > 0) ? 1 : -1;
            grid[i].current += step;
        }
    }
    symmetrize_grid();

    // Periodically morph symmetry mode for organic evolution
    frame_count++;
    if (frame_count >= MORPH_INTERVAL) {
        frame_count = 0;
        sym_mode = rand() % 5;
    }
}

// ── Rendering ──────────────────────────────────────────────────────────
static void render_frame(void) {
    update_term_size();

    int calc_width = term_width / 2;
    if (calc_width < 1) calc_width = 1;

    fb_len = 0;
    fb_append_str("\033[H");   // cursor home

    char tmp[64];
    int prev_color = -1;       // track last color to skip redundant escapes

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

            // Bilinear interpolation
            int v_tl = grid[r1 + gx].current;
            int v_bl = grid[r2 + gx].current;
            int v_left = v_tl + ((v_bl - v_tl) * ry) / 1000;

            int gx1 = gx + 1 < GRID_W ? gx + 1 : gx;
            int v_tr = grid[r1 + gx1].current;
            int v_br = grid[r2 + gx1].current;
            int v_right = v_tr + ((v_br - v_tr) * ry) / 1000;

            int val = v_left + ((v_right - v_left) * rx) / 1000;

            if (val < 0)   val = 0;
            if (val > 255) val = 255;

            // Mapping value (0-255) to palette index (0-5)
            int p_idx;
            if (val < 40) {
                p_idx = 0; // Transparent/Dark
            } else if (val > 235) {
                p_idx = 5; // Orange accent
            } else if (val > 190) {
                p_idx = 4; // Brightest Gray
            } else {
                // Map the range [40, 190] to palette indices [1, 3]
                p_idx = 1 + ((val - 40) * 3) / 150;
            }

            int color = palette_256[p_idx];

            // Only emit escape if color changed (huge speedup)
            if (color != prev_color) {
                if (p_idx == 0) {
                    fb_append_str("\033[0m");
                } else {
                    int n = snprintf(tmp, sizeof(tmp), "\033[48;5;%dm", color);
                    fb_append(tmp, n);
                }
                prev_color = color;
            }
            fb_append_str("  ");
        }

        // Odd-width column
        if (term_width & 1) {
            fb_append_str(" ");
        }

        fb_append_str("\033[0m");
        prev_color = -1;   // reset after line reset
        if (y < term_height - 1) {
            fb_append("\n", 1);
        }
    }

    write(STDOUT_FILENO, frame_buffer, fb_len);
}

// ── Main ───────────────────────────────────────────────────────────────
int main(void) {
    // Allocate frame buffer on heap
    frame_buffer = (char *)malloc(FRAME_BUF_SIZE);
    if (!frame_buffer) return 1;

    // Hide cursor & clear
    write(STDOUT_FILENO, "\033[?25l\033[2J", 10);

    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);

    srand((unsigned)time(NULL));
    update_term_size();
    init_grid();

    // Read optional delay from environment
    const char *env = getenv("SCREENSAVER_DELAY");
    if (env) {
        double d = atof(env);
        if (d > 0.0) delay_us = (int)(d * 1e6);
    }

    while (running) {
        update_grid();
        render_frame();
        usleep(delay_us);
    }

    // Restore terminal
    const char *restore = "\033[?25h\033[0m\033[2J\033[H";
    write(STDOUT_FILENO, restore, strlen(restore));
    free(frame_buffer);
    return 0;
}
