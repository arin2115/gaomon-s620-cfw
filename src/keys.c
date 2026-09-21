#include <stdint.h>
#include "keys.h"
#include "emr.h"
#include "settings.h"

static uint8_t ladder(int first, uint8_t lo_bit, uint8_t hi_bit, int v)
{
    int lo_max = CFG(KEY_LO_MAX), hi_min = CFG(KEY_HI_MIN), hi_max = CFG(KEY_HI_MAX);
    if (v <= lo_max) { delay_us(200); v = emr_adc_read(first); return v <= lo_max ? lo_bit : 0; }
    if (v >= hi_min && v <= hi_max) { delay_us(200); v = emr_adc_read(first); return (v >= hi_min && v <= hi_max) ? hi_bit : 0; }
    return 0;
}

uint8_t keys_read(void)
{
    int a = emr_adc_read(0), b = emr_adc_read(1), idle = CFG(KEY_IDLE);
    if (a < idle) return ladder(0, 1, 2, a);
    if (b < idle) return ladder(1, 4, 8, b);
    return 0;
}
