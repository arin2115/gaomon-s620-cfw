#ifndef USB_H
#define USB_H
#include <stdint.h>

enum { USB_STATE_OFF = 0, USB_STATE_POWERED = 1, USB_STATE_ADDRESSED = 2, USB_STATE_CONFIGURED = 3 };

void delay_ms(uint32_t ms);
// 0 = ok, 1 = core never idle, 2 = core reset timeout, 3 = stuck in host mode
int usb_init(void);
int usb_poll(void);
int usb_sof(void);        // 1 once per USB frame (1 ms)
// 0 = host has not chosen, 8 = raw vendor reports, 10 = digitizer reports
int usb_report_mode(void);
// Sends a report on endpoint 1 (raw pen), 2 (digitizer) or 3 (keyboard). 0 = queued, <0 = dropped.
int usb_send(int ep, const void *data, int len);
uint32_t usb_stat_resets(void);
uint32_t usb_stat_setups(void);

#endif
