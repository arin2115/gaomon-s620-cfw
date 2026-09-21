// Pen tracker: finds the pen, follows it coil by coil, estimates position and pressure.
// X has 30 coil positions and Y has 19, but only 16 physical coils each (the positions wrap around and share coils).
// Position: ratio of the amplitudes of the coils around the peak.
// Pressure: the pen's resonance frequency shifts when pressed; it is found by measuring 3 neighbouring burst frequencies.
#include <stdint.h>
#include "pentrack.h"
#include "emr.h"
#include "settings.h"

#define DET_FREQ         8          // burst frequency used to find the pen

#define X_MAX 33020
#define Y_MAX 20320

static int pos_x, pos_y;            // tracked coil positions
static int fc;                      // tracked resonance burst frequency
static int have_pen, tip;
static int f256_smooth, phase;
static int res_invalid_run;         // resonance checks in a row without a peak
static int release_run, press_run;  // checks in a row that say the tip is up / down
static uint32_t last_x, last_y;     // last reported position
static int lim_x0, lim_x1, lim_y0, lim_y1;          // coil positions the tracker may use: the area plus one coil
static long area_x0, area_x1, area_y0, area_y1;     // the active area in raw position units (before mirroring)
static int was_in;                                  // the last report said "in range"
static long sx, sy;                 // previous filter output, before flipping
static int ma_cnt;
static long ma_x[4], ma_y[4];

static uint8_t first_x[X_COILS_N], first_y[Y_COILS_N];   // first position that uses the same physical coil
static int inited;

static void init_alias(void)
{
    for (int i = 0; i < X_COILS_N; i++) {
        first_x[i] = (uint8_t)i;
        for (int j = 0; j < i; j++)
            if (X_COILS[j].pb_and == X_COILS[i].pb_and && X_COILS[j].pa_or == X_COILS[i].pa_or) { first_x[i] = (uint8_t)j; break; }
    }
    for (int i = 0; i < Y_COILS_N; i++) {
        first_y[i] = (uint8_t)i;
        for (int j = 0; j < i; j++)
            if (Y_COILS[j].pb_and == Y_COILS[i].pb_and && Y_COILS[j].pa_or == Y_COILS[i].pa_or) { first_y[i] = (uint8_t)j; break; }
    }
    inited = 1;
}

static int coil_pos(long raw, int off, int pitch, int n)
{
    if (raw < off) return 0;
    int p = (int)((raw - off) / pitch) + 1;
    return p < n ? p : n - 1;
}

// The active area is set in reported coordinates; the tracker works in raw ones, so undo the mirroring.
static void area_update(void)
{
    long x0 = CFG(AREA_X0), x1 = CFG(AREA_X1), y0 = CFG(AREA_Y0), y1 = CFG(AREA_Y1);
    if (CFG(FLIP_X)) { long t = X_MAX - x1; x1 = X_MAX - x0; x0 = t; }
    if (CFG(FLIP_Y)) { long t = Y_MAX - y1; y1 = Y_MAX - y0; y0 = t; }
    area_x0 = x0; area_x1 = x1; area_y0 = y0; area_y1 = y1;
    lim_x0 = coil_pos(x0, 4, 1179, X_COILS_N) - 1; if (lim_x0 < 0) lim_x0 = 0;
    lim_x1 = coil_pos(x1, 4, 1179, X_COILS_N) + 1; if (lim_x1 > X_COILS_N - 1) lim_x1 = X_COILS_N - 1;
    lim_y0 = coil_pos(y0, 2, 1195, Y_COILS_N) - 1; if (lim_y0 < 0) lim_y0 = 0;
    lim_y1 = coil_pos(y1, 2, 1195, Y_COILS_N) + 1; if (lim_y1 > Y_COILS_N - 1) lim_y1 = Y_COILS_N - 1;
}

static inline uint16_t mx(int p, int f) { return p < 0 || p >= X_COILS_N ? 0 : emr_self(&X_COILS[p], f); }
static inline uint16_t my(int p, int f) { return p < 0 || p >= Y_COILS_N ? 0 : emr_self(&Y_COILS[p], f); }

// Position with the largest 3-coil sum. Wrapped positions share a coil, but their neighbours are quiet, so they lose.
static int best_window(const uint16_t *amp, int n)
{
    int best = 0, bs = -1;
    for (int p = 0; p < n; p++) {
        int s = amp[p] * 2 + (p > 0 ? amp[p - 1] : 0) + (p + 1 < n ? amp[p + 1] : 0);
        if (s > bs) { bs = s; best = p; }
    }
    return best;
}

static int lost_thr = 150;      // below this amplitude the pen is gone (kept above the coil-to-coil leakage)
static int leak_est;

// The search drives and listens on the same coil, so the coil's own ringing must die out first.
struct fastcfg { uint16_t sa, sb, per; uint8_t mean, burst; };
static struct fastcfg use_slow(void)
{
    struct fastcfg c = { emr_settle_a, emr_settle_b, emr_period_us, emr_mean_n, emr_burst_cycles };
    emr_settle_a = 27; emr_settle_b = 30; emr_mean_n = 0; emr_burst_cycles = (uint8_t)CFG(SEARCH_BURST); emr_period_us = 0;
    return c;
}
static void restore_fast(struct fastcfg c)
{
    emr_settle_a = c.sa; emr_settle_b = c.sb; emr_period_us = c.per; emr_mean_n = c.mean; emr_burst_cycles = c.burst;
}

static int search(void)
{
    struct fastcfg saved = use_slow();
    uint16_t ax[X_COILS_N], ay[Y_COILS_N];
    uint16_t px = 0, py = 0;
    for (int i = 0; i < X_COILS_N; i++) {
        ax[i] = first_x[i] == i ? mx(i, DET_FREQ) : ax[first_x[i]];
        if (ax[i] > px) px = ax[i];
    }
    for (int i = 0; i < Y_COILS_N; i++) {
        ay[i] = first_y[i] == i ? my(i, DET_FREQ) : ay[first_y[i]];
        if (ay[i] > py) py = ay[i];
    }
    restore_fast(saved);
    for (int i = 0; i < X_COILS_N; i++) if (i < lim_x0 || i > lim_x1) ax[i] = 0;     // ignore coils outside the active area
    for (int i = 0; i < Y_COILS_N; i++) if (i < lim_y0 || i > lim_y1) ay[i] = 0;
    px = py = 0;
    for (int i = 0; i < X_COILS_N; i++) if (ax[i] > px) px = ax[i];
    for (int i = 0; i < Y_COILS_N; i++) if (ay[i] > py) py = ay[i];
    if (px < CFG(DET_THRESHOLD) || py < CFG(DET_THRESHOLD)) {
        // No pen: measure the coil-to-coil leakage of the fast cross-scan
        int l = 0;
        for (int i = 0; i < 4; i++) {
            int v = emr_measure(&Y_COILS[3 + 3 * i], &X_COILS[4 + 5 * i], DET_FREQ);
            int w = emr_measure(&X_COILS[4 + 5 * i], &Y_COILS[3 + 3 * i], DET_FREQ);
            if (v > l) l = v;
            if (w > l) l = w;
        }
        leak_est = l > leak_est ? l : leak_est * 15 / 16;
        lost_thr = leak_est * 2 + 80;
        if (lost_thr < CFG(LOST_THRESHOLD)) lost_thr = CFG(LOST_THRESHOLD);
        if (lost_thr > 600) lost_thr = 600;
        return 0;
    }
    pos_x = best_window(ax, X_COILS_N);
    pos_y = best_window(ay, Y_COILS_N);
    fc = DET_FREQ;
    return 1;
}

// Cross-scan: for an X window the Y coil at the pen transmits and the X coils only listen (and the other way round).
// Every measurement then excites the pen the same way, so its leftover ringing cancels in the amplitude ratios.
static int fcur;
static uint16_t gx(int off)
{
    int p = pos_x + off;
    return p < 0 || p >= X_COILS_N ? 0 : emr_measure(&Y_COILS[pos_y], &X_COILS[p], fcur);
}
static uint16_t gy(int off)
{
    int p = pos_y + off;
    return p < 0 || p >= Y_COILS_N ? 0 : emr_measure(&X_COILS[pos_x], &Y_COILS[p], fcur);
}

// Signal limiter: changes the burst length only when the amplitude is far too high or too low.
// Inside the normal range it stays put, because the amplitude varies as the pen crosses coils and following it would disturb the position.
static uint8_t burst_n = 18;

static void agc_update(int amp)
{
    if (amp > CFG(AMP_HIGH) && burst_n > CFG(BURST_MIN)) burst_n--;
    else if (amp < CFG(AMP_LOW) && burst_n < CFG(BURST_MAX)) burst_n++;
    emr_burst_cycles = burst_n;
}

// Position estimator: a, b, c = amplitudes of the coil before, at and after the peak; s = coil pitch.
// The offset inside the coil is the average of a168() and either an edge rule or alt().
static int a168(int a, int b, int c, int s)
{
    int den = (b - a) + (b - c);
    if (den <= 0) return s / 2;
    int r = (b - a) * s / den;
    return r < 0 ? 0 : r > s ? s : r;
}

static int alt(int a, int b, int c, int s, int ext)      // ext: the next coil out on the stronger side
{
    int r;
    if (c >= a) {
        int den = (b - ext) + (c - a);
        r = (den > 0 ? (c - a) * s / den : 0) + s / 2;
    } else {
        int den = (b - ext) + (a - c);
        r = s / 2 - (den > 0 ? (a - c) * s / den : 0);
    }
    return r < -0x95 ? -0x95 : r > s + 0x96 ? s + 0x96 : r;
}

// mode 0: normal coil, 1: second coil of the axis, 2: second-last coil
static int estimate(int a, int b, int c, int s, int mode, int ext)
{
    int e1;
    if ((mode == 1 && c < a) || (mode == 2 && c > a)) e1 = a168(a, b, c, s);
    else if (b < c) e1 = s;
    else if (b < a) e1 = 0;
    else e1 = alt(a, b, c, s, ext);
    return (e1 + a168(a, b, c, s)) >> 1;
}

// Median of the last three positions: removes single bad measurements (about 0.5% are off) and delays real movement by about one report.
static long med3(long a, long b, long c)
{
    long lo = a < b ? a : b, hi = a < b ? b : a;
    long m = hi < c ? hi : c;
    return lo > m ? lo : m;
}

static long spike_hist[2][3];
static int spike_cnt[2];
static long despike_axis(int ax, long v)                  // call only when the axis has a new value
{
    if (spike_cnt[ax] < 3) spike_cnt[ax]++;
    spike_hist[ax][2] = spike_hist[ax][1]; spike_hist[ax][1] = spike_hist[ax][0]; spike_hist[ax][0] = v;
    return spike_cnt[ax] >= 3 ? med3(spike_hist[ax][0], spike_hist[ax][1], spike_hist[ax][2]) : v;
}

// Position filter: moving average, then an exponential filter,
// with a hover dead-zone that freezes the output for tiny movements.
static long moving_avg(long *buf, long v, int cnt)
{
    long sum = 0;
    if (cnt <= CFG(SMOOTH_MA)) {
        for (int i = 0; i < cnt - 1; i++) sum += buf[i];
        buf[cnt - 1] = v;
        return (sum + v) / cnt;
    }
    for (int i = 0; i < CFG(SMOOTH_MA) - 1; i++) { buf[i] = buf[i + 1]; sum += buf[i]; }
    buf[CFG(SMOOTH_MA) - 1] = v;
    return (sum + v) / CFG(SMOOTH_MA);
}

static long ema(long cur, long prev, int w)
{
    if (cur < prev) return prev - ((prev - cur) * w + 0x80) / 256;
    if (cur > prev) return prev + ((cur - prev) * w + 0x80) / 256;
    return prev;
}

static void filter_position(long *x, long *y, int amp_sum, int amp_min, int hover)
{
    if (ma_cnt <= CFG(SMOOTH_MA)) ma_cnt++;
    long ax = moving_avg(ma_x, *x, ma_cnt), ay = moving_avg(ma_y, *y, ma_cnt);
    if (ma_cnt < 2) { sx = ax; sy = ay; }
    long speed = (ax > sx ? ax - sx : sx - ax) + (ay > sy ? ay - sy : sy - ay);
    // Dead-zone radius depends on signal strength. The two stronger bands are wider than the original's (20, 60) because this scan is noisier.
    int freeze = hover && ((amp_sum > 0x5aa && speed <= CFG(HOVER_ZONE_HI)) ||
                           (amp_sum > 0x320 && amp_sum <= 0x5aa && speed <= CFG(HOVER_ZONE_MID)) ||
                           (amp_sum > 0xc8 && amp_sum <= 0x320 && speed <= CFG(HOVER_ZONE_LO)));
    // weaker signal = more smoothing
    int w = CFG(SMOOTH_EMA);
    if (amp_min < CFG(SMOOTH_FULL_AMP)) {
        w = CFG(SMOOTH_EMA) * amp_min / CFG(SMOOTH_FULL_AMP);
        if (w < CFG(SMOOTH_MIN_W)) w = CFG(SMOOTH_MIN_W);
    }
    if (!freeze) { sx = ema(ax, sx, w); sy = ema(ay, sy, w); }
    *x = sx; *y = sy;
}

// One axis per report, alternating Y and X, so each axis updates every second report (this is what makes ~1000 Hz possible).
// A window is 6 coils around the pen, measured back to back and re-centred on the strongest one.
static int turn_x;                                       // which axis the next report scans
static int win_x[5], win_y[5];                           // latest windows
static long raw_x, raw_y;                                // latest despiked position per axis
static int amp_x, amp_y;                                 // latest centre amplitudes
static int first_scan;

static void scan_y(void)
{
    int by[6], oy;
    emr_grid_reset();                            // the first coil of a window is a dummy, so start the grid right away
    for (int i = 0; i < 6; i++) by[i] = gy(i - 3);
    int iy = 0;
    for (int i = 1; i < 6; i++) if (by[i] > by[iy]) iy = i;
    int sy_ = 0;
    if (iy <= 2 && pos_y > 0) sy_ = -1; else if (iy >= 4 && pos_y < Y_COILS_N - 1) sy_ = 1;
    oy = sy_ < 0 ? 0 : sy_ > 0 ? 2 : 1;
    for (int i = 0; i < 5; i++) win_y[i] = oy + i < 6 ? by[oy + i] : 0;
    if (pos_y + sy_ < lim_y0 || pos_y + sy_ > lim_y1) sy_ = 0;      // keep the window centre inside the active area
    pos_y += sy_;
    int ya = win_y[1], y1 = win_y[2], yc = win_y[3], ye = yc >= ya ? win_y[4] : win_y[0];
    int j = pos_y + 1;
    long yy;
    if (j == 1) yy = 0;
    else if (j == Y_COILS_N) yy = Y_MAX;
    else if (j == 2) yy = estimate(ya, y1, yc, 1197, 1, ye);
    else if (j == Y_COILS_N - 1) yy = 19122L + estimate(ya, y1, yc, 1198, 2, ye);
    else yy = 1195L * (j - 3) + 1197 + estimate(ya, y1, yc, 1195, 0, ye);
    if (yy < 0) yy = 0; if (yy > Y_MAX) yy = Y_MAX;
    raw_y = despike_axis(1, yy);
    amp_y = y1;
}

static void scan_x(void)
{
    int bx[6], ox;
    emr_grid_reset();
    for (int i = 0; i < 6; i++) bx[i] = gx(i - 3);
    int ix = 0;
    for (int i = 1; i < 6; i++) if (bx[i] > bx[ix]) ix = i;
    int sx_ = 0;
    if (ix <= 2 && pos_x > 0) sx_ = -1; else if (ix >= 4 && pos_x < X_COILS_N - 1) sx_ = 1;
    ox = sx_ < 0 ? 0 : sx_ > 0 ? 2 : 1;
    for (int i = 0; i < 5; i++) win_x[i] = ox + i < 6 ? bx[ox + i] : 0;
    if (pos_x + sx_ < lim_x0 || pos_x + sx_ > lim_x1) sx_ = 0;
    pos_x += sx_;
    int xa = win_x[1], x1 = win_x[2], xc = win_x[3], xe = xc >= xa ? win_x[4] : win_x[0];
    int k = pos_x + 1;
    long xx;
    if (k == 1) xx = 0;
    else if (k == X_COILS_N) xx = X_MAX;
    else if (k == 2) xx = estimate(xa, x1, xc, 1183, 1, xe);
    else if (k == X_COILS_N - 1) xx = 31837L + estimate(xa, x1, xc, 1183, 2, xe);
    else xx = 1179L * (k - 3) + 1183 + estimate(xa, x1, xc, 1179, 0, xe);
    if (xx < 0) xx = 0; if (xx > X_MAX) xx = X_MAX;
    raw_x = despike_axis(0, xx);
    amp_x = x1;
}

// settings changed while running: reset what depends on them
void pentrack_settings_changed(void)
{
    burst_n = (uint8_t)CFG(BURST_DEF);
    emr_burst_cycles = burst_n;
    ma_cnt = 0;
    area_update();
    lost_thr = CFG(LOST_THRESHOLD);
    leak_est = 0;
}

void (*pentrack_idle_hook)(void);
#define IDLE_HOOK() do { if (pentrack_idle_hook) pentrack_idle_hook(); } while (0)

int pentrack_step(pen_sample_t *s)
{
    if (!inited) { init_alias(); area_update(); }

    if (!have_pen) {
        if (!search())
            return 0;
        have_pen = 1; tip = 0; release_run = 0; press_run = 0; f256_smooth = fc * 256; phase = 0; ma_cnt = 0;
        spike_cnt[0] = spike_cnt[1] = 0;
        burst_n = (uint8_t)CFG(BURST_DEF);              // the burst length only limits the signal
        emr_burst_cycles = burst_n;
        turn_x = 0; first_scan = 1;                      // the first report scans both axes
        return 0;
    }

    int do_freq = (phase++ % CFG(FREQ_EVERY)) == 0;
    fcur = fc;
    int passes = first_scan ? 2 : 1;
    first_scan = 0;
    int scanned;
    for (int pass = 0; pass < passes; pass++) {
        if (turn_x) { scan_x(); scanned = amp_x; } else { scan_y(); scanned = amp_y; }
        turn_x ^= 1;
    }
    int x1 = amp_x, y1 = amp_y;

    agc_update(scanned);

    if (scanned < lost_thr) {
        have_pen = 0;
        tip = 0;
        if (!was_in) return 0;                        // already reported as out of range
        was_in = 0;
        s->in_range = 0; s->tip = 0; s->pressure = 0;
        s->x = last_x; s->y = last_y;                 // out of range at the last position, not at 0, 0 (the cursor would jump to the corner)
        return 1;
    }

    // resonance (pressure) check every few reports
    if (do_freq) {
        const coil_t *cx = &X_COILS[pos_x], *cy = &Y_COILS[pos_y];
        // A long burst gives a narrow frequency response (a short one hardly depends on frequency).
        // Attenuated gain, because the signal is strong at contact and would clip.
        uint8_t keep_burst = emr_burst_cycles;
        uint16_t keep_period = emr_period_us;
        emr_burst_cycles = (uint8_t)CFG(FREQ_BURST);
        emr_period_us = 0;                        // long measurements do not fit the grid
        uint8_t keep_mean = emr_mean_n;
        emr_mean_n = 1;                           // one sample, it gets averaged over checks anyway
        emr_set_gain(1);
        delay_us(CFG(GAIN_SETTLE_US));
        int sa = emr_measure(cx, cy, fc - 1);
        IDLE_HOOK();
        int sb = emr_measure(cx, cy, fc);
        IDLE_HOOK();
        int sc = emr_measure(cx, cy, fc + 1);
        IDLE_HOOK();
        emr_set_gain(0);
        emr_burst_cycles = keep_burst;
        emr_period_us = keep_period;
        emr_mean_n = keep_mean;
        delay_us(CFG(GAIN_SETTLE_US) + CFG(FREQ_PAUSE_US));
        IDLE_HOOK();
        emr_grid_reset();                         // the position windows start right after the pause
        int den = sa - 2 * sb + sc;
        if (den < 0 && sb > sa && sb > sc) {      // peak inside the window: interpolate it
            int off = 128 * (sa - sc) / den;
            int f256 = fc * 256 + (off < -256 ? -256 : off > 256 ? 256 : off);
            f256_smooth = (f256_smooth + f256) / 2;
            if (f256_smooth > fc * 256 + 160 && fc < EMR_FREQS - 2) fc++;
            else if (f256_smooth < fc * 256 - 160 && fc > 1) fc--;
            res_invalid_run = 0;
        } else {
            if (res_invalid_run < 100) res_invalid_run++;
            if (sc > sb && sc >= sa && sb > 0) {      // peak moved out of the window: follow it
                if (fc < EMR_FREQS - 2) fc++;
            } else if (sa > sb && sb > 0) {
                if (fc > 1) fc--;
            }
        }
    }

    // Tip is down above resonance index 8, and only if the last two checks found a real peak (without one the value means nothing).
    int f_rest256 = 256 * CFG(F_REST) / 1000, f_full256 = 256 * CFG(F_FULL) / 1000;
    int pr = (f256_smooth - f_rest256) * 8191 / (f_full256 - f_rest256);
    if (pr < 0) pr = 0;
    if (pr > 8191) pr = 8191;
    if (tip) {
        // letting go needs several checks in a row, otherwise one noisy check makes a double click
        int up = res_invalid_run >= CFG(TIP_INVALID_CHECKS) || f256_smooth < 256 * CFG(TIP_OFF) / 1000;
        if (!up) release_run = 0;
        else if (do_freq && ++release_run >= CFG(TIP_RELEASE_CHECKS)) tip = 0;
    } else if (res_invalid_run < 2 && f256_smooth > 256 * CFG(TIP_ON) / 1000) {
        if (do_freq && ++press_run >= CFG(TIP_PRESS_CHECKS)) {
            tip = 1;
            release_run = 0;
            press_run = 0;
        }
    } else {
        press_run = 0;
    }
    if (!tip) pr = 0;

    long xx = raw_x, yy = raw_y;
    filter_position(&xx, &yy, x1 + y1, x1 < y1 ? x1 : y1, !tip);      // dead-zone only while hovering
    if (xx > X_MAX) xx = X_MAX;
    if (yy > Y_MAX) yy = Y_MAX;
    // Outside the active area the pen counts as out of range (leaving needs 0.5 mm more than entering, so the edge does not flicker)
    long m = was_in ? 100 : 0;
    if (xx < area_x0 - m || xx > area_x1 + m || yy < area_y0 - m || yy > area_y1 + m) {
        tip = 0; release_run = 0; press_run = 0;
        if (!was_in) return 0;
        was_in = 0;
        s->in_range = 0; s->tip = 0; s->pressure = 0;
        s->x = last_x; s->y = last_y;
        return 1;
    }
    was_in = 1;
    if (CFG(FLIP_X)) xx = X_MAX - xx;
    if (CFG(FLIP_Y)) yy = Y_MAX - yy;

    s->in_range = 1; s->tip = tip; s->pressure = (uint16_t)(CFG(PRESSURE_GRADED) ? pr : (tip ? 8191 : 0));
    s->x = last_x = (uint32_t)xx; s->y = last_y = (uint32_t)yy;
    return 1;
}

