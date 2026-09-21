// Main loop: clock setup, status LED, express keys, USB reports.
// Fault codes: 2.5 s solid on, then N blinks.
//   1 crystal  2 PLL  3 clock switch  4-6 USB init  7 CPU fault  8 cycle counter
// Before USB is configured, the LED shows the USB state instead (see status_led).
#include <stdint.h>
#include "usb.h"
#include "emr.h"
#include "pentrack.h"
#include "pen.h"
#include "settings.h"

#define REG(a) (*(volatile uint32_t *)(a))

#define RCU        0x40021000u
#define RCU_CTL    REG(RCU + 0x00)
#define RCU_CFG0   REG(RCU + 0x04)
#define RCU_AHBEN  REG(RCU + 0x14)
#define FMC_WS     REG(0x40022000u + 0x00)

#define GPIOA      0x48000000u
#define GPIOA_CTL  REG(GPIOA + 0x00)
#define GPIOA_OSPD REG(GPIOA + 0x08)
#define GPIOA_BOP  REG(GPIOA + 0x18)

#define SYST_CSR   REG(0xE000E010u)
#define SYST_RVR   REG(0xE000E014u)
#define SYST_CVR   REG(0xE000E018u)

static uint32_t core_mhz = 8;
static int tick_running;
static uint32_t ms_count, ms_last, ms_frac;

#define DWT_CYCCNT REG(0xE0001004u)

// No timer interrupt: one landing inside a measurement adds position noise. Milliseconds come from the cycle counter,
// so millis() must be called at least once a minute.
static uint32_t millis(void)
{
    if (tick_running) {
        uint32_t now = DWT_CYCCNT;
        ms_frac += now - ms_last;
        ms_last = now;
        ms_count += ms_frac / 72000u;
        ms_frac %= 72000u;
    }
    return ms_count;
}

static void systick_poll_mode(void)         // delays use the SysTick counter, without interrupt
{
    tick_running = 0;
    SYST_CSR = 0;
}

static void millis_start(void)
{
    SYST_CSR = 0;
    ms_last = DWT_CYCCNT;
    tick_running = 1;
}

void delay_ms(uint32_t ms)
{
    if (tick_running) {
        uint32_t t0 = millis();
        while ((millis() - t0) < ms) { }
        return;
    }
    SYST_RVR = core_mhz * 1000u - 1u;
    SYST_CVR = 0;
    SYST_CSR = 5;
    while (ms--)
        while (!(SYST_CSR & (1u << 16))) { }
    SYST_CSR = 0;
}

#include "keys.h"

#define RCU_APB1EN REG(RCU + 0x1C)
#define GPIOA_AFSEL0 REG(GPIOA + 0x20)
#define TIMER1     0x40000000u
#define T1_CTL0    REG(TIMER1 + 0x00)
#define T1_SWEVG   REG(TIMER1 + 0x14)
#define T1_CHCTL1  REG(TIMER1 + 0x1C)
#define T1_CHCTL2  REG(TIMER1 + 0x20)
#define T1_PSC     REG(TIMER1 + 0x28)
#define T1_CAR     REG(TIMER1 + 0x2C)
#define T1_CH2CV   REG(TIMER1 + 0x3C)

static int led_pwm_on;
static int led_last = -1;                                    // last PWM level (-1 = none)

static void led_init(void)
{
    RCU_AHBEN |= (1u << 17);                                 // GPIOA clock
    GPIOA_CTL = (GPIOA_CTL & ~(3u << 4)) | (1u << 4);        // PA2 output
    GPIOA_OSPD |= (3u << 4);
}

// The LED is active-low
static void led(int on)
{
    if (led_pwm_on) {                                        // back to plain GPIO
        GPIOA_CTL = (GPIOA_CTL & ~(3u << 4)) | (1u << 4);
        led_pwm_on = 0;
    }
    GPIOA_BOP = on ? (1u << 18) : (1u << 2);
}

// PWM brightness on TIMER1 CH2 (PA2), 100 Hz
static void led_pwm(int brightness)
{
    if (!led_pwm_on) {
        RCU_APB1EN |= 1u;
        T1_PSC = 719; T1_CAR = 999;
        T1_CHCTL1 = (T1_CHCTL1 & ~0xFFu) | 0x68;             // CH2: PWM mode 0
        T1_CHCTL2 |= 1u << 8;                                // CH2 output on
        T1_CH2CV = 1000;
        T1_CTL0 |= (1u << 7) | 1u;                           // start the counter
        T1_SWEVG = 1;
        GPIOA_AFSEL0 = (GPIOA_AFSEL0 & ~(0xFu << 8)) | (2u << 8);
        GPIOA_CTL = (GPIOA_CTL & ~(3u << 4)) | (2u << 4);    // PA2 alternate function
        led_pwm_on = 1;
    }
    T1_CH2CV = (uint32_t)(1000 - brightness);                // active-low
}

static void fault_code(int code)                             // never returns
{
    systick_poll_mode();
    for (;;) {
        led(1); delay_ms(2500); led(0); delay_ms(1000);
        for (int i = 0; i < code; i++) { led(1); delay_ms(400); led(0); delay_ms(400); }
        delay_ms(2000);
    }
}

void HardFault_Handler(void) { fault_code(7); }

#define WAIT_LIMIT 3000000u

static int clock_init_72mhz(void)
{
    RCU_CTL |= 1u;                                           // internal 8 MHz oscillator on
    for (uint32_t t = 0; !(RCU_CTL & 2u); t++)
        if (t > WAIT_LIMIT)
            return 3;
    RCU_CFG0 &= 0x08FF000Cu;                                 // run from the internal oscillator, clear prescalers and PLL
    RCU_CFG0 &= 0x77C2FFFFu;
    RCU_CFG0 &= ~0x00C00000u;
    REG(RCU + 0x30) &= 0xBFFFFFBFu;
    RCU_CTL &= 0xFEF2FFFFu;                                  // crystal, PLL and clock monitor off
    REG(RCU + 0x2C) &= 0x3FFFFFF0u;
    REG(RCU + 0x30) &= ~0x00000103u;
    REG(RCU + 0x30) &= ~0x00010000u;
    REG(RCU + 0x30) &= ~0x80000000u;
    REG(RCU + 0x34) &= ~1u;
    REG(RCU + 0x08) = 0;                                     // clock interrupts off
    REG(RCU + 0xCC) = 0;
    core_mhz = 8;

    RCU_CTL |= (1u << 16);                                   // crystal on
    for (uint32_t t = 0; !(RCU_CTL & (1u << 17)); t++)       // wait for the crystal
        if (t > WAIT_LIMIT)
            return 1;

    FMC_WS = 2;
    uint32_t c = RCU_CFG0;
    c &= 0x77C2FFFFu;
    c &= ~(3u << 22);
    c &= ~(0x7u << 8);  c |= (4u << 8);
    c &= ~(0x7u << 11); c |= (4u << 11);
    c |= (1u << 16) | (4u << 18);
    RCU_CFG0 = c;

    RCU_CTL |= (1u << 24);
    for (uint32_t t = 0; !(RCU_CTL & (1u << 25)); t++)
        if (t > WAIT_LIMIT)
            return 2;

    RCU_CFG0 = (RCU_CFG0 & ~3u) | 2u;
    for (uint32_t t = 0; ((RCU_CFG0 >> 2) & 3u) != 2u; t++)
        if (t > WAIT_LIMIT)
            return 3;
    core_mhz = 72;
    return 0;
}

// LED level at time t (ms) of the repeating USB status pattern:
// n fast blinks (1 waiting, 2 reset seen, 3 setup seen, 4 addressed), a gap, then m medium blinks = setup requests so far.
static int status_led(uint32_t t, int n, int m)
{
    uint32_t g1 = 200u * (uint32_t)n;
    uint32_t g2 = 600u * (uint32_t)m;
    uint32_t period = g1 + 800u + g2 + 1500u;
    t %= period;
    if (t < g1) return (t % 200u) < 100u;
    t -= g1;
    if (t < 800u) return 0;
    t -= 800u;
    if (t < g2) return (t % 600u) < 300u;
    return 0;
}

// One report per USB frame with the freshest sample. Also called from inside the long resonance check so no frame is missed.
static pen_sample_t latest;
static int have_latest, sof_pending;

static uint8_t key_mask;
static int key_pending;

static void service_usb(void)
{
    if (usb_sof()) sof_pending = 1;
    if (key_pending && sof_pending) {                // a key change takes this frame's slot
        if (usb_report_mode() != 8) key_pending = 0;   // key reports only exist in raw mode
        else {
            uint8_t k[12] = { 8, 0xE0, 1, 1, key_mask, 0, 0, 0, 0, 0, 0, 0 };
            if (usb_send(1, k, 12) == 0) { key_pending = 0; sof_pending = 0; }
            return;
        }
    }
    if (have_latest && sof_pending) {
        sof_pending = 0;
        int rc;
        rc = pen_send(latest.x, latest.y, latest.pressure, latest.in_range, latest.tip, 0, 0);
        if (rc == 0) have_latest = 0;
    }
}

int main(void)
{
    led_init();
    led(1);
    int rc = clock_init_72mhz();
    if (rc)
        fault_code(rc);
    settings_init();
    delay_ms(1000);                                          // solid on for 1 s = running
    led(0);
    delay_ms(300);

    if (emr_init())
        fault_code(8);

    int urc = usb_init();
    if (urc)
        fault_code(3 + urc);                                 // codes 4..6

    millis_start();
    pentrack_idle_hook = service_usb;
    for (;;) {
        int st = usb_poll();
        uint32_t now = millis();
        if (st == USB_STATE_CONFIGURED) {
            static int in_range;
            static uint32_t next_key_poll;
            if ((int32_t)(now - next_key_poll) >= 0) {
                next_key_poll = now + (uint32_t)CFG(KEY_POLL_MS);
                uint8_t k = CFG(KEYS_ENABLED) ? keys_read() : 0;
                if (k != key_mask) { key_mask = k; key_pending = 1; }
            }
            {   // bright while the pen is tracked or a key is down
                int bright = in_range || key_mask ? CFG(LED_ACTIVE) : CFG(LED_IDLE);
                if (bright != led_last) { led_pwm(bright); led_last = bright; }
            }
            pen_sample_t ps;
            if (pentrack_step(&ps)) {
                in_range = ps.in_range;
                latest = ps; have_latest = 1;              // more samples than frames: keep the freshest
            }
            service_usb();
            if (settings_service()) {                      // new values from the host
                emr_configure();
                pentrack_settings_changed();
                led_last = -1;
            }
            continue;
        }
        led_last = -1;
        int n = 1;
        if (st == USB_STATE_ADDRESSED)        n = 4;
        else if (usb_stat_setups())           n = 3;
        else if (usb_stat_resets())           n = 2;
        uint32_t s = usb_stat_setups();
        led(status_led(now, n, s > 9 ? 9 : (int)s));
    }
}
