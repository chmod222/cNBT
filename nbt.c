/*
* -----------------------------------------------------------------------------
* "THE BEER-WARE LICENSE" (Revision 42):
* <webmaster@flippeh.de> wrote this file. As long as you retain this notice you
* can do whatever you want with this stuff. If we meet some day, and you think
* this stuff is worth it, you can buy me a beer in return. Lukas Niederbremer.
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

    return t.c[0] == 0;
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

/* Does a memcpy, then performs a byteswap on the newly copied memory */
static void* swapped_memcpy(void* dest, const void* src, size_t n)
{
    memcpy(dest, src, n);
    return be2ne(dest, n);
}

/*
 * Reads a string from memory, moving the pointer and updating the length
 * appropriately. Returns NULL on failure.
 */
static char* read_string(const char** memory, size_t* length)
{
    int16_t string_length;
    char* ret;

    if(*length < sizeof string_length) return NULL;
    swapped_memcpy(&string_length, *memory, sizeof string_length);

    *memory += sizeof string_length;
    *length -= sizeof string_length;

    if(string_length < 0) return NULL;
    if(*length < (size_t)string_length) return NULL;

    ret = malloc(string_length + 1);
    memcpy(ret, *memory, string_length);
    ret[string_length] = '\0';

    *memory += string_length;
    *length -= string_length;

    return ret;
}

/* TODO: Read a list in from memory */
static struct tag_list* read_list(const void* memory, size_t length)
{
    struct tag_list* ret = NULL;
    return ret;
}

/* TODO: Read a compound in from memory */
static struct tag_list* read_compound(const void* memory, size_t length)
{
    struct tag_list* ret = NULL;
    return ret;
}

nbt_node* nbt_parse(const void* memory, size_t length)
{
    nbt_node* node = malloc(sizeof *node);

    if(node == NULL)
    {
        errno = NBT_EMEM;
        return NULL;
    }

    if(length < 1) goto parse_error;
    memcpy(&node->type, memory, 1);

    node->name = read_string(&memory, &length);
    if(node->name == NULL) goto parse_error;

#define COPY_INTO_PAYLOAD(payload_name) do {                                                \
    if(length < sizeof node->payload.payload_name) goto parse_error;                        \
    swapped_memcpy(&node->payload.payload_name, memory, sizeof node->payload.payload_name); \
} while(0)

    switch(node->type)
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
        COPY_INTO_PAYLOAD(tag_byte_array.length);
        if(node->payload.tag_byte_array.length < 0) goto parse_error;
        node->payload.tag_byte_array.data = malloc(node->payload.tag_byte_array.length);

        length -= sizeof(int32_t);
        memory = (const char*)memory + sizeof(int32_t);

        if(length < node->payload.tag_byte_array.length) goto parse_error;
        memcpy(node->payload.tag_byte_array.data, memory, node->payload.tag_byte_array.length);

        break;
    case TAG_STRING:
        node->payload.tag_string = read_string(&memory, &length);
        if(node->payload.tag_string == NULL) goto parse_error;
        break;
    case TAG_LIST:
        node->payload.tag_list = read_list(memory, length);
        if(node->payload.tag_list == NULL) goto parse_error;
        break;
    case TAG_COMPOUND:
        node->payload.tag_compound = read_compound(memory, length);
        if(node->payload.tag_compound == NULL) goto parse_error;
        break;

    default:
        goto parse_error; /* Unknown node or TAG_END. Either way, we shouldn't be parsing this. */
    }

#undef COPY_INTO_PAYLOAD

    return node;

parse_error:
    free(node);
    errno = NBT_ERR;
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
        errno = NBT_EGZ;
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

    if(list == NULL) return NULL;

    ret = malloc(sizeof *ret);
    ret->data = nbt_clone(list->data);
    ret->next = clone_list(list->next);

    return ret;
}

nbt_node* nbt_clone(nbt_node* tree)
{
    nbt_node* ret;

    if(tree == NULL) return NULL;

    ret = malloc(sizeof *ret);
    *ret = *tree;

    if(tree->type == TAG_LIST)
        ret->payload.tag_list = clone_list(tree->payload.tag_list);

    if(tree->type == TAG_COMPOUND)
        ret->payload.tag_compound = clone_list(tree->payload.tag_compound);

    return ret;
}

/* A version of free() that conforms to visitor_t. */
static bool node_free(nbt_node* n, void* aux)
{
    free(n);
    return true;
}

void nbt_free(nbt_node* tree)
{
    nbt_map(tree, &node_free, NULL);
}

/* Iterates through each of the elements in a tag_list. */
static bool for_each_list(struct tag_list* list, nbt_visitor_t v, void* aux)
{
    if(list == NULL) return true;

    /* We iterate through our current element's data... */
    if(!nbt_map(list->data, v, aux)) return false;

    /* Then hit the next one! */
    return for_each_list(list->next, v, aux);
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

    if(saved_node.type == TAG_LIST)
        return for_each_list(saved_node.payload.tag_list, v, aux);

    if(saved_node.type == TAG_COMPOUND)
        return for_each_list(saved_node.payload.tag_compound, v, aux);

    return true;
}

static struct tag_list* filter_list(const struct tag_list* list, nbt_filter_t filter, void* aux)
{
    struct tag_list* ret;

    if(list == NULL) return NULL;

    ret = malloc(sizeof *ret);

    ret->data = nbt_filter(list->data, filter, aux);

    if(ret->data == NULL)
        return free(ret), filter_list(list->next, filter, aux);

    ret->next = filter_list(list->next, filter, aux);
    return ret;
}

/* TODO: OOM error checking/reporting */
nbt_node* nbt_filter(const nbt_node* tree, nbt_filter_t filter, void* aux)
{
    nbt_node* ret;

    if(tree == NULL) return NULL;

    assert(filter);

    /* Keep this node? */
    if(!filter(tree, aux))
        return NULL;

    ret = malloc(sizeof *ret);
    *ret = *tree;

    /* Okay, we want to keep this node, but keep traversing the tree! */

    if(tree->type == TAG_LIST)
        ret->payload.tag_list = filter_list(tree->payload.tag_list, filter, aux);

    if(tree->type == TAG_COMPOUND)
        ret->payload.tag_compound = filter_list(tree->payload.tag_compound, filter, aux);

    return ret;
}

static struct tag_list* filter_list_inplace(struct tag_list* list, nbt_filter_t filter, void* aux)
{
    if(list == NULL) return NULL;

    list->data = nbt_filter_inplace(list->data, filter, aux);

    if(list->data == NULL)
    {
        struct tag_list* next = list->next;
        free(list);
        return filter_list_inplace(next, filter, aux);
    }

    list->next = filter_list_inplace(list->next, filter, aux);

    return list;
}

nbt_node* nbt_filter_inplace(nbt_node* tree, nbt_filter_t filter, void* aux)
{
    if(tree == NULL) return NULL;

    assert(filter);

    if(!filter(tree, aux))
        return nbt_free(tree), NULL;

    if(tree->type == TAG_LIST)
        tree->payload.tag_list = filter_list_inplace(tree->payload.tag_list, filter, aux);

    if(tree->type == TAG_COMPOUND)
        tree->payload.tag_compound = filter_list_inplace(tree->payload.tag_compound, filter, aux);

    return tree;
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
