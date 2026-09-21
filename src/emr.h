#ifndef EMR_H
#define EMR_H
#include <stdint.h>
#include "coil_tables.h"

#define EMR_FREQS 12

// timing in microseconds
extern uint16_t emr_settle_a;    // after the burst, before the receiver switch closes
extern uint16_t emr_settle_b;    // receiver switch closed -> first sample
extern uint8_t emr_burst_cycles;
extern uint16_t emr_period_us;
extern uint8_t emr_mean_n;       // 0 = median of 3 ADC samples, N = mean of N

// 0 = ok, 1 = cycle counter not running
int emr_init(void);
void emr_configure(void);            // apply the timing settings and re-time the grid
void delay_us(uint32_t us);
void emr_set_gain(int attenuate);

// Drive `tx` at burst frequency `freq` (0..11), listen on `rx`, return the ADC value (0..4095).
uint16_t emr_adc_read(int ch);
void emr_grid_reset(void);
uint16_t emr_measure(const coil_t *tx, const coil_t *rx, int freq);
// same coil transmits and receives
static inline uint16_t emr_self(const coil_t *c, int freq) { return emr_measure(c, c, freq); }

#endif
