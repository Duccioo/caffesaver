#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

/*
 * rorschach — endless generative inkblot screensaver
 *
 * The blot is a slice of 3D Perlin noise that flows forever through time,
 * shaped by a soft "inkblot" envelope and folded on the vertical axis
 * (perfect left/right mirror). It never restarts and never repeats: it
 * just keeps slowly morphing from one figure into the next.
 *
 *   - Three resolutions: high (half blocks), medium (2 columns per pixel,
 *     default) and low (4x2 cell blocks) — all with roughly square pixels
 *   - Only half the sheet is computed, the other half is the mirror
 *   - Domain-warped noise for ragged, organic edges, white holes, droplets
 *   - Ink style (solid black <-> watercolour, tint) drifts slowly over time
 *   - 24-bit colour when the terminal supports it, 256-colour otherwise
 *
 * Usage: rorschach [--led] [--paper] [--low | --medium | --high]
 *   --led          add a second, glowing red/orange ink (like Rorschach cards II/III)
 *   --paper        draw on a cream paper sheet instead of the terminal background
 *   --low/--medium/--high   resolution (default: medium)
 *
 * Env:  SCREENSAVER_DELAY   seconds per frame (default 0.033)
 *       RORSCHACH_COLORS    "256" or "truecolor" to override detection
 *       RORSCHACH_BG        "transparent" (default) or "paper"
 *       RORSCHACH_INK       with a transparent background: "light" (default,
 *                           for dark terminals) or "dark" (for light ones)
 *       RORSCHACH_RES       "low", "medium" (default) or "high"
 */

// ── Configuration ──────────────────────────────────────────────────────
#define DEFAULT_DELAY   0.033
#define FLOW_SPEED      0.07    // how fast the noise flows through time
#define FADE_IN         2.0f    // seconds of ink fading in at start-up

// ── Types ──────────────────────────────────────────────────────────────
typedef struct { float r, g, b; } RGB;

typedef struct {
    float z;            // position of the noise slice along the time axis
    float thr;          // ink iso-level
    float alpha;        // global fade-in
    float wash;         // 0 = solid ink, 1 = watery
    RGB   ink;
    RGB   accent;
    float glow;
    float aa;           // soft-edge width (in field units)
} Frame;

// ── Globals ────────────────────────────────────────────────────────────
static volatile sig_atomic_t running = 1;
static int    cols = 80, rows = 24;
static int    truecolor = 0;
static int    led_mode = 0;
static RGB    paper = { 238, 234, 224 };
static int    transparent_bg = 1;
static int    light_ink = 0;               // light ink on a (dark) terminal background
static RGB    backdrop;                    // assumed terminal colour, for soft edges

#define TRANSPARENT 0xFF000000u            // pixel key: terminal default background
#define OPAQUE_MIN  0.25f                  // below this much ink, show the terminal

enum { RES_LOW, RES_MEDIUM, RES_HIGH };
static int    res = RES_MEDIUM;

static int       W = 0, H = 0;     // pixel grid (size depends on the resolution)
static uint32_t *pix = NULL;       // packed 0xRRGGBB
static uint32_t *paper_pix = NULL; // pre-computed paper grain
static char     *fb = NULL;
static size_t    fb_len = 0;

// ── Small helpers ──────────────────────────────────────────────────────
static void handle_signal(int sig) { (void)sig; running = 0; }

static float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static float smooth(float e0, float e1, float x) {
    float t = clampf((x - e0) / (e1 - e0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}
static RGB mix(RGB a, RGB b, float t) {
    RGB c = { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t };
    return c;
}

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static inline void fb_put(const char *s, size_t n) {
    memcpy(fb + fb_len, s, n);
    fb_len += n;
}

// ── Gradient noise (3D Perlin) ─────────────────────────────────────────
static uint32_t noise_seed = 1;

static inline uint32_t hash3(int x, int y, int z) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u
               + (uint32_t)z * 2246822519u + noise_seed * 3266489917u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static inline float grad3(uint32_t h, float x, float y, float z) {
    switch (h % 12) {
        case 0:  return  x + y;  case 1:  return -x + y;
        case 2:  return  x - y;  case 3:  return -x - y;
        case 4:  return  x + z;  case 5:  return -x + z;
        case 6:  return  x - z;  case 7:  return -x - z;
        case 8:  return  y + z;  case 9:  return -y + z;
        case 10: return  y - z;  default: return -y - z;
    }
}

static inline float fade_curve(float t) { return t * t * t * (t * (t * 6.f - 15.f) + 10.f); }

// z is split into an integer lattice cell and a fraction so that precision
// does not degrade however long the screensaver runs
static float noise3i(float x, float y, int zi, float zf) {
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float xf = x - xi, yf = y - yi;
    float u = fade_curve(xf), v = fade_curve(yf), w = fade_curve(zf);

    float n000 = grad3(hash3(xi,     yi,     zi    ), xf,       yf,       zf);
    float n100 = grad3(hash3(xi + 1, yi,     zi    ), xf - 1.f, yf,       zf);
    float n010 = grad3(hash3(xi,     yi + 1, zi    ), xf,       yf - 1.f, zf);
    float n110 = grad3(hash3(xi + 1, yi + 1, zi    ), xf - 1.f, yf - 1.f, zf);
    float n001 = grad3(hash3(xi,     yi,     zi + 1), xf,       yf,       zf - 1.f);
    float n101 = grad3(hash3(xi + 1, yi,     zi + 1), xf - 1.f, yf,       zf - 1.f);
    float n011 = grad3(hash3(xi,     yi + 1, zi + 1), xf,       yf - 1.f, zf - 1.f);
    float n111 = grad3(hash3(xi + 1, yi + 1, zi + 1), xf - 1.f, yf - 1.f, zf - 1.f);

    float x00 = n000 + u * (n100 - n000), x10 = n010 + u * (n110 - n010);
    float x01 = n001 + u * (n101 - n001), x11 = n011 + u * (n111 - n011);
    float y0 = x00 + v * (x10 - x00), y1 = x01 + v * (x11 - x01);
    return (y0 + w * (y1 - y0)) * 0.7f;   // roughly [-0.7, 0.7]
}

// Time coordinate: kept as double, split per call
static float noise3(float x, float y, double z) {
    double fz = floor(z);
    return noise3i(x, y, (int)(int64_t)fz, (float)(z - fz));
}

static float fbm(float x, float y, double z, int oct) {
    float sum = 0.f, amp = 0.5f, norm = 0.f;
    for (int i = 0; i < oct; i++) {
        sum  += amp * noise3(x, y, z);
        norm += amp;
        x = x * 2.03f + 17.1f; y = y * 2.03f + 5.3f; z = z * 1.3 + 3.1;
        amp *= 0.5f;
    }
    return sum / norm;
}

// Slow 1D wandering value in [0, 1] for parameters that drift over time
static float drift(double t, float lane) {
    return clampf(0.5f + 1.4f * noise3(lane, 0.5f, t), 0.f, 1.f);
}

// ── The ink field ──────────────────────────────────────────────────────
// x = distance from the fold (>= 0), y in [-1, 1] (top to bottom)
static float ink_field(float x, float y, double z, float *px_out, float *py_out) {
    // domain warp: makes the noise flow and the outline organic
    float wx = noise3(x * 1.3f + 11.f, y * 1.3f, z * 0.8);
    float wy = noise3(x * 1.3f - 7.f, y * 1.3f + 23.f, z * 0.8 + 5.0);
    float px = x + wx * 0.25f, py = y + wy * 0.25f;
    *px_out = px; *py_out = py;

    // soft inkblot envelope: wide ellipse, keeps the ink on the sheet
    float ex = px / 1.3f, ey = py / 1.0f;
    float env = 1.f - (ex * ex + ey * ey);

    // central body along the fold keeps the two halves joined
    float spine = expf(-(px * px) / 0.03f) * (1.f - py * py);

    // the flowing noise itself: big lobes, horizontal arms, ragged detail
    float n = fbm(px * 1.5f, py * 1.5f, z, 3);
    float arms = noise3(px * 1.1f + 60.f, py * 3.4f, z * 0.9 + 60.0);
    float d = noise3(px * 6.5f, py * 6.5f, z * 1.6 + 40.0);

    return 0.45f * env + 0.3f * spine + 1.5f * n + 0.45f * arms + 0.2f * d;
}

static float accent_field(float px, float py, double z) {
    // lives in bands near the top and the bottom of the blot
    float band = smooth(0.3f, 0.6f, fabsf(py)) * (1.f - smooth(0.8f, 0.95f, fabsf(py)));
    float side = 1.f - smooth(0.4f, 0.75f, px);
    float n = fbm(px * 2.2f + 50.f, py * 2.2f, z * 1.2 + 50.0, 2);
    return 0.4f * band * side + 1.1f * n - 0.24f;
}

// ── Shading of one pixel ───────────────────────────────────────────────
// *op receives how much ink covers the pixel (0 = bare paper / background)
static RGB shade(RGB col, float x, float y, const Frame *fr, double z, float S, float *op) {
    float px, py;
    *op = 0.f;
    float f = ink_field(x, y, z, &px, &py);
    float thr = fr->thr;

    if (f > thr - fr->aa) {
        float cov = smooth(thr - fr->aa, thr + fr->aa, f);
        float depth = f - thr;
        float w = fr->wash;
        float inner = smooth(-0.35f, 0.35f, noise3(px * 1.7f + 7.f, py * 1.7f, z * 0.6 + 20.0));
        float tone = 1.f - w * (0.1f + 0.5f * inner);
        tone += w * 0.3f * expf(-depth / 0.08f);           // ink pools at the rim
        if (x * S < 0.6f) tone += 0.08f * w;                  // faint fold crease
        tone = clampf(tone, 0.08f, 1.f);
        col = mix(col, fr->ink, tone * cov * fr->alpha);
        *op = cov * fr->alpha;
    } else if (f > thr - 0.3f) {
        // droplets flung just outside the outline
        float s = noise3(px * 9.f + 90.f, py * 9.f, z * 0.5 + 90.0);
        if (s > 0.38f) {
            float k = 0.9f * fr->alpha * smooth(0.38f, 0.45f, s);
            col = mix(col, fr->ink, k);
            *op = k;
        }
    }

    if (led_mode) {
        float a = accent_field(px, py, z);
        if (a > 0.f) {
            float cov = smooth(0.f, 0.08f, a);
            RGB ac = { fr->accent.r * fr->glow, fr->accent.g * fr->glow, fr->accent.b * fr->glow };
            ac = mix(ac, (RGB){ ac.r * 0.55f, ac.g * 0.45f, ac.b * 0.45f }, 0.35f * expf(-a / 0.06f));
            col = mix(col, ac, 0.92f * cov * fr->alpha);
            *op = fmaxf(*op, cov * fr->alpha);
        }
    }
    return col;
}

// ── Colour output ──────────────────────────────────────────────────────
static int rgb_to_256(int r, int g, int b) {
    static const int lv[6] = { 0, 95, 135, 175, 215, 255 };
    #define Q(v) ((v) < 48 ? 0 : (v) < 115 ? 1 : ((v) - 35) / 40)
    int qr = Q(r), qg = Q(g), qb = Q(b);
    #undef Q
    int gi = ((r + g + b) / 3 - 3) / 10;
    if (gi < 0) gi = 0;
    if (gi > 23) gi = 23;
    int gv = 8 + 10 * gi;
    int dc = (lv[qr] - r) * (lv[qr] - r) + (lv[qg] - g) * (lv[qg] - g) + (lv[qb] - b) * (lv[qb] - b);
    int dg = (gv - r) * (gv - r) + (gv - g) * (gv - g) + (gv - b) * (gv - b);
    // prefer the smooth grey ramp unless the cube is clearly closer
    return (dg <= dc * 2) ? 232 + gi : 16 + 36 * qr + 6 * qg + qb;
}

// What the terminal will actually show: the RGB itself, or its 256-colour index
static inline uint32_t out_key(uint32_t c) {
    if (truecolor || c == TRANSPARENT) return c;
    return (uint32_t)rgb_to_256((c >> 16) & 255, (c >> 8) & 255, c & 255);
}

// fg = 1: foreground (never transparent), fg = 0: background
static int emit_color(char *buf, int fg, uint32_t key) {
    if (key == TRANSPARENT)
        return sprintf(buf, "\033[49m");
    if (truecolor)
        return sprintf(buf, "\033[%d;2;%u;%u;%um", fg ? 38 : 48,
                       (key >> 16) & 255, (key >> 8) & 255, key & 255);
    return sprintf(buf, "\033[%d;5;%um", fg ? 38 : 48, key);
}

static uint32_t pack(RGB c) {
    int r = (int)clampf(c.r, 0.f, 255.f), g = (int)clampf(c.g, 0.f, 255.f),
        b = (int)clampf(c.b, 0.f, 255.f);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

// ── Frame ──────────────────────────────────────────────────────────────
// pixels per unit: the blot spans y in [-1, 1] and fits with a paper margin
static float paper_scale(void) {
    return fminf(H / 2.f / 1.08f, W / 2.f / 1.45f);
}

// t = position on the endless time axis, elapsed = seconds since start-up
static void render(double t, double elapsed) {
    // everything wanders slowly and forever
    Frame fr;
    double z = t * FLOW_SPEED;
    fr.alpha = smooth(0.f, FADE_IN, (float)elapsed);
    fr.thr = 0.3f + 0.12f * (drift(t * 0.013, 3.f) - 0.5f);   // more / less ink
    fr.wash = 0.75f * smooth(0.3f, 0.75f, drift(t * 0.009, 7.f));
    static const RGB dark_inks[3]  = { { 16, 14, 18 },    { 38, 56, 74 },    { 44, 36, 30 } };
    static const RGB light_inks[3] = { { 236, 233, 226 }, { 190, 210, 232 }, { 232, 212, 186 } };
    const RGB *inks = light_ink ? light_inks : dark_inks;
    RGB black = inks[0], blue = inks[1], sepia = inks[2];
    float tint = drift(t * 0.007, 11.f);
    fr.ink = tint < 0.5f ? mix(blue, black, tint * 2.f) : mix(black, sepia, tint * 2.f - 1.f);
    fr.ink = mix(black, fr.ink, fr.wash / 0.75f);           // tints only show in washes
    static const RGB red = { 230, 40, 35 }, orange = { 255, 110, 20 }, crimson = { 245, 60, 90 };
    float hue = drift(t * 0.011, 13.f);
    fr.accent = hue < 0.5f ? mix(red, orange, hue * 2.f) : mix(orange, crimson, hue * 2.f - 1.f);
    fr.glow = 0.85f + 0.15f * (float)sin(fmod(elapsed * 1.3, 6.283185307));
    fr.aa = 0.06f;

    float S = paper_scale();
    float cx = W / 2.f, cy = H / 2.f;

    int half = (W + 1) / 2;
    for (int py = 0; py < H; py++) {
        float y = (py + 0.5f - cy) / S;
        uint32_t *row = pix + (size_t)py * W;
        const uint32_t *prow = paper_pix + (size_t)py * W;
        for (int px = 0; px < half; px++) {
            uint32_t c = prow[px];
            float x = fabsf((px + 0.5f - cx) / S);
            // outside this box the envelope keeps the sheet blank
            if (fabsf(y) < 1.2f && x < 1.6f) {
                RGB p = (c == TRANSPARENT) ? backdrop
                      : (RGB){ (float)(c >> 16), (float)((c >> 8) & 255), (float)(c & 255) };
                float op;
                RGB col = shade(p, x, y, &fr, z, S, &op);
                if (c != TRANSPARENT || op >= OPAQUE_MIN) c = pack(col);
            }
            row[px] = c;
            row[W - 1 - px] = c;
        }
    }

    fb_len = 0;
    char tmp[48];
    if (res == RES_HIGH) {
        // two pixels per cell: upper one = foreground of "▀", lower = background
        for (int r = 0; r < rows; r++) {
            fb_len += (size_t)sprintf(fb + fb_len, "\033[%d;1H", r + 1);
            uint32_t cur_fg = 0xFFFFFFFF, cur_bg = 0xFFFFFFFF;
            const uint32_t *top = pix + (size_t)(2 * r) * W, *bot = top + W;
            for (int x = 0; x < W; x++) {
                uint32_t a = out_key(top[x]), b = out_key(bot[x]);
                const char *glyph = "\xE2\x96\x80";            // ▀
                uint32_t fg = a, bg = b;
                if (a == b) glyph = NULL;
                else if (a == TRANSPARENT) { glyph = "\xE2\x96\x84"; fg = b; bg = a; }  // ▄
                if (bg != cur_bg) { fb_put(tmp, (size_t)emit_color(tmp, 0, bg)); cur_bg = bg; }
                if (!glyph) { fb_put(" ", 1); continue; }
                if (fg != cur_fg) { fb_put(tmp, (size_t)emit_color(tmp, 1, fg)); cur_fg = fg; }
                fb_put(glyph, 3);
            }
        }
    } else {
        // each pixel is a block of bw x bh cells; leftover columns are split
        // evenly on both sides so the blot stays centred
        int bw = res == RES_LOW ? 4 : 2, bh = res == RES_LOW ? 2 : 1;
        int pad = cols - W * bw, lpad = pad / 2, rpad = pad - lpad;
        static const char spaces[] = "    ";
        for (int r = 0; r < rows; r++) {
            fb_len += (size_t)sprintf(fb + fb_len, "\033[%d;1H", r + 1);
            uint32_t cur = 0xFFFFFFFF;
            const uint32_t *row = pix + (size_t)(r / bh) * W;
            for (int x = -1; x <= W; x++) {
                int n = x < 0 ? lpad : (x == W ? rpad : bw);
                if (n <= 0) continue;
                uint32_t k = out_key(row[x < 0 ? 0 : (x == W ? W - 1 : x)]);
                if (k != cur) { fb_put(tmp, (size_t)emit_color(tmp, 0, k)); cur = k; }
                fb_put(spaces, (size_t)n);
            }
        }
    }
    fb_put("\033[0m", 4);

    size_t off = 0;
    while (off < fb_len) {
        ssize_t n = write(STDOUT_FILENO, fb + off, fb_len - off);
        if (n <= 0) break;
        off += (size_t)n;
    }
}

static int update_size(void) {
    struct winsize ws;
    int c = cols, r = rows;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        c = ws.ws_col; r = ws.ws_row;
    }
    if (c == cols && r == rows && pix) return 0;
    cols = c; rows = r;
    switch (res) {
        case RES_HIGH: W = cols;     H = rows * 2;       break;
        case RES_LOW:  W = cols / 4; H = (rows + 1) / 2; break;
        default:       W = cols / 2; H = rows;           break;
    }
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    free(pix); free(paper_pix); free(fb);
    pix = malloc(sizeof(uint32_t) * (size_t)W * H);
    paper_pix = malloc(sizeof(uint32_t) * (size_t)W * H);
    // worst case per cell: two 24-bit colour escapes + a 3-byte glyph
    fb = malloc((size_t)cols * rows * 44 + (size_t)rows * 16 + 64);
    if (!pix || !paper_pix || !fb) { perror("malloc"); exit(1); }

    // paper grain is fixed to the sheet: compute it once (mirrored like the ink)
    float S = paper_scale();
    for (int py = 0; py < H; py++)
        for (int px = 0; px < W; px++) {
            float x = fabsf((px + 0.5f - W / 2.f) / S), y = (py + 0.5f - H / 2.f) / S;
            float g = noise3(x * 6.f, y * 6.f, 3.7) * 6.f;
            paper_pix[(size_t)py * W + px] = transparent_bg ? TRANSPARENT
                : pack((RGB){ paper.r + g, paper.g + g, paper.b + g });
        }
    return 1;
}

// ── Main ───────────────────────────────────────────────────────────────
int main(int argc, char **argv) {
    const char *bg = getenv("RORSCHACH_BG");
    if (bg && strcmp(bg, "paper") == 0) transparent_bg = 0;
    const char *rs = getenv("RORSCHACH_RES");
    if (rs && strcmp(rs, "low") == 0)  res = RES_LOW;
    if (rs && strcmp(rs, "high") == 0) res = RES_HIGH;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--led") == 0) led_mode = 1;
        else if (strcmp(argv[i], "--paper") == 0) transparent_bg = 0;
        else if (strcmp(argv[i], "--transparent") == 0) transparent_bg = 1;
        else if (strcmp(argv[i], "--low") == 0) res = RES_LOW;
        else if (strcmp(argv[i], "--medium") == 0) res = RES_MEDIUM;
        else if (strcmp(argv[i], "--high") == 0) res = RES_HIGH;
    }
    const char *ik = getenv("RORSCHACH_INK");
    light_ink = transparent_bg && !(ik && strcmp(ik, "dark") == 0);

    const char *ct = getenv("COLORTERM");
    truecolor = ct && (strstr(ct, "truecolor") || strstr(ct, "24bit"));
    const char *ov = getenv("RORSCHACH_COLORS");
    if (ov) truecolor = strcmp(ov, "256") != 0;
    if (!truecolor) paper = (RGB){ 238, 238, 238 };
    backdrop = light_ink ? (RGB){ 20, 20, 22 } : paper;

    double delay = DEFAULT_DELAY;
    const char *env = getenv("SCREENSAVER_DELAY");
    if (env && atof(env) > 0.0) delay = atof(env);

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGQUIT, handle_signal);
    signal(SIGHUP, handle_signal);

    srand((unsigned)time(NULL) ^ (unsigned)getpid());
    noise_seed = (uint32_t)rand();

    const char *init = "\033[?25l\033[2J";
    write(STDOUT_FILENO, init, strlen(init));

    // start somewhere random along the endless time axis
    double t_start = now_s(), t_off = (rand() % 100000) / 10.0;
    while (running) {
        double frame_start = now_s();
        if (update_size()) write(STDOUT_FILENO, "\033[2J", 4);

        double elapsed = frame_start - t_start;
        render(t_off + elapsed, elapsed);

        double left = delay - (now_s() - frame_start);
        if (left > 0) usleep((useconds_t)(left * 1e6));
    }

    const char *restore = "\033[0m\033[?25h\033[2J\033[H";
    write(STDOUT_FILENO, restore, strlen(restore));
    free(pix);
    free(paper_pix);
    free(fb);
    return 0;
}
