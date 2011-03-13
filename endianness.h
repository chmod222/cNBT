#ifndef ENDIANNESS_H
#define ENDIANNESS_H

#include <stddef.h>
#include <stdint.h>

#define L_ENDIAN    0
#define B_ENDIAN    1

static inline int get_endianness()
{
    union {
        uint16_t i;
        char c[2];
    } t = { 0x0001 };

    return t.c[0] == 0;
}

void* swap_bytes(void* s, size_t len);

#endif
