#ifndef KEYS_H
#define KEYS_H
#include <stdint.h>

// ch0 = keys 1, 2 and ch1 = keys 3, 4. mask 1, 2, 4 or 8
uint8_t keys_read(void);
#endif
