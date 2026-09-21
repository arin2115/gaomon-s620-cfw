// Runtime settings: one table of 16-bit values. Read one with CFG(NAME).
// They are changed over USB (feature report 0x30, tools/s620cfg.py) and can be saved to flash (settings.c).
// This list is the only place that defines them: the firmware and tools/s620cfg.py both read it.
// Add new settings at the END, so saved records stay valid.
// X(name, default, min, max, "description")
#ifndef SETTINGS_H
#define SETTINGS_H
#include <stdint.h>

#define SETTINGS_LIST(X) \
    X(SMOOTH_EMA,      150,   1,  256, "Exponential filter weight of the new sample, /256 (higher = less smoothing)") \
    X(SMOOTH_MA,         4,   1,    4, "Moving average over the last N reports before the exponential filter") \
    X(SMOOTH_FULL_AMP, 1200, 100, 4095, "Signal amplitude at and above which the exponential filter runs at full weight") \
    X(SMOOTH_MIN_W,     60,   1,  256, "Weakest exponential filter weight (weak signal = more smoothing), /256") \
    X(HOVER_ZONE_HI,    50,   0, 1000, "Hover dead-zone radius, strong signal (units, 200 = 1 mm)") \
    X(HOVER_ZONE_MID,   90,   0, 1000, "Hover dead-zone radius, medium signal") \
    X(HOVER_ZONE_LO,   150,   0, 1000, "Hover dead-zone radius, weak signal") \
    X(TIP_ON,         7980, 6000,12000, "Tip goes down above this resonance index (x1000; 8050 = 8.05)") \
    X(TIP_OFF,        7770, 6000,12000, "Tip goes up below this resonance index (x1000)") \
    X(F_REST,         7800, 6000,12000, "Resonance index at zero pressure (x1000), only used for graded pressure") \
    X(F_FULL,         9680, 6000,12000, "Resonance index at full pressure (x1000), only used for graded pressure") \
    X(PRESSURE_GRADED,   0,   0,    1, "0 = pressure is only 0 / 8191 (tip up / down), 1 = graded from the resonance shift") \
    X(FLIP_X,            0,   0,    1, "Mirror the X axis") \
    X(FLIP_Y,            1,   0,    1, "Mirror the Y axis") \
    X(FREQ_EVERY,        6,   1,  100, "Resonance (pressure) check every N reports") \
    X(DET_THRESHOLD,   200,  20, 2000, "Pen search: amplitude that counts as a pen") \
    X(LOST_THRESHOLD,  150,  20, 2000, "Tracking: amplitude below which the pen counts as gone (raised automatically above the leakage)") \
    X(AMP_HIGH,       3700,1000, 4095, "Limiter: shorten the burst above this amplitude") \
    X(AMP_LOW,         700,  50, 3000, "Limiter: lengthen the burst below this amplitude") \
    X(BURST_MIN,         6,   1,   29, "Shortest excitation burst the limiter may use (cycles)") \
    X(BURST_DEF,        18,   1,   29, "Normal excitation burst (cycles); shorter = faster") \
    X(BURST_MAX,        19,   1,   29, "Longest excitation burst the limiter may use (also sets the measurement grid)") \
    X(SETTLE,           18,   4,   60, "Settle time before/after the receiver switch (us); do not go much lower") \
    X(FREQ_BURST,       25,   6,   29, "Burst for the resonance (pressure) measurement (cycles)") \
    X(SEARCH_BURST,     24,   6,   29, "Burst for the pen search (cycles)") \
    X(GRID_MARGIN,       6,   0,   40, "Slack added to the measurement grid period (us)") \
    X(FREQ_PAUSE_US,    40,   0,  500, "Ring-down pause after the resonance measurement (us)") \
    X(GAIN_SETTLE_US,   60,   0,  500, "Time for the gain switch to settle (us)") \
    X(AREA_X0,           0,   0,33020, "Active area: left edge (units, 200 = 1 mm, as the tablet reports X)") \
    X(AREA_X1,       33020,   0,33020, "Active area: right edge") \
    X(AREA_Y0,           0,   0,20320, "Active area: top edge (units, 200 = 1 mm, as the tablet reports Y)") \
    X(AREA_Y1,       20320,   0,20320, "Active area: bottom edge") \
    X(LED_IDLE,        100,   0, 1000, "LED brightness when idle (0..1000)") \
    X(LED_ACTIVE,      940,   0, 1000, "LED brightness while the pen is tracked or a key is down") \
    X(KEYS_ENABLED,      1,   0,    1, "Express keys on/off") \
    X(KEY_POLL_MS,       4,   1,   50, "Express key polling interval (ms)") \
    X(KEY_LO_MAX,     1000,   0, 4095, "Key ladder: ADC value at or below this = key 1 / 3") \
    X(KEY_HI_MIN,     1100,   0, 4095, "Key ladder: lower edge of key 2 / 4") \
    X(KEY_HI_MAX,     1800,   0, 4095, "Key ladder: upper edge of key 2 / 4") \
    X(KEY_IDLE,       3851,   0, 4095, "Key ladder: ADC value at and above this = nothing pressed") \
    X(TIP_RELEASE_CHECKS, 1,  1,   10, "Tip goes up only after this many pressure checks in a row say so (stops double clicks)") \
    X(TIP_INVALID_CHECKS, 16, 1,  100, "Tip goes up after this many pressure checks in a row found no clear resonance peak (it happens at firm pressure)") \
    X(TIP_PRESS_CHECKS,   1,  1,   10, "Tip goes down only after this many pressure checks in a row are above the threshold (stops false clicks)") \
    X(MAX_RATE,        1000, 50, 1000, "Highest report rate sent to the computer (Hz). The tablet still scans as fast as it can, it just sends fewer reports")

enum {
#define X(n, d, lo, hi, s) SET_##n,
    SETTINGS_LIST(X)
#undef X
    SET_COUNT
};

#define SETTINGS_VERSION 3                       // 2: handled-commands counter and USB diagnostics, 3: the active area is in position units
#define SETTINGS_REPORT_ID 0x30
#define SETTINGS_PAYLOAD 95                      // bytes after the report ID; the values start at offset 4 (uint16, little endian)
#define SETTINGS_DIAG (SETTINGS_PAYLOAD - 3)     // the last 3 bytes: USB diagnostics (usb.c). At most 44 settings fit.

_Static_assert(4 + 2 * SET_COUNT <= SETTINGS_DIAG, "the settings no longer fit in the report before the diagnostics");

extern uint16_t cfg[SET_COUNT];
#define CFG(n) ((int)cfg[SET_##n])

// command in payload[0] when writing
enum { SCMD_NONE = 0, SCMD_APPLY = 1, SCMD_APPLY_SAVE = 2, SCMD_DEFAULTS = 3, SCMD_SAVE = 4, SCMD_RELOAD = 5, SCMD_FACTORY = 6 };
// status in payload[0] when reading
#define SSTAT_SAVED_VALID 1u                     // flash holds a valid record
#define SSTAT_DIRTY       2u                     // running settings differ from the saved ones
#define SSTAT_FAILED      4u                     // last save or erase failed

void settings_init(void);                        // load the saved record, else the defaults
void settings_submit(const uint8_t *payload, int len);   // from USB: queue a write or command
void settings_fill_report(uint8_t *out);         // fills SETTINGS_PAYLOAD bytes
int settings_service(void);                      // main loop: does the queued work, returns 1 if the settings changed

#endif
