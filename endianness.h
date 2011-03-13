#ifndef ENDIANNESS_H
#define ENDIANNESS_H

#include <stdint.h>

#define L_ENDIAN 0
#define B_ENDIAN    1

static inline int get_endianness()
{
    union {
        uint16_t i;
        char c[2];
    } t = { 0x0001 };

    return t.c[0] == 0;
}

uint64_t swpd(double d);
double uswpd(uint64_t d);

float swapf(float);
double swapd(double);
void swaps(uint16_t *x);
void swapi(uint32_t *x);
void swapl(uint64_t *x);

#endif
