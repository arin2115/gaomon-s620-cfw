#ifndef COIL_TABLES_H
#define COIL_TABLES_H
#include <stdint.h>

typedef struct { uint16_t pb_and; uint16_t pa_or; } coil_t;

#define X_COILS_N 30
static const coil_t X_COILS[30] = {
    { 0xFFDF, 0x0000 },   /* coil 1 */
    { 0xFFDF, 0x0040 },   /* coil 2 */
    { 0xFFEF, 0x0080 },   /* coil 3 */
    { 0xFFDF, 0x0020 },   /* coil 4 */
    { 0xFFEF, 0x00E0 },   /* coil 5 */
    { 0xFFDF, 0x0060 },   /* coil 6 */
    { 0xFFEF, 0x0040 },   /* coil 7 */
    { 0xFFEF, 0x00C0 },   /* coil 8 */
    { 0xFFEF, 0x0000 },   /* coil 9 */
    { 0xFFEF, 0x00A0 },   /* coil 10 */
    { 0xFFDF, 0x0080 },   /* coil 11 */
    { 0xFFEF, 0x0020 },   /* coil 12 */
    { 0xFFDF, 0x00C0 },   /* coil 13 */
    { 0xFFEF, 0x0060 },   /* coil 14 */
    { 0xFFDF, 0x00E0 },   /* coil 15 */
    { 0xFFDF, 0x00A0 },   /* coil 16 */
    { 0xFFDF, 0x0040 },   /* coil 17 */
    { 0xFFDF, 0x0020 },   /* coil 18 */
    { 0xFFDF, 0x0000 },   /* coil 19 */
    { 0xFFDF, 0x0060 },   /* coil 20 */
    { 0xFFEF, 0x0080 },   /* coil 21 */
    { 0xFFEF, 0x00C0 },   /* coil 22 */
    { 0xFFEF, 0x00E0 },   /* coil 23 */
    { 0xFFEF, 0x00A0 },   /* coil 24 */
    { 0xFFEF, 0x0040 },   /* coil 25 */
    { 0xFFEF, 0x0020 },   /* coil 26 */
    { 0xFFEF, 0x0000 },   /* coil 27 */
    { 0xFFEF, 0x0060 },   /* coil 28 */
    { 0xFFDF, 0x0080 },   /* coil 29 */
    { 0xFFDF, 0x00A0 },   /* coil 30 */
};

#define Y_COILS_N 19
static const coil_t Y_COILS[19] = {
    { 0xFFFD, 0x00A0 },   /* coil 1 */
    { 0xFFFD, 0x00E0 },   /* coil 2 */
    { 0xFFFD, 0x00C0 },   /* coil 3 */
    { 0xFFFD, 0x0080 },   /* coil 4 */
    { 0xFFFD, 0x0060 },   /* coil 5 */
    { 0xFFFD, 0x0000 },   /* coil 6 */
    { 0xFFFD, 0x0020 },   /* coil 7 */
    { 0xFFFD, 0x0040 },   /* coil 8 */
    { 0xFFF7, 0x00A0 },   /* coil 9 */
    { 0xFFF7, 0x00E0 },   /* coil 10 */
    { 0xFFF7, 0x00C0 },   /* coil 11 */
    { 0xFFF7, 0x0080 },   /* coil 12 */
    { 0xFFF7, 0x0060 },   /* coil 13 */
    { 0xFFF7, 0x0000 },   /* coil 14 */
    { 0xFFF7, 0x0020 },   /* coil 15 */
    { 0xFFF7, 0x0040 },   /* coil 16 */
    { 0xFFFD, 0x00E0 },   /* coil 17 */
    { 0xFFFD, 0x0080 },   /* coil 18 */
    { 0xFFFD, 0x00A0 },   /* coil 19 */
};

#endif
