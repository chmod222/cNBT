/*
 * buffer.h - INTERNAL USE ONLY. Defines a simple automatically resizing buffer.
 *
 * -----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Lukas Niederbremer <webmaster@flippeh.de> and Clark Gaebel <cg.wowus.cg@gmail.com>
 * wrote this file. As long as you retain this notice you can do whatever you
 * want with this stuff. If we meet some day, and you think this stuff is worth
 * it, you can buy us a beer in return.
 * -----------------------------------------------------------------------------
 */
#ifndef NBT_BUFFER_H
#define NBT_BUFFER_H

#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#define INITIAL_SIZE 1024

struct buffer {
    char* data; /* You can access the buffer's raw bytes through this pointer */
    size_t len; /* Only accesses in the interval [data, data + len) are defined */
    size_t cap; /* Internal use. The allocated size of the buffer. */
};

/*
 * Initializes a buffer. Returns non-zero if an out-of memory failure occured.
 * If such a failure occurs, further usage of the buffer results in undefined
 * behavior.
 */
static inline int buffer_init(struct buffer* b)
{
    *b = (struct buffer) {
        .data = malloc(INITIAL_SIZE),
        .len  = 0,
        .cap  = INITIAL_SIZE
    };

    return b->data == 0;
}

/* Ensures the buffer has enough room for `newsize' elements. */
static inline int __buffer_grow(struct buffer* b, size_t newsize)
{
    while(b->cap < newsize)
        b->cap *= 2;

    char* temp = realloc(b->data, b->cap);

    if(temp == NULL)
    {
        b->data = (free(b->data), NULL);
        b->len = 0;
        b->cap = 0;

        return 1;
    }

    b->data = temp;

    return 0;
}

/*
 * Copies `n' bytes from `data' into the buffer. Returns non-zero if an
 * out-of-memory failure occured. If such a failure occurs, further usage of the
 * buffer results in undefined behavior.
 */
static inline int buffer_append(struct buffer* b, char* data, size_t n)
{
    if(b->len + n > b->cap)
        if(__buffer_grow(b, b->len + n))
            return 1;

    memcpy(b->data + b->len, data, n);
    b->len += n;

    return 0;
}

/* Frees all memory associated with the buffer */
static inline void buffer_free(struct buffer* b)
{
    free(b->data);

    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

#undef INITIAL_SIZE

#endif
