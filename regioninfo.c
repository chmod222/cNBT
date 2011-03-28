#include "nbt.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

static inline void* reverse_endianness(void* s, size_t len)
{
    for(char* b = s,
            * e = b + len - 1;
        b < e;
        b++, e--)
    {
        char t = *b;

        *b = *e;
        *e = t;
    }

    return s;
}

static inline bool little_endian()
{
    union {
        int  i;
        char c;
    } u;

    u.i = 0;
    u.c = 1;

    return u.i == 1;
}

#define be2ne(x) do {                        \
    if(little_endian())                      \
        reverse_endianness(&(x), sizeof(x)); \
} while(0)

int main(int argc, char** argv)
{
    if(argc < 2)
    {
        fprintf(stderr, "Usage: %s [minecraft region file]\n", argv[0]);
        return 1;
    }

    FILE* f = fopen(argv[1], "rb");

    //

    fclose(f);
    return 0;
}
