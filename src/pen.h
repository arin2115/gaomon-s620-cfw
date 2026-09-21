#ifndef PEN_H
#define PEN_H
#include <stdint.h>

// Max x 33020, y 20320, pressure 8191. Returns <0 if the report was dropped.
int pen_send(uint32_t x, uint32_t y, uint16_t pressure, int in_range, int tip, int8_t tilt_x, int8_t tilt_y);

#endif
