#ifndef LIST_H
#define LIST_H

#include <stddef.h>

/*
 * Minimal List API:
 *
 * struct list_head;
 *
 * INIT_LIST_HEAD(head)
 *
 * list_add_tail(new, head)
 * list_del(pos)
 * list_empty(head)
 * list_length(head)
 * list_for_each(pos, head)
 * list_for_each_safe(pos, n, head)
 * list_entry(ptr, type, member)
 */

struct list_head {
    struct list_head *blink, /* back  link */
                     *flink; /* front link */
};

/* The first element is a sentinel. Don't access it. */
#define INIT_LIST_HEAD(head) (head)->flink = (head)->blink = (head)

static inline void list_add_tail(struct list_head* new, struct list_head* head)
{
    /* new goes between head->blink and head */
    new->flink = head;
    new->blink = head->blink;

    head->blink->flink = new;
    head->blink = new;
}

static inline void list_del(struct list_head* loc)
{
    loc->blink->flink = loc->flink;
    loc->flink->blink = loc->blink;
    loc->blink = loc->flink = NULL;
}

#define list_empty(head) ((head)->flink == (head))

#define list_entry(ptr, type, member) \
    ((type*)((char*)(ptr) - offsetof(type, member)))

#define list_for_each(pos, head) \
    for((pos) = (head)->flink;   \
        (pos) != (head);         \
        (pos) = (pos)->flink)

#define list_for_each_safe(pos, n, head)           \
    for((pos) = (head)->flink, (n) = (pos)->flink; \
        (pos) != (head);                           \
        (pos) = (n), (n) = (pos)->flink)

static inline size_t list_length(const struct list_head* head)
{
    const struct list_head* cursor;
    size_t accum = 0;

    list_for_each(cursor, head)
        accum++;

    return accum;
}

#endif
