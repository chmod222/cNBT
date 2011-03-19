/*
 * -----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Lukas Niederbremer <webmaster@flippeh.de> and Clark Gaebel <cg.wowus.cg@gmail.com>
 * wrote this file. As long as you retain this notice you can do whatever you
 * want with this stuff. If we meet some day, and you think this stuff is worth
 * it, you can buy us a beer in return.
 * -----------------------------------------------------------------------------
 */
#include "nbt.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* works around a bug in clang */
char* strdup(const char*);

#define CHECKED_MALLOC(var, n, on_error) do { \
    if((var = malloc(n)) == NULL)             \
    {                                         \
        errno = NBT_EMEM;                     \
        on_error;                             \
    }                                         \
} while(0)

void nbt_free_list(struct tag_list* list)
{
    assert(list);

    struct list_head* current;
    struct list_head* temp;
    list_for_each_safe(current, temp, &list->entry)
    {
        struct tag_list* entry = list_entry(current, struct tag_list, entry);

        nbt_free(entry->data);
        free(entry);
    }

    free(list);
}

void nbt_free(nbt_node* tree)
{
    if(tree == NULL) return;

    free(tree->name);

    if(tree->type == TAG_LIST || tree->type == TAG_COMPOUND)
        nbt_free_list(tree->payload.tag_list);

    else if(tree->type == TAG_BYTE_ARRAY)
        free(tree->payload.tag_byte_array.data);

    else if(tree->type == TAG_STRING)
        free(tree->payload.tag_string);

    free(tree);
}

static struct tag_list* clone_list(struct tag_list* list)
{
    /* even empty lists are valid pointers! */
    assert(list);

    struct tag_list* ret;
    CHECKED_MALLOC(ret, sizeof *ret, goto clone_error);

    INIT_LIST_HEAD(&ret->entry);
    ret->data = NULL;

    struct list_head* pos;
    list_for_each(pos, &list->entry)
    {
        struct tag_list* current = list_entry(pos, struct tag_list, entry);
        struct tag_list* new;

        CHECKED_MALLOC(new, sizeof *new, goto clone_error);

        new->data = nbt_clone(current->data);

        if(new->data == NULL)
        {
            free(new);
            goto clone_error;
        }

        list_add_tail(&new->entry, &ret->entry);
    }

    return ret;

clone_error:
    nbt_free_list(ret);
    return NULL;
}

/* same as strdup, but handles NULL gracefully */
static inline char* safe_strdup(const char* s)
{
    return s ? strdup(s) : NULL;
}

nbt_node* nbt_clone(nbt_node* tree)
{
    if(tree == NULL) return NULL;
    assert(tree->type != TAG_INVALID);

    nbt_node* ret;
    CHECKED_MALLOC(ret, sizeof *ret, return NULL);

    ret->type = tree->type;
    ret->name = safe_strdup(tree->name);

    if(tree->name && ret->name == NULL) goto clone_error;

    if(tree->type == TAG_STRING)
    {
        ret->payload.tag_string = strdup(tree->payload.tag_string);
        if(ret->payload.tag_string == NULL) goto clone_error;
    }

    else if(tree->type == TAG_BYTE_ARRAY)
    {
        unsigned char* newbuf;
        CHECKED_MALLOC(newbuf, tree->payload.tag_byte_array.length, goto clone_error);

        memcpy(newbuf,
               tree->payload.tag_byte_array.data,
               tree->payload.tag_byte_array.length);

        ret->payload.tag_byte_array.data   = newbuf;
        ret->payload.tag_byte_array.length = tree->payload.tag_byte_array.length;
    }

    else if(tree->type == TAG_LIST || tree->type == TAG_COMPOUND)
    {
        ret->payload.tag_list = clone_list(tree->payload.tag_list);
        if(ret->payload.tag_list == NULL) goto clone_error;
    }
    else
    {
        ret->payload = tree->payload;
    }

    return ret;

clone_error:
    if(ret) free(ret->name);

    free(ret);
    return NULL;
}

bool nbt_map(nbt_node* tree, nbt_visitor_t v, void* aux)
{
    assert(v);

    if(tree == NULL)  return true;
    if(!v(tree, aux)) return false;

    /* And if the item is a list or compound, recurse through each of their elements. */
    if(tree->type == TAG_LIST || tree->type == TAG_COMPOUND)
    {
        struct list_head* pos;

        list_for_each(pos, &tree->payload.tag_list->entry)
            if(!nbt_map(list_entry(pos, struct tag_list, entry)->data, v, aux))
                return false;
    }

    return true;
}

/* Only returns NULL on error. An empty list is still a valid pointer */
static struct tag_list* filter_list(const struct tag_list* list, nbt_predicate_t predicate, void* aux)
{
    assert(list);

    struct tag_list* ret;
    CHECKED_MALLOC(ret, sizeof *ret, goto filter_error);

    ret->data = NULL;
    INIT_LIST_HEAD(&ret->entry);

    const struct list_head* pos;
    list_for_each(pos, &list->entry)
    {
        const struct tag_list* p = list_entry(pos, struct tag_list, entry);

        nbt_node* new_node = nbt_filter(p->data, predicate, aux);

        if(errno != NBT_OK)  goto filter_error;
        if(new_node == NULL) continue;

        struct tag_list* new_entry;
        CHECKED_MALLOC(new_entry, sizeof *new_entry, goto filter_error);

        new_entry->data = new_node;
        list_add_tail(&new_entry->entry, &ret->entry);
    }

    return ret;

filter_error:
    if(errno == NBT_OK)
        errno = NBT_EMEM;

    nbt_free_list(ret);
    return NULL;
}

nbt_node* nbt_filter(const nbt_node* tree, nbt_predicate_t filter, void* aux)
{
    assert(filter);

    errno = NBT_OK;

    if(tree == NULL)       return NULL;
    if(!filter(tree, aux)) return NULL;

    nbt_node* ret;
    CHECKED_MALLOC(ret, sizeof *ret, goto filter_error);

    ret->type = tree->type;
    ret->name = safe_strdup(tree->name);

    if(tree->name && ret->name == NULL) goto filter_error;

    if(tree->type == TAG_STRING)
    {
        ret->payload.tag_string = strdup(tree->payload.tag_string);
        if(ret->payload.tag_string == NULL) goto filter_error;
    }

    else if(tree->type == TAG_BYTE_ARRAY)
    {
        CHECKED_MALLOC(ret->payload.tag_byte_array.data,
                       tree->payload.tag_byte_array.length,
                       goto filter_error);

        memcpy(ret->payload.tag_byte_array.data,
               tree->payload.tag_byte_array.data,
               tree->payload.tag_byte_array.length);

        ret->payload.tag_byte_array.length = tree->payload.tag_byte_array.length;
    }

    /* Okay, we want to keep this node, but keep traversing the tree! */
    else if(tree->type == TAG_LIST || tree->type == TAG_COMPOUND)
    {
        ret->payload.tag_list = filter_list(tree->payload.tag_list, filter, aux);
        if(ret->payload.tag_list == NULL) goto filter_error;
    }
    else
    {
        ret->payload = tree->payload;
    }

    return ret;

filter_error:
    if(errno == NBT_OK)
        errno = NBT_EMEM;

    if(ret) free(ret->name);

    free(ret);
    return NULL;
}

nbt_node* nbt_filter_inplace(nbt_node* tree, nbt_predicate_t filter, void* aux)
{
    assert(filter);

    if(tree == NULL)       return                 NULL;
    if(!filter(tree, aux)) return nbt_free(tree), NULL;

    if(tree->type == TAG_LIST || tree->type == TAG_COMPOUND)
    {
        struct list_head* pos;
        struct list_head* n;

        list_for_each_safe(pos, n, &tree->payload.tag_list->entry)
        {
            struct tag_list* cur = list_entry(pos, struct tag_list, entry);

            cur->data = nbt_filter_inplace(cur->data, filter, aux);

            if(cur->data == NULL)
            {
                list_del(pos);
                free(cur);
            }
        }
    }

    return tree;
}

nbt_node* nbt_find(nbt_node* tree, nbt_predicate_t predicate, void* aux)
{
    if(tree == NULL)                  return NULL;
    if(predicate(tree, aux))          return tree;
    if(   tree->type != TAG_LIST
       && tree->type != TAG_COMPOUND) return NULL;

    struct list_head* pos;
    list_for_each(pos, &tree->payload.tag_list->entry)
    {
        struct tag_list* p = list_entry(pos, struct tag_list, entry);
        struct nbt_node* found;

        if((found = nbt_find(p->data, predicate, aux)))
            return found;
    }

    return NULL;
}

static bool names_are_equal(const nbt_node* node, void* vname)
{
    const char* name = vname;

    assert(node);
    assert(node->name != name);

    /*
     * Just another way of saying that they either have to both be NULL or valid
     * pointers. The !! forces a value to either 1 or 0. Write out a truth table.
     */
    if(!(!!name ^ !!node->name))
        return false;

    return strcmp(node->name, name) == 0;
}

nbt_node* nbt_find_by_name(nbt_node* tree, const char* name)
{
    return nbt_find(tree, &names_are_equal, (void*)name);
}

/* Gets the length of the list, plus the length of all its children. */
static inline size_t nbt_full_list_length(struct tag_list* list)
{
    size_t accum = 0;

    struct list_head* pos;
    list_for_each(pos, &list->entry)
        accum += nbt_size(list_entry(pos, const struct tag_list, entry)->data);

    return accum;
}

size_t nbt_size(const nbt_node* tree)
{
    if(tree == NULL)
        return 0;

    if(tree->type == TAG_LIST || tree->type == TAG_COMPOUND)
        return nbt_full_list_length(tree->payload.tag_list) + 1;

    return 1;
}
