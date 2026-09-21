// Pen scan engine
// One measurement: drive the TX coil with a burst, switch to the RX coil, wait, listen, read the ADC.
#include <stdint.h>
#include "emr.h"
#include "settings.h"

#define REG(a) (*(volatile uint32_t *)(a))

#define RCU        0x40021000u
#define RCU_CFG0   REG(RCU + 0x04)
#define RCU_AHBEN  REG(RCU + 0x14)
#define RCU_APB2EN REG(RCU + 0x18)
#define RCU_CFG2   REG(RCU + 0x30)

#define GPIOA      0x48000000u
#define GPIOB      0x48000400u
#define GCTL(p)    REG((p) + 0x00)
#define GOTYPE(p)  REG((p) + 0x04)
#define GOSPD(p)   REG((p) + 0x08)
#define GODR(p)    REG((p) + 0x14)
#define GBOP(p)    REG((p) + 0x18)
#define GBC(p)     REG((p) + 0x28)

#define ADC        0x40012400u
#define ADC_STAT   REG(ADC + 0x00)
#define ADC_CTL0   REG(ADC + 0x04)
#define ADC_CTL1   REG(ADC + 0x08)
#define ADC_SAMPT1 REG(ADC + 0x10)
#define ADC_RSQ0   REG(ADC + 0x2C)
#define ADC_RSQ2   REG(ADC + 0x34)
#define ADC_RDATA  REG(ADC + 0x4C)

#define DEMCR      REG(0xE000EDFCu)
#define DWT_CTRL   REG(0xE0001000u)
#define DWT_CYCCNT REG(0xE0001004u)

#define PA_DRV_EN  (1u << 3)     // coil driver enable
#define PA_COIL    (1u << 15)    // coil drive output
#define PA_SEL     0x00E0u       // PA5..7: 4051 channel select
#define PB_RXSW    (1u << 6)     // receiver switch (low = listening)
#define PB_GAIN    (1u << 7)     // high = attenuate
#define PB_EN      0x003Au       // PB1,3,4,5: 4051 chip enables (active low)

uint16_t emr_settle_a = 18;
uint16_t emr_settle_b = 18;
uint8_t emr_burst_cycles = 18;
uint16_t emr_period_us = 130;   // 0 = no grid, otherwise measurements start every emr_period_us
static uint32_t next_start;
uint8_t emr_mean_n = 0;         // 0 = median of 3 samples, N = mean of N samples

void delay_us(uint32_t us)
{
    uint32_t t0 = DWT_CYCCNT, n = us * 72u;
    while ((DWT_CYCCNT - t0) < n) { }
}

static void pin_out(uint32_t port, uint32_t pins, uint32_t speed)
{
    for (uint32_t i = 0; i < 16; i++) {
        if (!(pins & (1u << i))) continue;
        GCTL(port)  = (GCTL(port) & ~(3u << (2 * i))) | (1u << (2 * i));
        GOTYPE(port) &= ~(1u << i);
        GOSPD(port) = (GOSPD(port) & ~(3u << (2 * i))) | (speed << (2 * i));
    }
}

// Times a measurement and returns the grid period (us). A period shorter than a measurement halves the report rate.
static uint16_t emr_calibrate_period(int margin_us)
{
    emr_period_us = 0;
    uint32_t worst = 0;
    for (int i = 0; i < 8; i++) {
        uint32_t t0 = DWT_CYCCNT;
        emr_measure(&X_COILS[8], &X_COILS[8], 8);
        uint32_t d = DWT_CYCCNT - t0;
        if (d > worst) worst = d;
    }
    uint32_t us = (worst + 71u) / 72u + (uint32_t)margin_us;
    return (uint16_t)(us > 250u ? 250u : us);
}

int emr_init(void)
{
    DEMCR |= 1u << 24;                           // enable the cycle counter
    DWT_CTRL |= 1u;
    uint32_t c0 = DWT_CYCCNT;
    for (volatile int i = 0; i < 50; i++) { }
    if (DWT_CYCCNT == c0)
        return 1;

    RCU_AHBEN |= (1u << 17) | (1u << 18);        // GPIOA, GPIOB
    RCU_APB2EN |= (1u << 9);                     // ADC

    // pin states
    pin_out(GPIOA, PA_SEL, 0);
    GBC(GPIOA) = PA_SEL;
    pin_out(GPIOA, PA_DRV_EN, 0);
    pin_out(GPIOB, PB_RXSW, 0);
    GBOP(GPIOB) = PB_RXSW;
    GBC(GPIOA) = PA_DRV_EN;
    pin_out(GPIOB, PB_EN, 0);
    GODR(GPIOB) &= 0xFFC5u;
    pin_out(GPIOB, PB_GAIN, 1);
    GBC(GPIOB) = PB_GAIN;
    GCTL(GPIOA) |= (3u << 8) | 0xFu;             // PA4 (pen signal), PA0/PA1 (keys) analog

    // ADC clock 12 MHz
    RCU_CFG0 = (RCU_CFG0 & ~0xC000u) | 0x8000u;
    RCU_CFG2 |= 0x100u;

    ADC_CTL1 = 1u;                               // ADC on
    delay_us(20);
    ADC_CTL1 |= 1u << 3;                         // reset calibration
    for (uint32_t t = 0; (ADC_CTL1 & (1u << 3)) && t < 1000000u; t++) { }
    ADC_CTL1 |= 1u << 2;                         // calibrate
    for (uint32_t t = 0; (ADC_CTL1 & (1u << 2)) && t < 1000000u; t++) { }
    ADC_RSQ0 &= ~(0xFu << 20);                   // one conversion
    ADC_RSQ2 = 4;                                // channel 4 = PA4
    ADC_SAMPT1 = (ADC_SAMPT1 & ~(7u << 12)) | (5u << 12);
    ADC_CTL1 = (ADC_CTL1 & ~(7u << 17) & ~(1u << 11)) | (7u << 17) | (1u << 20) | 1u;   // software trigger
    ADC_CTL0 |= 0x100u;

    emr_configure();
    return 0;
}

// Applies the timing settings and re-times the grid. Main loop only.
void emr_configure(void)
{
    emr_settle_a = emr_settle_b = (uint16_t)cfg[SET_SETTLE];
    emr_burst_cycles = (uint8_t)cfg[SET_BURST_MAX];              // the longest burst is the slowest measurement
    emr_period_us = emr_calibrate_period(cfg[SET_GRID_MARGIN]);
    emr_burst_cycles = (uint8_t)cfg[SET_BURST_DEF];
    emr_grid_reset();
}

// Reads another ADC channel (express keys), then restores the pen channel.
uint16_t emr_adc_read(int ch)
{
    uint32_t rsq2 = ADC_RSQ2, sampt = ADC_SAMPT1;
    ADC_RSQ2 = (rsq2 & ~0x1Fu) | (uint32_t)ch;
    ADC_SAMPT1 = (sampt & ~(7u << (3 * ch))) | (7u << (3 * ch));      // longest sample time, the key ladder is high impedance
    ADC_STAT &= ~2u;
    ADC_CTL1 |= 1u << 22;
    uint16_t v = 0xFFFu;
    for (uint32_t t = 0; t < 20000u; t++)
        if (ADC_STAT & 2u) { v = (uint16_t)(ADC_RDATA & 0xFFFu); break; }
    ADC_STAT &= ~2u;
    ADC_RSQ2 = rsq2; ADC_SAMPT1 = sampt;
    return v;
}

static inline uint16_t adc_sample(void)
{
    ADC_CTL1 |= 1u << 22;                        // start conversion
    for (uint32_t t = 0; !(ADC_STAT & 2u); t++)
        if (t > 20000u)
            return 0;
    ADC_STAT &= ~2u;
    return (uint16_t)(ADC_RDATA & 0xFFFu);
}

// The 12 burst frequencies: PA15 low, n1 nops, PA15 high, n2 nops, repeat. The nop counts set the frequency.
#define BURST(k, n1, n2)                                                                      \
    static void burst##k(void)                                                                   \
    {                                                                                          \
        register uint32_t cnt __asm__("r4") = emr_burst_cycles;                                \
        __asm__ volatile(                                                                      \
            "1:\n"                                                                             \
            "mov.w r0, #0x8000\n mov.w r1, #0x48000000\n str r0, [r1, #0x28]\n"           \
            ".rept %c[a]\n nop\n .endr\n"                                                      \
            "mov.w r0, #0x8000\n mov.w r1, #0x48000000\n str r0, [r1, #0x18]\n"          \
            ".rept %c[b]\n nop\n .endr\n"                                                      \
            "subs r0, %[c], #0\n sub.w r1, %[c], #1\n uxtb %[c], r1\n bne.w 1b\n"              \
            : [c] "+r"(cnt) : [a] "i"(n1), [b] "i"(n2) : "r0", "r1", "cc", "memory");          \
    }

BURST(0, 75, 71)
BURST(1, 74, 68)
BURST(2, 72, 68)
BURST(3, 71, 65)
BURST(4, 69, 65)
BURST(5, 68, 62)
BURST(6, 67, 63)
BURST(7, 66, 61)
BURST(8, 65, 60)
BURST(9, 64, 59)
BURST(10, 63, 58)
BURST(11, 62, 56)

static void (*const bursts[EMR_FREQS])(void) = {
    burst0, burst1, burst2, burst3, burst4, burst5, burst6, burst7, burst8, burst9, burst10, burst11,
};

static void excite(int freq)
{
    uint32_t primask;
    __asm__ volatile("mrs %0, primask\n cpsid i" : "=r"(primask) :: "memory");
    GCTL(GPIOA) = (GCTL(GPIOA) & ~(3u << 30)) | (1u << 30);      // PA15 output
    GOTYPE(GPIOA) &= ~PA_COIL;
    GOSPD(GPIOA) |= 3u << 30;
    GBC(GPIOA) = PA_COIL;                                        // start low
    GBOP(GPIOA) = PA_DRV_EN;
    bursts[freq]();
    GBOP(GPIOA) = PA_COIL;                                       // end high
    GCTL(GPIOA) &= ~(3u << 30);                                  // release PA15
    GBC(GPIOA) = PA_DRV_EN;
    __asm__ volatile("msr primask, %0" :: "r"(primask) : "memory");
}

static uint16_t median3(uint16_t a, uint16_t b, uint16_t c)
{
    uint16_t lo = a < b ? a : b, hi = a < b ? b : a;
    uint16_t m = hi < c ? hi : c;
    return lo > m ? lo : m;
}

static uint8_t grid_fresh;
// The next measurement starts right away and the grid continues from there.
void emr_grid_reset(void)
{
    grid_fresh = 1;
}

uint16_t emr_measure(const coil_t *tx, const coil_t *rx, int freq)
{
    if (emr_period_us) {
        // Fixed time grid: the pen rings after each burst and this adds to the next one depending on the gap, so the gap must not vary.
        uint32_t period = emr_period_us * 72u;
        int32_t late;
        if (grid_fresh) { next_start = DWT_CYCCNT; grid_fresh = 0; late = 0; }
        else late = (int32_t)(DWT_CYCCNT - next_start);
        if (late > (int32_t)(period * 1000u) || late < -(int32_t)(period * 4u))
            next_start = DWT_CYCCNT;                              // grid lost, restart it
        else if (late > 0)
            next_start += ((uint32_t)late + period - 1) / period * period;
        while ((int32_t)(DWT_CYCCNT - next_start) < 0) { }
        next_start += period;
    }
    GODR(GPIOB) &= tx->pb_and;
    GODR(GPIOA) = (GODR(GPIOA) & 0xFF1Fu) | tx->pa_or;
    excite(freq);
    GODR(GPIOB) |= PB_EN;                                      // all multiplexers off
    GODR(GPIOA) = (GODR(GPIOA) & 0xFF1Fu) | rx->pa_or;
    GODR(GPIOB) &= rx->pb_and;

    delay_us(emr_settle_a);
    GBC(GPIOB) = PB_RXSW;                                      // listen
    delay_us(emr_settle_b);
    uint16_t result;
    if (emr_mean_n) {
        uint32_t sum = 0;
        for (int i = 0; i < emr_mean_n; i++) sum += adc_sample();
        result = (uint16_t)(sum / emr_mean_n);
    } else {
        uint16_t a = adc_sample(), b = adc_sample(), c = adc_sample();
        result = median3(a, b, c);
    }
    GBOP(GPIOB) = PB_RXSW;
    GODR(GPIOB) |= PB_EN;
    return result;
}

void emr_set_gain(int attenuate)
{
    if (attenuate) GBOP(GPIOB) = PB_GAIN; else GBC(GPIOB) = PB_GAIN;
}
