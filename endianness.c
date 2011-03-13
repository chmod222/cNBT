#include <stdint.h>

#include "endianness.h"

static char* __swap_bytes(char* b, char* e)
{
    char t;

    if(b >= e)
        return NULL;

    t  = *b;
    *b = *e;
    *e =  t;

    __swap_bytes(b + 1, e - 1);

    return b;
}

void* swap_bytes(void* s, size_t len)
{
    char* cs = s;
    return __swap_bytes(cs, cs + len - 1);
}
