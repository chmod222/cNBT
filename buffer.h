/*
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

/*
 * A buffer is 'unlimited' storage for raw data. As long as buffer_append is
 * used to add data, it will automatically resize to make room. To read the
 * data, just access `data' directly.
 */
struct buffer {
    char* data; /* You can access the buffer's raw bytes through this pointer */
    size_t len; /* Only accesses in the interval [data, data + len) are defined */
    size_t cap; /* Internal use. The allocated size of the buffer. */
};

/*
 * Initialize a buffer with this macro.
 *
 * Usage:
 *   struct buffer b = BUFFER_INIT;
 * OR
 *   struct buffer b;
 *   b = BUFFER_INIT;
 */
#define BUFFER_INIT (struct buffer) { NULL, 0, 0 }

/*
 * Ensures there's enough room in the buffer for at least `reserved_amount'
 * bytes. Returns non-zero on failure. If such a failure occurs, further
 * usage of the buffer results in undefined behavior.
 */
static inline int buffer_reserve(struct buffer* b, size_t reserved_amount)
{
	if(b->cap >= reserved_amount) return 0;

    while(b->cap < reserved_amount)
        b->cap *= 2;

    char* temp = realloc(b->data, b->cap);

    if(temp == NULL)
		return buffer_free(b), 1;

    b->data = temp;

    return 0;
}

/*
 * Copies `n' bytes from `data' into the buffer. Returns non-zero if an
 * out-of-memory failure occured. If such a failure occurs, further usage of the
 * buffer results in undefined behavior.
 */
static inline int buffer_append(struct buffer* b, const void* data, size_t n)
{
	/* lazy initialization */
	if(b->data == NULL)
	{
		b->cap = INITIAL_SIZE;
		b->data = malloc(b->cap);
		if(b->data == NULL)          return 1;
	}

    if(buffer_reserve(b, b->len + n)) return 1;

	/* copy data in */
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
