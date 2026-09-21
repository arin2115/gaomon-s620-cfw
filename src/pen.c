// Raw vendor report (endpoint 1, 12 bytes): 8, flags (0x80 in range, 1 tip), X, Y, pressure (16 bit each), X and Y high bytes, tilt X, tilt Y
// Digitizer report (endpoint 2, 10 bytes): 0x0A, flags (0x40 in range, 1 tip), X, Y, pressure, tilt X, tilt Y
#include <stdint.h>
#include "pen.h"
#include "usb.h"

int pen_send(uint32_t x, uint32_t y, uint16_t pressure, int in_range, int tip, int8_t tilt_x, int8_t tilt_y)
{
    if (x > 33020u) x = 33020u;
    if (y > 20320u) y = 20320u;
    if (pressure > 8191u) pressure = 8191u;

    uint8_t v[12] = {
        0x08, (uint8_t)((in_range ? 0x80 : 0) | (tip ? 1 : 0)),
        (uint8_t)x, (uint8_t)(x >> 8), (uint8_t)y, (uint8_t)(y >> 8),
        (uint8_t)pressure, (uint8_t)(pressure >> 8),
        (uint8_t)(x >> 16), (uint8_t)(y >> 16), (uint8_t)tilt_x, (uint8_t)tilt_y,
    };
    uint8_t d[10] = {
        0x0A, (uint8_t)((in_range ? 0x40 : 0) | (tip ? 1 : 0)),
        (uint8_t)x, (uint8_t)(x >> 8), (uint8_t)y, (uint8_t)(y >> 8),
        (uint8_t)pressure, (uint8_t)(pressure >> 8), (uint8_t)tilt_x, (uint8_t)tilt_y,
    };
    if (usb_report_mode() == 8)
        return usb_send(1, v, sizeof v);
    return usb_send(2, d, sizeof d);
}
