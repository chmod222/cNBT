/*
 * -----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Lukas Niederbremer <webmaster@flippeh.de>, and Clark Gaebel <cg.wowus.cg@gmail.com>
 * wrote this file. As long as you retain this notice you can do whatever you want
 * with this stuff. If we meet some day, and you think this stuff is worth it, you
 * can buy us a beer in return.
 * -----------------------------------------------------------------------------
 */

#include "nbt.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/* are we running on a little-endian system? */
static int little_endian()
{
    union {
        uint16_t i;
        char c[2];
    } t = { 0x0001 };

    return *t.c == 1;
}

static void* swap_bytes(void* s, size_t len)
{
    char* b;
    char* e;

    for(b = s, e = b + len - 1;
        b < e;
        b++, e--)
    {
        char t = *b;

        *b = *e;
        *e = t;
    }

    return s;
}

/* big endian to native endian. works in-place */
static void* be2ne(void* s, size_t len)
{
    if(little_endian())
        return swap_bytes(s, len);
    else
        return s;
}

/* native endian to big endian. works the exact same as its reverse */
#define ne2be be2ne

/* A special form of memcpy which copies `n' bytes into `dest', then returns
 * `src' + n.
 */
static const void* memscan(void* dest, const void* src, size_t n)
{
    memcpy(dest, src, n);
    return (const char*)src + n;
}

/* Does a memscan, then goes from big endian to native endian on the
 * destination.
 */
static const void* swapped_memscan(void* dest, const void* src, size_t n)
{
    const void* ret = memscan(dest, src, n);
    return be2ne(dest, n), ret;
}

#define CHECKED_MALLOC(var, n, on_error) do { \
    var = malloc(n);                          \
    if(var == NULL) { on_error; }             \
} while(0)

/* Parses a tag, given a name (may be NULL) and a type. Fills in the payload. */
static nbt_node* parse_unnamed_tag(nbt_type type, char* name, const char** memory, size_t* length);

static void free_list(struct tag_list* list)
{
    if(list == NULL) return;

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
    if(tree->type == TAG_LIST || tree->type == TAG_COMPOUND)
        free_list(tree->payload.tag_list);

    if(tree->type == TAG_BYTE_ARRAY)
        free(tree->payload.tag_byte_array.data);

    free(tree->name);
    free(tree);
}

/*
 * Reads some bytes from the memory stream. This macro will read `n'
 * bytes into `dest', call either memscan or swapped_memscan depending on
 * `scanner', then fix the length. If anything funky goes down, `on_failure'
 * will be executed.
 */
#define READ_GENERIC(dest, n, scanner, on_failure) do { \
    if(*length < (n)) { on_failure; }                   \
    *memory = scanner((dest), *memory, (n));            \
    *length -= (n);                                     \
} while(0)

/*
 * Reads a string from memory, moving the pointer and updating the length
 * appropriately. Returns NULL on failure.
 */
static char* read_string(const char** memory, size_t* length)
{
    int16_t string_length;
    char* ret = NULL;

    READ_GENERIC(&string_length, sizeof string_length, swapped_memscan, goto parse_error);

    if(string_length < 0)               goto parse_error;
    if(*length < (size_t)string_length) goto parse_error;

    CHECKED_MALLOC(ret, string_length + 1,
        errno = NBT_EMEM;
        goto parse_error;
    );

    READ_GENERIC(ret, (size_t)string_length, memscan, goto parse_error);

    ret[string_length] = '\0'; /* don't forget to NULL-terminate ;) */
    return ret;

parse_error:
    if(errno == NBT_OK)
        errno = NBT_ERR;

    free(ret);
    return NULL;
}

static struct nbt_byte_array read_byte_array(const char** memory, size_t* length)
{
    struct nbt_byte_array ret;
    ret.data = NULL;

    READ_GENERIC(&ret.length, sizeof ret.length, swapped_memscan, goto parse_error);

    if(ret.length < 0) goto parse_error;

    CHECKED_MALLOC(ret.data, ret.length,
        errno = NBT_EMEM;
        goto parse_error;
    );

    READ_GENERIC(ret.data, (size_t)ret.length, memscan, goto parse_error);

    return ret;

parse_error:
    if(errno == NBT_OK)
        errno = NBT_ERR;

    free(ret.data);
    ret.data = NULL;
    return ret;
}

static struct tag_list* read_list(const char** memory, size_t* length)
{
    uint8_t type;
    int32_t elems;
    struct tag_list* ret = NULL;
    int32_t i;

    READ_GENERIC(&type, sizeof type, swapped_memscan, goto parse_error);
    READ_GENERIC(&elems, sizeof elems, swapped_memscan, goto parse_error);

    CHECKED_MALLOC(ret, sizeof *ret,
        errno = NBT_EMEM;
        goto parse_error;
    );

    ret->data = NULL; /* the first value in a list is a sentinel. don't even try to read it. */
    INIT_LIST_HEAD(&ret->entry);

    for(i = 0; i < elems; i++)
    {
        struct tag_list* new;

        CHECKED_MALLOC(new, sizeof *new,
            errno = NBT_EMEM;
            goto parse_error;
        );

        new->data = parse_unnamed_tag((nbt_type)type, NULL, memory, length);

        if(new->data == NULL)
        {
            free(new);
            goto parse_error;
        }

        list_add_tail(&new->entry, &ret->entry);
    }

    return ret;

parse_error:
    if(errno == NBT_OK)
        errno = NBT_ERR;

    free_list(ret);
    return NULL;
}

static struct tag_list* read_compound(const char** memory, size_t* length)
{
    struct tag_list* ret;

    CHECKED_MALLOC(ret, sizeof *ret,
        errno = NBT_EMEM;
        goto parse_error;
    );

    ret->data = NULL;
    INIT_LIST_HEAD(&ret->entry);

    for(;;)
    {
        uint8_t type;
        char* name = NULL;
        struct tag_list* new_entry;

        READ_GENERIC(&type, sizeof type, swapped_memscan, goto parse_error);

        if(type == 0) break; /* TAG_END == 0. We've hit the end of the list when type == TAG_END. */

        name = read_string(memory, length);
        if(name == NULL) goto parse_error;

        CHECKED_MALLOC(new_entry, sizeof *new_entry,
            errno = NBT_EMEM;
            free(name);
            goto parse_error;
        );

        new_entry->data = parse_unnamed_tag((nbt_type)type, name, memory, length);

        if(new_entry->data == NULL)
        {
            free(new_entry);
            free(name);
            goto parse_error;
        }

        list_add_tail(&new_entry->entry, &ret->entry);
    }

    return ret;

parse_error:
    if(errno == NBT_OK)
        errno = NBT_ERR;
    free_list(ret);

    return NULL;
}

/*
 * Parses a tag, given a name (may be NULL) and a type. Fills in the payload.
 */
static nbt_node* parse_unnamed_tag(nbt_type type, char* name, const char** memory, size_t* length)
{
    nbt_node* node;

    CHECKED_MALLOC(node, sizeof *node,
        errno = NBT_EMEM;
        goto parse_error;
    );

    node->type = type;
    node->name = name;

#define COPY_INTO_PAYLOAD(payload_name) \
    READ_GENERIC(&node->payload.payload_name, sizeof node->payload.payload_name, swapped_memscan, goto parse_error);

    switch(type)
    {
    case TAG_BYTE:
        COPY_INTO_PAYLOAD(tag_byte);
        break;
    case TAG_SHORT:
        COPY_INTO_PAYLOAD(tag_short);
        break;
    case TAG_INT:
        COPY_INTO_PAYLOAD(tag_int);
        break;
    case TAG_LONG:
        COPY_INTO_PAYLOAD(tag_long);
        break;
    case TAG_FLOAT:
        COPY_INTO_PAYLOAD(tag_float);
        break;
    case TAG_DOUBLE:
        COPY_INTO_PAYLOAD(tag_double);
        break;
    case TAG_BYTE_ARRAY:
        node->payload.tag_byte_array = read_byte_array(memory, length);
        break;
    case TAG_STRING:
        node->payload.tag_string = read_string(memory, length);
        break;
    case TAG_LIST:
        node->payload.tag_list = read_list(memory, length);
        break;
    case TAG_COMPOUND:
        node->payload.tag_compound = read_compound(memory, length);
        break;

    default:
        goto parse_error; /* Unknown node or TAG_END. Either way, we shouldn't be parsing this. */
    }

    if(errno != NBT_OK) goto parse_error;

#undef COPY_INTO_PAYLOAD

    return node;

parse_error:
    if(errno == NBT_OK)
        errno = NBT_ERR;

    free(node);
    return NULL;
}


nbt_node* nbt_parse(const void* mem, size_t len)
{
    const char** memory = (const char**)&mem;
    size_t* length = &len;

    uint8_t type;
    char* name = NULL;

    errno = NBT_OK;

    READ_GENERIC(&type, sizeof type, memscan, goto parse_error);

    name = read_string(memory, length);
    if(name == NULL) goto parse_error;

    return parse_unnamed_tag((nbt_type)type, name, memory, length);

parse_error:
    if(errno == NBT_OK)
        errno = NBT_ERR;

    free(name);
    return NULL;
}

/*
 * No incremental parsing goes on. We just dump the whole decompressed file into
 * memory then pass the job off to nbt_parse.
 */
nbt_node* nbt_parse_file(FILE* fp)
{
    char* buf = NULL;
    size_t len = 4096;
    nbt_node* ret = NULL;

    gzFile f = gzdopen(fileno(fp), "rb");

    if(f == NULL)
    {
        errno = NBT_EGZ;
        return NULL;
    }

    for(;;)
    {
        size_t bytes_read;
        char* temp = realloc(buf, len * 2);

        if(temp)
            buf = temp;
        else
        {
            errno = NBT_EMEM;
            free(buf);
            gzclose(f); /* No need to error check. */
            return NULL;
        }

        bytes_read = gzread(f, buf + len, len);
        len += bytes_read;

        if(bytes_read == 0)
            break;
    }

    ret = nbt_parse(buf, len);
    free(buf);

    if(gzclose(f) != Z_OK)
    {
        if(errno == NBT_OK)
            errno = NBT_EGZ;

        nbt_free(ret);
        return NULL;
    }

    return ret;
}

nbt_status nbt_dump_ascii(nbt_node* tree, FILE* fp)
{
    return NBT_OK; /* TODO */
}

nbt_status nbt_dump_binary(nbt_node* tree, FILE* fp)
{
    return NBT_OK; /* TODO */
}

static struct tag_list* clone_list(struct tag_list* list)
{
    struct tag_list* ret;
    struct list_head* pos;

    assert(list);

    ret = malloc(sizeof *ret);
    if(ret == NULL) goto clone_error;

    INIT_LIST_HEAD(&ret->entry);
    ret->data = NULL;

    list_for_each(pos, &list->entry)
    {
        struct tag_list* current = list_entry(pos, struct tag_list, entry);
        struct tag_list* new = malloc(sizeof *ret);

        if(new == NULL) goto clone_error;

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
    free_list(ret);
    return NULL;
}

nbt_node* nbt_clone(nbt_node* tree)
{
    nbt_node* ret;

    if(tree == NULL) return NULL;

    CHECKED_MALLOC(ret, sizeof *ret, return NULL);

    *ret = *tree;

    if(tree->type == TAG_LIST || tree->type == TAG_COMPOUND)
    {
        ret->payload.tag_list = clone_list(tree->payload.tag_list);

        if(ret->payload.tag_list == NULL)
        {
            free(ret);
            return NULL;
        }
    }

    return ret;
}

bool nbt_map(nbt_node* tree, nbt_visitor_t v, void* aux)
{
    nbt_node saved_node;

    if(tree == NULL) return true;
    assert(v);

    /* We save the node in case the visitor calls free() on it. */
    saved_node = *tree;

    /* call the visitor on the current item. If it says stop, stop. */
    if(!v(tree, aux)) return false;

    /* And if the item is a list or compound, recurse through each of their elements. */
    if(saved_node.type == TAG_LIST || saved_node.type == TAG_COMPOUND)
    {
        struct list_head* pos;
        struct list_head* n;

        list_for_each_safe(pos, n, &saved_node.payload.tag_list->entry)
            if(!nbt_map(list_entry(pos, struct tag_list, entry)->data, v, aux))
                return false;
    }

    return true;
}

static struct tag_list* filter_list(const struct tag_list* list, nbt_predicate_t predicate, void* aux)
{
    struct tag_list* ret;
    const struct list_head* pos;

    assert(list);

    ret = malloc(sizeof *ret);
    if(ret == NULL) goto filter_error;

    ret->data = NULL;
    INIT_LIST_HEAD(&ret->entry);

    list_for_each(pos, &list->entry)
    {
        const struct tag_list* p = list_entry(pos, struct tag_list, entry);

        struct tag_list* new = malloc(sizeof *new);
        if(new == NULL) goto filter_error;

        new->data = nbt_filter(p->data, predicate, aux);

        if(new->data == NULL)
        {
            free(new);
            continue;
        }

        list_add_tail(&new->entry, &ret->entry);
    }

    return ret;

filter_error:
    if(errno == NBT_OK)
        errno = NBT_EMEM;

    free_list(ret);
    return NULL;
}

nbt_node* nbt_filter(const nbt_node* tree, nbt_predicate_t filter, void* aux)
{
    nbt_node* ret;

    errno = NBT_OK;

    if(tree == NULL) return NULL;

    assert(filter);

    /* Keep this node? */
    if(!filter(tree, aux))
        return NULL;

    ret = malloc(sizeof *ret);
    if(ret == NULL) goto filter_error;

    *ret = *tree;

    /* Okay, we want to keep this node, but keep traversing the tree! */
    if(tree->type == TAG_LIST || tree->type == TAG_COMPOUND)
    {
        ret->payload.tag_list = filter_list(tree->payload.tag_list, filter, aux);
        if(errno != NBT_OK) goto filter_error;
    }

    return ret;

filter_error:
    if(errno == NBT_OK)
        errno = NBT_EMEM;

    free(ret);
    return NULL;
}

static struct tag_list* filter_list_inplace(struct tag_list* list, nbt_predicate_t filter, void* aux)
{
    struct list_head* pos;
    struct list_head* n;

    list_for_each_safe(pos, n, &list->entry)
    {
        struct tag_list* cur = list_entry(pos, struct tag_list, entry);

        cur->data = nbt_filter_inplace(cur->data, filter, aux);

        /* If there are no more elements in this node, free it. */
        if(cur->data == NULL)
        {
            list_del(pos);
            free(cur);
        }
    }

    return list;
}

nbt_node* nbt_filter_inplace(nbt_node* tree, nbt_predicate_t filter, void* aux)
{
    if(tree == NULL) return NULL;

    assert(filter);

    if(!filter(tree, aux))
        return nbt_free(tree), NULL;

    if(tree->type == TAG_LIST || tree->type == TAG_COMPOUND)
        tree->payload.tag_list = filter_list_inplace(tree->payload.tag_list, filter, aux);

    return tree;
}

nbt_node* nbt_find(nbt_node* tree, nbt_predicate_t predicate, void* aux)
{
    if(tree == NULL)         return NULL;
    if(predicate(tree, aux)) return tree;

    if(tree->type == TAG_LIST || tree->type == TAG_COMPOUND)
    {
        struct list_head* pos;

        list_for_each(pos, &tree->payload.tag_list->entry)
        {
            struct tag_list* p = list_entry(pos, struct tag_list, entry);
            struct nbt_node* found;

            if((found = nbt_find(p->data, predicate, aux)))
                return found;
        }
    }

    return NULL;
}

static size_t list_length(struct tag_list* list)
{
    struct list_head* pos;
    size_t accum = 0;

    list_for_each(pos, &list->entry)
        accum++;

    return accum;
}

size_t nbt_size(nbt_node* tree)
{
    if(tree == NULL)
        return 0;

    if(tree->type == TAG_LIST || tree->type == TAG_COMPOUND)
        return list_length(tree->payload.tag_list) + 1;

    return 1;
}

const char* nbt_type_to_string(nbt_type t)
{
#define DEF_CASE(name) case name: return #name;
    switch(t)
    {
        case 0: return "TAG_END";
        DEF_CASE(TAG_BYTE);
        DEF_CASE(TAG_SHORT);
        DEF_CASE(TAG_INT);
        DEF_CASE(TAG_LONG);
        DEF_CASE(TAG_FLOAT);
        DEF_CASE(TAG_DOUBLE);
        DEF_CASE(TAG_BYTE_ARRAY);
        DEF_CASE(TAG_STRING);
        DEF_CASE(TAG_LIST);
        DEF_CASE(TAG_COMPOUND);
    default:
        return "TAG_UNKNOWN";
    }
#undef DEF_CASE
}
