// Polled USB full-speed device for the GD32F350 USB core (DWC2-style).
//   interface 0: vendor (report 8 = raw pen, 0x16 = feature, 0x30 = settings), endpoint 1 IN + OUT
//   interface 1: digitizer pen (report 0x0A), endpoint 2 IN
//   interface 2: keyboard (report 3), endpoint 3 IN
#include <stdint.h>
#include "usb.h"
#include "hid_desc.h"
#include "orig_desc.h"
#include "settings.h"

#define REG(a) (*(volatile uint32_t *)(a))

#define USB_BASE 0x50000000u
#define GUSBCFG   REG(USB_BASE + 0x00C)
#define GRSTCTL   REG(USB_BASE + 0x010)
#define GINTSTS   REG(USB_BASE + 0x014)
#define GRXSTSP   REG(USB_BASE + 0x020)
#define GRXFSIZ   REG(USB_BASE + 0x024)
#define GNPTXFSIZ REG(USB_BASE + 0x028)
#define GCCFG     REG(USB_BASE + 0x038)
#define DIEPTXF(n) REG(USB_BASE + 0x100 + 4u * (n))     // n = 1..3

#define DCFG      REG(USB_BASE + 0x800)
#define DCTL      REG(USB_BASE + 0x804)
#define DIEPMSK   REG(USB_BASE + 0x810)
#define DOEPMSK   REG(USB_BASE + 0x814)
#define DAINTMSK  REG(USB_BASE + 0x81C)

#define DIEPCTL(n)  REG(USB_BASE + 0x900 + 0x20u * (n))
#define DIEPINT(n)  REG(USB_BASE + 0x908 + 0x20u * (n))
#define DIEPTSIZ(n) REG(USB_BASE + 0x910 + 0x20u * (n))
#define DOEPCTL(n)  REG(USB_BASE + 0xB00 + 0x20u * (n))
#define DOEPINT(n)  REG(USB_BASE + 0xB08 + 0x20u * (n))
#define DOEPTSIZ(n) REG(USB_BASE + 0xB10 + 0x20u * (n))

#define PWRCLKCTL REG(USB_BASE + 0xE00)
#define FIFO(n)   ((volatile uint32_t *)(USB_BASE + 0x1000u + 0x1000u * (n)))

#define RCU_AHBRST REG(0x40021000u + 0x28)
#define RCU_AHBEN  REG(0x40021000u + 0x14)
#define RCU_CFG0   REG(0x40021000u + 0x04)

#define GINT_RXFLVL  (1u << 4)
#define GINT_USBRST  (1u << 12)
#define GINT_ENUMDNE (1u << 13)

#define EPENA (1u << 31)
#define CNAK  (1u << 26)
#define SNAK  (1u << 27)
#define STALL (1u << 21)

// endpoint 0 uses 64-byte packets
static uint8_t DEV_DESC[18];
static const uint8_t *const RD_TABLE[3] = { RD_VENDOR, RD_PEN, RD_KBD };
static const uint16_t RD_LEN[3] = { sizeof(RD_VENDOR), sizeof(RD_PEN), sizeof(RD_KBD) };
static const uint8_t HID_OFF[3] = { HID_OFF0, HID_OFF1, HID_OFF2 };
static const uint8_t EP_SIZE[4] = { 0, 16, 16, 64 };     // IN endpoints 1..3

static const uint8_t STR_MANUF[] = { 14, 3, 'G', 0, 'A', 0, 'O', 0, 'M', 0, 'O', 0, 'N', 0 };
static const uint8_t STR_PRODUCT[] = { 28, 3, 'G', 0, 'a', 0, 'o', 0, 'm', 0, 'o', 0, 'n', 0, ' ', 0,
                                       'T', 0, 'a', 0, 'b', 0, 'l', 0, 'e', 0, 't', 0 };
static const uint8_t STR_EMPTY[] = { 2, 3 };
static const uint8_t STR_ID[] = { 36, 3, 'O', 0, 'E', 0, 'M', 0, '0', 0, '2', 0, '_', 0, 'T', 0, '1', 0, '8', 0,
                                  'e', 0, '_', 0, '2', 0, '4', 0, '1', 0, '0', 0, '3', 0, '0', 0 };
static const uint8_t STR_LANG[] = { 4, 3, 0x09, 0x04 };

static uint32_t setup[2];
static volatile int state = USB_STATE_OFF;
static volatile int report_mode;                            // 0 = not chosen, 8 = raw vendor, 10 = digitizer
static uint8_t pending_addr = 0xFF;
static uint32_t stat_resets, stat_setups;

static const uint8_t *ep0_ptr;
static int ep0_left;
static int ep0_zlp;
static int ep0_out_wait;                                    // 1 = ignore the SET_REPORT data, 2 = settings report
static uint8_t ep0_rx[SETTINGS_PAYLOAD + 1];               // settings report received (ID + payload)
static int ep0_rx_len, ep0_want;                           // bytes received / expected in the settings report
static uint8_t dbg_sets, dbg_len, dbg_wlen;                 // shown in the settings report: writes seen, bytes kept, last wLength
static uint8_t ep0_tx[SETTINGS_PAYLOAD + 1];

uint32_t usb_stat_resets(void) { return stat_resets; }
uint32_t usb_stat_setups(void) { return stat_setups; }
int usb_report_mode(void) { return report_mode; }

static void fifo_write(int ep, const uint8_t *data, int len)
{
    int words = (len + 3) / 4;
    for (int i = 0; i < words; i++) {
        uint32_t w = 0;
        for (int b = 0; b < 4 && i * 4 + b < len; b++)
            w |= ((uint32_t)data[i * 4 + b]) << (b * 8);
        FIFO(ep)[0] = w;
    }
}

static void ep0_arm_out(void)
{
    DOEPTSIZ(0) = (3u << 29) | (1u << 19) | 24u;
    DOEPCTL(0) |= EPENA | CNAK;
}

static void ep0_send_chunk(void)
{
    int n = ep0_left > 64 ? 64 : ep0_left;
    DIEPTSIZ(0) = (1u << 19) | (uint32_t)n;
    DIEPCTL(0) |= EPENA | CNAK;
    if (n)
        fifo_write(0, ep0_ptr, n);
    ep0_ptr += n;
    ep0_left -= n;
}

static void ep0_start_in(const uint8_t *p, int len, int wLength)
{
    if (len > wLength) len = wLength;
    ep0_ptr = p;
    ep0_left = len;
    ep0_zlp = (len > 0 && len < wLength && (len % 64) == 0);
    ep0_send_chunk();
}

static void ep0_status_in(void)
{
    ep0_ptr = 0; ep0_left = 0; ep0_zlp = 0;
    DIEPTSIZ(0) = (1u << 19);
    DIEPCTL(0) |= EPENA | CNAK;
}

static void flush_fifos(void)
{
    GRSTCTL = (0x10u << 6) | (1u << 5);
    while (GRSTCTL & (1u << 5)) { }
    GRSTCTL = (1u << 4);
    while (GRSTCTL & (1u << 4)) { }
}

int usb_init(void)
{
    for (int i = 0; i < 18; i++)
        DEV_DESC[i] = ORIG_DEV_DESC[i];
    DEV_DESC[7] = 64;                                   // EP0 packet size
    DEV_DESC[12] = 0x13;

    RCU_CFG0 &= ~((3u << 22) | (1u << 31));         // USB clock = PLL/1.5 = 48 MHz
    RCU_AHBEN |= (1u << 12);
    RCU_AHBRST |= (1u << 12);
    RCU_AHBRST &= ~(1u << 12);

    GUSBCFG |= (1u << 6);
    for (uint32_t t = 0; !(GRSTCTL & (1u << 31)); t++)
        if (t > 500000u)
            return 1;
    GRSTCTL |= 1u;
    for (uint32_t t = 0; GRSTCTL & 1u; t++)
        if (t > 500000u)
            return 2;

    // power/VBUS setup
    GCCFG |= 0xD0000u;
    REG(USB_BASE + 0x008) &= ~0x80u;
    GCCFG |= (1u << 21);
    GUSBCFG = (GUSBCFG & ~(1u << 29)) | (1u << 30); // force device mode
    delay_ms(50);
    if (GINTSTS & 1u)
        return 3;

    PWRCLKCTL = 0;
    GUSBCFG = (GUSBCFG & ~(0xFu << 10)) | (6u << 10);
    DCFG = (DCFG & ~3u) | 3u;
    GRXFSIZ = 128;
    GNPTXFSIZ = (32u << 16) | 128u;                 // FIFO sizes in words: RX 128, EP0 32, EP1-3 32 each
    DIEPTXF(1) = (32u << 16) | 160u;
    DIEPTXF(2) = (32u << 16) | 192u;
    DIEPTXF(3) = (32u << 16) | 224u;
    flush_fifos();

    DIEPMSK = 0; DOEPMSK = 0; DAINTMSK = 0;
    GINTSTS = 0xBFFFFFFFu;
    state = USB_STATE_POWERED;
    DCTL &= ~(1u << 1);
    return 0;
}

static void ep1_out_arm(void)
{
    DOEPTSIZ(1) = (1u << 19) | 8u;
    DOEPCTL(1) |= EPENA | CNAK;
}

static void configure_endpoints(void)
{
    for (int n = 1; n <= 3; n++) {
        DIEPCTL(n) = EP_SIZE[n] | (1u << 15) | (3u << 18) | ((uint32_t)n << 22) | (1u << 28);
        DIEPINT(n) = 0xFFu;
        DAINTMSK |= 1u << n;
    }
    DOEPCTL(1) = 8u | (1u << 15) | (3u << 18) | (1u << 28);     // EP1 OUT: 8 bytes, interrupt
    DOEPINT(1) = 0xFFu;
    DAINTMSK |= 1u << 17;
    DOEPMSK |= 1u;
    ep1_out_arm();
}

static void deconfigure_endpoints(void)
{
    for (int n = 1; n <= 3; n++) {
        if (DIEPCTL(n) & EPENA)
            DIEPCTL(n) |= (1u << 30) | SNAK;
        DIEPCTL(n) &= ~(1u << 15);
        DAINTMSK &= ~(1u << n);
    }
    if (DOEPCTL(1) & EPENA)
        DOEPCTL(1) |= (1u << 30) | SNAK;
    DOEPCTL(1) &= ~(1u << 15);
    DAINTMSK &= ~(1u << 17);
}

static const uint8_t *string_desc(int idx)
{
    if (idx == 0) return STR_LANG;
    if (idx == 1) return STR_MANUF;
    if (idx == 2) return STR_PRODUCT;
    if (idx == 3) return STR_EMPTY;
    if (idx == 200) { report_mode = 8;  return STR_EMPTY; }   // driver init: raw vendor reports
    if (idx == 201) return STR_ID;
    if (idx == 205) { report_mode = 10; return STR_EMPTY; }   // digitizer reports
    return 0;
}

static void stall_ep0(void)
{
    DIEPCTL(0) |= STALL;
    DOEPCTL(0) |= STALL;
}

static void handle_setup(void)
{
    uint8_t *s = (uint8_t *)setup;
    uint8_t bmReq = s[0], bReq = s[1];
    uint16_t wValue = s[2] | (s[3] << 8);
    uint16_t wIndex = s[4] | (s[5] << 8);
    uint16_t wLength = s[6] | (s[7] << 8);
    static const uint8_t zeros[8] = {0};
    static const uint8_t one = 1;
    static const uint8_t feature_rpt[8] = {0x16, 0, 0, 0, 0, 0, 0, 0};
    stat_setups++;

    uint8_t type = bmReq & 0x60;

    if (type == 0x00) {
        switch (bReq) {
        case 0x00: ep0_start_in(zeros, 2, wLength); return;
        case 0x01: case 0x03: ep0_status_in(); return;
        case 0x05:                                              // SET_ADDRESS
            pending_addr = (uint8_t)wValue;
            DCFG = (DCFG & ~(0x7Fu << 4)) | ((uint32_t)pending_addr << 4);
            ep0_status_in();
            return;
        case 0x06: {                                            // GET_DESCRIPTOR
            uint8_t dt = (uint8_t)(wValue >> 8), di = (uint8_t)wValue;
            if (dt == 1) { ep0_start_in(DEV_DESC, sizeof(DEV_DESC), wLength); return; }
            if (dt == 2) { ep0_start_in(ORIG_CFG_DESC, sizeof(ORIG_CFG_DESC), wLength); return; }
            if (dt == 3) {
                const uint8_t *p = string_desc(di);
                if (p) { ep0_start_in(p, p[0], wLength); return; }
            }
            if (dt == 0x21 && wIndex < 3) { ep0_start_in(&ORIG_CFG_DESC[HID_OFF[wIndex]], 9, wLength); return; }
            if (dt == 0x22 && wIndex < 3) { ep0_start_in(RD_TABLE[wIndex], RD_LEN[wIndex], wLength); return; }
            break;
        }
        case 0x08: ep0_start_in(&one, 1, wLength); return;
        case 0x09:                                              // SET_CONFIGURATION
            if (wValue) { configure_endpoints(); state = USB_STATE_CONFIGURED; }
            else        { deconfigure_endpoints(); state = USB_STATE_ADDRESSED; }
            ep0_status_in();
            return;
        case 0x0A: ep0_start_in(zeros, 1, wLength); return;
        case 0x0B: ep0_status_in(); return;
        }
    } else if (type == 0x20) {                                 // HID class requests
        switch (bReq) {
        case 0x01:                                              // GET_REPORT
            if (wValue == (0x0300u | SETTINGS_REPORT_ID) && wLength == sizeof(ep0_tx)) {
                ep0_tx[0] = SETTINGS_REPORT_ID;
                settings_fill_report(ep0_tx + 1);
                ep0_tx[1 + SETTINGS_DIAG] = dbg_sets;
                ep0_tx[2 + SETTINGS_DIAG] = dbg_len;
                ep0_tx[3 + SETTINGS_DIAG] = dbg_wlen;
                ep0_start_in(ep0_tx, sizeof(ep0_tx), wLength);
            }
            else if ((wValue >> 8) == 3 && (wValue & 0xFF) == 0x16) ep0_start_in(feature_rpt, 8, wLength);
            else ep0_start_in(zeros, 8, wLength);
            return;
        case 0x02: ep0_start_in(zeros, 1, wLength); return;
        case 0x03: ep0_start_in(&one, 1, wLength); return;
        case 0x09:                                              // SET_REPORT
            if (wValue == (0x0300u | SETTINGS_REPORT_ID) && wLength > 0 && wLength <= sizeof(ep0_rx)) {
                // EP0 takes one packet per arming: the packets after the first are armed again when the previous one is done
                ep0_out_wait = 2;
                ep0_rx_len = 0;
                ep0_want = wLength;
                dbg_sets++;
                dbg_wlen = (uint8_t)wLength;
                DOEPTSIZ(0) = (3u << 29) | (1u << 19) | (wLength > 64 ? 64u : wLength);
                DOEPCTL(0) |= EPENA | CNAK;
            } else if (wLength) {
                ep0_out_wait = 1;
                DOEPTSIZ(0) = (3u << 29) | (1u << 19) | (wLength > 64 ? 64u : wLength);
                DOEPCTL(0) |= EPENA | CNAK;
            } else {
                ep0_status_in();
            }
            return;
        case 0x0A: case 0x0B: ep0_status_in(); return;
        }
    }
    stall_ep0();
}

int usb_send(int ep, const void *data, int len)
{
    if (state != USB_STATE_CONFIGURED || ep < 1 || ep > 3 || len > EP_SIZE[ep])
        return -1;
    if (DIEPCTL(ep) & EPENA)
        return -2;
    DIEPTSIZ(ep) = (1u << 19) | (uint32_t)len;
    DIEPCTL(ep) |= EPENA | CNAK;
    fifo_write(ep, (const uint8_t *)data, len);
    return 0;
}

// 1 once per USB frame (1 ms)
int usb_sof(void)
{
    if (GINTSTS & (1u << 3)) {
        GINTSTS = 1u << 3;
        return 1;
    }
    return 0;
}

// Handles one entry of the receive FIFO: a SETUP packet, or OUT data (kept if it is part of the settings report)
static void rx_pop(void)
{
    uint32_t st = GRXSTSP;
    uint32_t ep = st & 0xF;
    uint32_t pktsts = (st >> 17) & 0xF;
    uint32_t bcnt = (st >> 4) & 0x7FF;
    if (pktsts == 0x6 && ep == 0) {
        setup[0] = FIFO(0)[0];
        setup[1] = FIFO(0)[1];
    } else if (pktsts == 0x2 && bcnt) {              // OUT data: keep the settings report, drop the rest
        int keep = ep == 0 && ep0_out_wait == 2;
        for (uint32_t i = 0; i < (bcnt + 3) / 4; i++) {
            uint32_t w = FIFO(0)[0];
            for (int b = 0; b < 4 && (int)(i * 4 + (uint32_t)b) < (int)bcnt; b++)
                if (keep && ep0_rx_len < (int)sizeof(ep0_rx)) ep0_rx[ep0_rx_len++] = (uint8_t)(w >> (8 * b));
        }
    }
}

int usb_poll(void)
{
    uint32_t ints = GINTSTS;

    if (ints & GINT_USBRST) {
        GINTSTS = GINT_USBRST;
        stat_resets++;
        DOEPCTL(0) |= SNAK;
        DCFG &= ~(0x7Fu << 4);
        pending_addr = 0xFF;
        ep0_out_wait = 0;
        deconfigure_endpoints();
        DAINTMSK = 1u | (1u << 16);
        DOEPMSK = (1u << 3) | 1u;
        DIEPMSK = 1u;
        flush_fifos();
        ep0_arm_out();
        state = USB_STATE_POWERED;
    }
    if (ints & GINT_ENUMDNE) {
        GINTSTS = GINT_ENUMDNE;
        DCTL |= (1u << 8);
        DIEPCTL(0) &= ~3u;
        ep0_arm_out();
    }
    if (GINTSTS & GINT_RXFLVL) rx_pop();
    if (DOEPINT(0) & (1u << 3)) {
        DOEPINT(0) = (1u << 3);
        handle_setup();
    }
    if (DOEPINT(0) & 1u) {
        DOEPINT(0) = 1u;
        for (int i = 0; ep0_out_wait == 2 && i < 8 && (GINTSTS & GINT_RXFLVL); i++) rx_pop();   // take all received packets first
        if (ep0_out_wait == 2 && ep0_rx_len < ep0_want) {           // more of the settings report to come
            DOEPTSIZ(0) = (3u << 29) | (1u << 19) | 64u;
            DOEPCTL(0) |= EPENA | CNAK;
        } else if (ep0_out_wait) {
            if (ep0_out_wait == 2 && ep0_rx_len > 1 && ep0_rx[0] == SETTINGS_REPORT_ID) {
                dbg_len = (uint8_t)ep0_rx_len;
                settings_submit(ep0_rx + 1, ep0_rx_len - 1);
            }
            ep0_out_wait = 0;
            ep0_status_in();
        }
        else ep0_arm_out();
    }
    if (state == USB_STATE_CONFIGURED && (DOEPINT(1) & 1u)) {   // EP1 OUT done: re-arm
        DOEPINT(1) = 1u;
        ep1_out_arm();
    }
    if (DIEPINT(0) & 1u) {
        DIEPINT(0) = 1u;
        if (pending_addr != 0xFF) {
            if (pending_addr) state = USB_STATE_ADDRESSED;
            pending_addr = 0xFF;
        }
        if (ep0_left > 0) ep0_send_chunk();
        else if (ep0_zlp) { ep0_zlp = 0; ep0_send_chunk(); }
        else ep0_arm_out();
    }
    for (int n = 1; n <= 3; n++)
        if (DIEPINT(n) & 1u)
            DIEPINT(n) = 1u;
    return state;
}
