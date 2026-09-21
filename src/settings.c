// Runtime settings, saved in the last flash page.
// Record at 0x0800F800: magic "S620", version, count, count x u16 values, padding to 4 bytes, CRC32 (zlib) of all of that.
// Flash is only written on an explicit save. The erase stalls the CPU for tens of ms, so it runs from the main loop.
// A record with fewer values than this firmware has is still valid, the missing ones use their defaults.
#include <stdint.h>
#include "settings.h"

#define SETTINGS_PAGE 0x0800F800u
#define MAGIC 0x30323653u                        // "S620"

uint16_t cfg[SET_COUNT];

static const uint16_t DEF[SET_COUNT] = {
#define X(n, d, lo, hi, s) d,
    SETTINGS_LIST(X)
#undef X
};
static const uint16_t LO[SET_COUNT] = {
#define X(n, d, lo, hi, s) lo,
    SETTINGS_LIST(X)
#undef X
};
static const uint16_t HI[SET_COUNT] = {
#define X(n, d, lo, hi, s) hi,
    SETTINGS_LIST(X)
#undef X
};

static uint16_t saved[SET_COUNT];                // values in the flash record
static uint8_t saved_valid, failed;
static uint8_t staged[SETTINGS_PAYLOAD];
static volatile uint8_t staged_cmd;              // 0 = nothing queued
static uint8_t handled;                          // commands handled so far (wraps), lets the host see that a write arrived

// flash access
#define REG(a) (*(volatile uint32_t *)(a))
#define FMC_KEY  REG(0x40022004u)
#define FMC_STAT REG(0x4002200Cu)
#define FMC_CTL  REG(0x40022010u)
#define FMC_ADDR REG(0x40022014u)
#define FLASH_BYTES ((const uint8_t *)SETTINGS_PAGE)

static int fmc_wait(void)
{
    for (uint32_t t = 0; t < 4000000u; t++)
        if (!(FMC_STAT & 1u)) return (FMC_STAT & 0x14u) ? 1 : 0;      // program or write-protect error
    return 1;
}
static void fmc_unlock(void)
{
    if (FMC_CTL & (1u << 7)) { FMC_KEY = 0x45670123u; FMC_KEY = 0xCDEF89ABu; }
    FMC_STAT = 0x34u;                                                 // clear flags
}
static int flash_erase(void)
{
    fmc_unlock();
    FMC_CTL |= 2u;                                                    // page erase
    FMC_ADDR = SETTINGS_PAGE;
    FMC_CTL |= 1u << 6;                                               // start
    int rc = fmc_wait();
    FMC_CTL &= ~2u;
    FMC_CTL |= 1u << 7;                                               // lock
    return rc;
}
static int flash_program(uint32_t off, uint16_t v)
{
    fmc_unlock();
    FMC_CTL |= 1u;                                                    // program
    *(volatile uint16_t *)(SETTINGS_PAGE + off) = v;
    int rc = fmc_wait();
    FMC_CTL &= ~1u;
    FMC_CTL |= 1u << 7;
    return rc;
}

static uint32_t crc32(const uint8_t *p, int n)
{
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int i = 0; i < 8; i++) c = (c >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(c & 1));
    }
    return ~c;
}

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static int rd16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t crc_offset(int count) { return (8u + 2u * (uint32_t)count + 3u) & ~3u; }

static void sanitize(uint16_t *v)
{
    for (int i = 0; i < SET_COUNT; i++)
        if (v[i] < LO[i]) v[i] = LO[i]; else if (v[i] > HI[i]) v[i] = HI[i];
    if (v[SET_AREA_X0] > v[SET_AREA_X1]) v[SET_AREA_X0] = v[SET_AREA_X1];
    if (v[SET_AREA_Y0] > v[SET_AREA_Y1]) v[SET_AREA_Y0] = v[SET_AREA_Y1];
    if (v[SET_BURST_MIN] > v[SET_BURST_DEF]) v[SET_BURST_MIN] = v[SET_BURST_DEF];
    if (v[SET_BURST_MAX] < v[SET_BURST_DEF]) v[SET_BURST_MAX] = v[SET_BURST_DEF];
    if (v[SET_TIP_OFF] > v[SET_TIP_ON]) v[SET_TIP_OFF] = v[SET_TIP_ON];
    if (v[SET_F_FULL] <= v[SET_F_REST]) v[SET_F_FULL] = (uint16_t)(v[SET_F_REST] + 1);
    if (v[SET_KEY_HI_MIN] > v[SET_KEY_HI_MAX]) v[SET_KEY_HI_MIN] = v[SET_KEY_HI_MAX];
}

// Reads the flash record into out[]. Returns 1 if it is valid.
static int load_record(uint16_t *out)
{
    const uint8_t *f = FLASH_BYTES;
    if (rd32(f) != MAGIC || rd16(f + 4) < 1 || rd16(f + 4) > SETTINGS_VERSION) return 0;   // older records have the same layout, apart from the area
    int count = rd16(f + 6);
    if (count < 1 || count > 96) return 0;
    uint32_t co = crc_offset(count);
    if (rd32(f + co) != crc32(f, (int)co)) return 0;
    for (int i = 0; i < SET_COUNT; i++) out[i] = i < count ? (uint16_t)rd16(f + 8 + 2 * i) : DEF[i];
    if (rd16(f + 4) < 3) {                                            // before version 3 the area was a coil range: back to the whole tablet
        out[SET_AREA_X0] = DEF[SET_AREA_X0]; out[SET_AREA_X1] = DEF[SET_AREA_X1];
        out[SET_AREA_Y0] = DEF[SET_AREA_Y0]; out[SET_AREA_Y1] = DEF[SET_AREA_Y1];
    }
    sanitize(out);
    return 1;
}

static int save_record(void)
{
    uint8_t rec[8 + 2 * SET_COUNT + 4 + 4];
    int n = 8 + 2 * SET_COUNT;
    rec[0] = (uint8_t)MAGIC; rec[1] = (uint8_t)(MAGIC >> 8); rec[2] = (uint8_t)(MAGIC >> 16); rec[3] = (uint8_t)(MAGIC >> 24);
    rec[4] = SETTINGS_VERSION; rec[5] = 0; rec[6] = SET_COUNT; rec[7] = 0;
    for (int i = 0; i < SET_COUNT; i++) { rec[8 + 2 * i] = (uint8_t)cfg[i]; rec[9 + 2 * i] = (uint8_t)(cfg[i] >> 8); }
    uint32_t co = crc_offset(SET_COUNT);
    for (int i = n; i < (int)co; i++) rec[i] = 0xFF;
    uint32_t c = crc32(rec, (int)co);
    for (int i = 0; i < 4; i++) rec[co + i] = (uint8_t)(c >> (8 * i));
    int total = (int)co + 4;
    if (flash_erase()) return 1;
    for (int i = 0; i < total; i += 2)
        if (flash_program((uint32_t)i, (uint16_t)(rec[i] | rec[i + 1] << 8))) return 1;
    for (int i = 0; i < total; i++)                                    // verify
        if (FLASH_BYTES[i] != rec[i]) return 1;
    for (int i = 0; i < SET_COUNT; i++) saved[i] = cfg[i];
    saved_valid = 1;
    return 0;
}

static void set_defaults(void) { for (int i = 0; i < SET_COUNT; i++) cfg[i] = DEF[i]; sanitize(cfg); }

void settings_init(void)
{
    saved_valid = (uint8_t)load_record(saved);
    if (saved_valid) for (int i = 0; i < SET_COUNT; i++) cfg[i] = saved[i];
    else set_defaults();
}

void settings_submit(const uint8_t *p, int len)
{
    if (len > SETTINGS_PAYLOAD) len = SETTINGS_PAYLOAD;
    for (int i = 0; i < len; i++) staged[i] = p[i];
    for (int i = len; i < SETTINGS_PAYLOAD; i++) staged[i] = 0;
    staged_cmd = staged[0];
}

void settings_fill_report(uint8_t *out)
{
    int dirty = !saved_valid;
    for (int i = 0; i < SET_COUNT && !dirty; i++) if (cfg[i] != saved[i]) dirty = 1;
    for (int i = 0; i < SETTINGS_PAYLOAD; i++) out[i] = 0;
    out[0] = (uint8_t)((saved_valid ? SSTAT_SAVED_VALID : 0) | (dirty ? SSTAT_DIRTY : 0) | (failed ? SSTAT_FAILED : 0));
    out[1] = SETTINGS_VERSION;
    out[2] = SET_COUNT;
    out[3] = handled;
    for (int i = 0; i < SET_COUNT; i++) { out[4 + 2 * i] = (uint8_t)cfg[i]; out[5 + 2 * i] = (uint8_t)(cfg[i] >> 8); }
}

int settings_service(void)
{
    uint8_t cmd = staged_cmd;
    if (!cmd) return 0;
    staged_cmd = 0;
    handled++;
    int changed = 0;
    if (cmd == SCMD_APPLY || cmd == SCMD_APPLY_SAVE) {
        uint16_t v[SET_COUNT];
        int count = staged[2] < SET_COUNT ? staged[2] : SET_COUNT;   // a shorter list from an older tool leaves the rest unchanged
        for (int i = 0; i < SET_COUNT; i++) v[i] = i < count ? (uint16_t)(staged[4 + 2 * i] | staged[5 + 2 * i] << 8) : cfg[i];
        sanitize(v);
        for (int i = 0; i < SET_COUNT; i++) { if (v[i] != cfg[i]) changed = 1; cfg[i] = v[i]; }
    } else if (cmd == SCMD_DEFAULTS || cmd == SCMD_FACTORY) {
        uint16_t old[SET_COUNT];
        for (int i = 0; i < SET_COUNT; i++) old[i] = cfg[i];
        set_defaults();
        for (int i = 0; i < SET_COUNT; i++) if (old[i] != cfg[i]) changed = 1;
        if (cmd == SCMD_FACTORY) { failed = (uint8_t)flash_erase(); saved_valid = 0; }
    } else if (cmd == SCMD_RELOAD) {
        uint16_t v[SET_COUNT];
        if (load_record(v)) {
            for (int i = 0; i < SET_COUNT; i++) { if (v[i] != cfg[i]) changed = 1; cfg[i] = v[i]; }
            failed = 0;
        } else failed = 1;
    }
    if (cmd == SCMD_APPLY_SAVE || cmd == SCMD_SAVE) failed = (uint8_t)save_record();
    return changed;
}
