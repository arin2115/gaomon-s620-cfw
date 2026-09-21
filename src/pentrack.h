#ifndef PENTRACK_H
#define PENTRACK_H
#include <stdint.h>

typedef struct {
    int in_range;
    int tip;
    uint32_t x, y;            // 0..33020, 0..20320
    uint16_t pressure;        // 0..8191
} pen_sample_t;

// One unit of scan work. Returns 1 when `s` has a new sample (also once when the pen leaves range).
int pentrack_step(pen_sample_t *s);
// called during the long resonance check so USB reports keep going
extern void (*pentrack_idle_hook)(void);

// call after the settings changed (main loop only)
void pentrack_settings_changed(void);

#endif
