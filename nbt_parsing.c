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

#include "buffer.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/* TODO: Replace this all with ONLY low-level parsing funcs. */

/* works around a bug in icc */
int fileno(FILE*);

/* are we running on a little-endian system? */
static inline int little_endian()
{
    union {
        uint16_t i;
        char c[2];
    } t = { 0x0001 };

    return *t.c == 1;
}

static inline void* swap_bytes(void* s, size_t len)
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

/* big endian to native endian. works in-place */
static inline void* be2ne(void* s, size_t len)
{
    return little_endian() ? swap_bytes(s, len) : s;
}

/* native endian to big endian. works the exact same as its inverse */
#define ne2be be2ne

/* A special form of memcpy which copies `n' bytes into `dest', then returns
 * `src' + n.
 */
static inline const void* memscan(void* dest, const void* src, size_t n)
{
    memcpy(dest, src, n);
    return (const char*)src + n;
}

/* Does a memscan, then goes from big endian to native endian on the
 * destination.
 */
static inline const void* swapped_memscan(void* dest, const void* src, size_t n)
{
    const void* ret = memscan(dest, src, n);
    return be2ne(dest, n), ret;
}

#define CHECKED_MALLOC(var, n, on_error) do { \
    if((var = malloc(n)) == NULL)             \
    {                                         \
        errno = NBT_EMEM;                     \
        on_error;                             \
    }                                         \
} while(0)

#define CHECKED_GZWRITE(fp, ptr, len) do {        \
    if(gzwrite((fp), (ptr), (len)) != (int)(len)) \
        return NBT_EGZ;                           \
} while(0)

/* Parses a tag, given a name (may be NULL) and a type. Fills in the payload. */
static nbt_node* parse_unnamed_tag(nbt_type type, char* name, const char** memory, size_t* length);

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
static inline char* read_string(const char** memory, size_t* length)
{
    int16_t string_length;
    char* ret = NULL;

    READ_GENERIC(&string_length, sizeof string_length, swapped_memscan, goto parse_error);

    if(string_length < 0)               goto parse_error;
    if(*length < (size_t)string_length) goto parse_error;

    CHECKED_MALLOC(ret, string_length + 1, goto parse_error);

    READ_GENERIC(ret, (size_t)string_length, memscan, goto parse_error);

    ret[string_length] = '\0'; /* don't forget to NULL-terminate ;) */
    return ret;

parse_error:
    if(errno == NBT_OK)
        errno = NBT_ERR;

    free(ret);
    return NULL;
}

static inline struct nbt_byte_array read_byte_array(const char** memory, size_t* length)
{
    struct nbt_byte_array ret;
    ret.data = NULL;

    READ_GENERIC(&ret.length, sizeof ret.length, swapped_memscan, goto parse_error);

    if(ret.length < 0) goto parse_error;

    CHECKED_MALLOC(ret.data, ret.length, goto parse_error);

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

    READ_GENERIC(&type, sizeof type, swapped_memscan, goto parse_error);
    READ_GENERIC(&elems, sizeof elems, swapped_memscan, goto parse_error);

    CHECKED_MALLOC(ret, sizeof *ret, goto parse_error);

    ret->data = NULL; /* the first value in a list is a sentinel. don't even try to read it. */
    INIT_LIST_HEAD(&ret->entry);

    for(int32_t i = 0; i < elems; i++)
    {
        struct tag_list* new;

        CHECKED_MALLOC(new, sizeof *new, goto parse_error);

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

    nbt_free_list(ret);
    return NULL;
}

static struct tag_list* read_compound(const char** memory, size_t* length)
{
    struct tag_list* ret;

    CHECKED_MALLOC(ret, sizeof *ret, goto parse_error);

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
    nbt_free_list(ret);

    return NULL;
}

/*
 * Parses a tag, given a name (may be NULL) and a type. Fills in the payload.
 */
static inline nbt_node* parse_unnamed_tag(nbt_type type, char* name, const char** memory, size_t* length)
{
    nbt_node* node;

    CHECKED_MALLOC(node, sizeof *node, goto parse_error);

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

#undef COPY_INTO_PAYLOAD

    if(errno != NBT_OK) goto parse_error;

    return node;

parse_error:
    if(errno == NBT_OK)
        errno = NBT_ERR;

    free(node);
    return NULL;
}


nbt_node* nbt_parse(const void* mem, size_t len)
{
    errno = NBT_OK;

    const char** memory = (const char**)&mem;
    size_t* length = &len;

    /*
     * this needs to stay up here since it's referenced by the parse_error
     * block.
     */
    char* name = NULL;

    uint8_t type;
    READ_GENERIC(&type, sizeof type, memscan, goto parse_error);

    name = read_string(memory, length);
    if(name == NULL) goto parse_error;

    nbt_node* ret = parse_unnamed_tag((nbt_type)type, name, memory, length);

    /* We can't check for NULL, because it COULD be an empty tree. */
    if(errno != NBT_OK) goto parse_error;

    return ret;

parse_error:
    if(errno == NBT_OK)
        errno = NBT_ERR;

    free(name);
    return NULL;
}

/* parses the whole file into a buffer */
static inline struct buffer __parse_file(gzFile fp)
{
    struct buffer ret;

    char buf[4096];
    size_t bytes_read;

    if(buffer_init(&ret))
    {
        errno = NBT_EMEM;
        return (struct buffer) { NULL, 0, 0 };
    }

    while((bytes_read = gzread(fp, buf, 4096)) > 0)
    {
        int err;
        gzerror(fp, &err);
        if(err)
        {
            errno = NBT_EGZ;
            goto parse_error;
        }

        if(buffer_append(&ret, buf, bytes_read))
        {
            errno = NBT_EMEM;
            goto parse_error;
        }
    }

    return ret;

parse_error:
    buffer_free(&ret);
    ret.data = NULL;
    return ret;
}

/*
 * No incremental parsing goes on. We just dump the whole decompressed file into
 * memory then pass the job off to nbt_parse.
 */
nbt_node* nbt_parse_file(FILE* fp)
{
    nbt_node* ret;

    errno = NBT_OK;

    /*
     * We need to keep these declarations up here as opposed to where they're
     * used because they're referenced by the parse_error block.
     */
    struct buffer buf = { NULL, 0, 0 };
    gzFile f = Z_NULL;

                           if(fp == NULL)         goto parse_error;
    int fd = fileno(fp);   if(fd == -1)           goto parse_error;
    f = gzdopen(fd, "rb"); if(f == Z_NULL)        goto parse_error;

    buf = __parse_file(f);

                           if(buf.data == NULL)   goto parse_error;
                           if(gzclose(f) != Z_OK) goto parse_error;

    ret = nbt_parse(buf.data, buf.len);


    buffer_free(&buf);
    return ret;

parse_error:
    if(errno == NBT_OK)
        errno = NBT_EGZ;

    buffer_free(&buf);

    if(f != Z_NULL)
        gzclose(f);

    return NULL;
}

/* spaces, not tabs ;) */
static inline void indent(FILE* fp, size_t amount)
{
    for(size_t i = 0; i < amount; i++)
        fprintf(fp, "    ");
}

static nbt_status __nbt_dump_ascii(const nbt_node* tree, FILE* fp, size_t ident);

/* prints the node's name, or (null) if it has none. */
#define SAFE_NAME(node) ((node)->name ? (node)->name : "<null>")

static inline void dump_byte_array(const struct nbt_byte_array ba, FILE* fp)
{
    assert(ba.length >= 0);

    fprintf(fp, "[ ");
    for(int32_t i = 0; i < ba.length; ++i)
        fprintf(fp, "%i ", (int)ba.data[i]);
    fprintf(fp, "]");
}

static inline nbt_status dump_list_contents_ascii(const struct tag_list* list, FILE* fp, size_t ident)
{
    const struct list_head* pos;

    list_for_each(pos, &list->entry)
    {
        const struct tag_list* entry = list_entry(pos, const struct tag_list, entry);
        nbt_status err;

        if((err = __nbt_dump_ascii(entry->data, fp, ident)) != NBT_OK)
            return err;
    }

    return NBT_OK;
}

static inline nbt_status __nbt_dump_ascii(const nbt_node* tree, FILE* fp, size_t ident)
{
    if(tree == NULL) return NBT_OK;

    indent(fp, ident);

    if(tree->type == TAG_BYTE)
        fprintf(fp, "TAG_Byte(\"%s\"): %i\n", SAFE_NAME(tree), (int)tree->payload.tag_byte);
    else if(tree->type == TAG_SHORT)
        fprintf(fp, "TAG_Short(\"%s\"): %i\n", SAFE_NAME(tree), (int)tree->payload.tag_short);
    else if(tree->type == TAG_INT)
        fprintf(fp, "TAG_Int(\"%s\"): %i\n", SAFE_NAME(tree), (int)tree->payload.tag_int);
    else if(tree->type == TAG_LONG)
        fprintf(fp, "TAG_Long(\"%s\"): %" PRIi64 "\n", SAFE_NAME(tree), tree->payload.tag_long);
    else if(tree->type == TAG_FLOAT)
        fprintf(fp, "TAG_Float(\"%s\"): %f\n", SAFE_NAME(tree), (double)tree->payload.tag_float);
    else if(tree->type == TAG_DOUBLE)
        fprintf(fp, "TAG_Double(\"%s\"): %f\n", SAFE_NAME(tree), tree->payload.tag_double);
    else if(tree->type == TAG_BYTE_ARRAY)
    {
        fprintf(fp, "TAG_Byte_Array(\"%s\"): ", SAFE_NAME(tree));
        dump_byte_array(tree->payload.tag_byte_array, fp);
        fprintf(fp, "\n");
    }
    else if(tree->type == TAG_STRING)
    {
        if(tree->payload.tag_string == NULL)
            return NBT_ERR;

        fprintf(fp, "TAG_String(\"%s\"): %s\n", SAFE_NAME(tree), tree->payload.tag_string);
    }
    else if(tree->type == TAG_LIST)
    {
        fprintf(fp, "TAG_List(\"%s\")\n", SAFE_NAME(tree));
        indent(fp, ident);
        fprintf(fp, "{\n");

        nbt_status err;
        if((err = dump_list_contents_ascii(tree->payload.tag_list, fp, ident + 1)) != NBT_OK)
            return err;

        indent(fp, ident);
        fprintf(fp, "}\n");
    }
    else if(tree->type == TAG_COMPOUND)
    {
        fprintf(fp, "TAG_Compound(\"%s\")\n", SAFE_NAME(tree));
        indent(fp, ident);
        fprintf(fp, "{\n");

        nbt_status err;
        if((err = dump_list_contents_ascii(tree->payload.tag_compound, fp, ident + 1)) != NBT_OK)
            return err;

        indent(fp, ident);
        fprintf(fp, "}\n");
    }

    else
        return NBT_ERR;

    return NBT_OK;
}

nbt_status nbt_dump_ascii(const nbt_node* tree, FILE* fp)
{
    return __nbt_dump_ascii(tree, fp, 0);
}

static nbt_status dump_byte_array_binary(const struct nbt_byte_array ba, gzFile fp)
{
    int32_t dumped_length = ba.length;

    ne2be(&dumped_length, sizeof dumped_length);

    CHECKED_GZWRITE(fp, &dumped_length, sizeof dumped_length);

    if(ba.length) assert(ba.data);

    CHECKED_GZWRITE(fp, ba.data, ba.length);

    return NBT_OK;
}

static nbt_status dump_string_binary(const char* name, gzFile fp)
{
    assert(name);

    size_t len = strlen(name);

    if(len > 32767 /* SHORT_MAX */)
        return NBT_ERR;

    { /* dump the length */
        int16_t dumped_len = (int16_t)len;
        ne2be(&dumped_len, sizeof dumped_len);

        CHECKED_GZWRITE(fp, &dumped_len, sizeof dumped_len);
    }

    CHECKED_GZWRITE(fp, name, len);

    return NBT_OK;
}

/*
 * Is the list all one type? If yes, return the type. Otherwise, return
 * TAG_INVALID
 */
static inline nbt_type list_is_homogenous(const struct tag_list* list)
{
    nbt_type type = TAG_INVALID;

    const struct list_head* pos;
    list_for_each(pos, &list->entry)
    {
        const struct tag_list* cur = list_entry(pos, const struct tag_list, entry);

        assert(cur->data->type != TAG_INVALID);

        if(cur->data->type == TAG_INVALID)
            return TAG_INVALID;

        /* if we're the first type, just set it to our current type */
        if(type == TAG_INVALID) type = cur->data->type;

        if(type != cur->data->type)
            return TAG_INVALID;
    }

    return type;
}

static nbt_status __dump_binary(const nbt_node*, bool, gzFile);

static nbt_status dump_list_binary(const struct tag_list* list, gzFile fp)
{
    nbt_type type;

    size_t len = list_length(&list->entry);

    if(len == 0) /* empty lists can just be silently ignored */
        return NBT_OK;

    if(len > 2147483647 /* INT_MAX */)
        return NBT_ERR;

    assert(list_is_homogenous(list) != TAG_INVALID);
    if((type = list_is_homogenous(list)) == TAG_INVALID)
        return NBT_ERR;

    {
        int8_t _type = (int8_t)type;
        ne2be(&_type, sizeof _type); /* unnecessary, but left in to keep similar code looking similar */
        CHECKED_GZWRITE(fp, &_type, sizeof _type);
    }

    {
        int32_t dumped_len = (int32_t)len;
        ne2be(&dumped_len, sizeof dumped_len);
        CHECKED_GZWRITE(fp, &dumped_len, sizeof dumped_len);
    }

    const struct list_head* pos;
    list_for_each(pos, &list->entry)
    {
        const struct tag_list* entry = list_entry(pos, const struct tag_list, entry);
        nbt_status ret;

        if((ret = __dump_binary(entry->data, false, fp)) != NBT_OK)
            return ret;
    }

    return NBT_OK;
}

static nbt_status dump_compound_binary(const struct tag_list* list, gzFile fp)
{
    if(list_empty(&list->entry)) /* empty lists can just be silently ignored */
        return NBT_OK;

    const struct list_head* pos;
    list_for_each(pos, &list->entry)
    {
        const struct tag_list* entry = list_entry(pos, const struct tag_list, entry);
        nbt_status ret;

        if((ret = __dump_binary(entry->data, true, fp)) != NBT_OK)
            return ret;
    }

    /* write out TAG_End */
    uint8_t zero = 0;
    CHECKED_GZWRITE(fp, &zero, sizeof zero);

    return NBT_OK;
}

/*
 * @param dump_type   Should we dump the type, or just skip it? We need to skip
 *                    when dumping lists, because the list header already says
 *                    the type.
 */
static inline nbt_status __dump_binary(const nbt_node* tree, bool dump_type, gzFile fp)
{
    if(dump_type)
    { /* write out the type */
        int8_t type = (int8_t)tree->type;

        CHECKED_GZWRITE(fp, &type, sizeof type);
    }

    if(tree->name)
    {
        nbt_status err;

        if((err = dump_string_binary(tree->name, fp)) != NBT_OK)
            return err;
    }

#define DUMP_NUM(type, x) do {               \
    type temp = x;                           \
    ne2be(&temp, sizeof temp);               \
    CHECKED_GZWRITE(fp, &temp, sizeof temp); \
} while(0)

    if(tree->type == TAG_BYTE)
        DUMP_NUM(int8_t, tree->payload.tag_byte);
    else if(tree->type == TAG_SHORT)
        DUMP_NUM(int16_t, tree->payload.tag_short);
    else if(tree->type == TAG_INT)
        DUMP_NUM(int32_t, tree->payload.tag_int);
    else if(tree->type == TAG_LONG)
        DUMP_NUM(int64_t, tree->payload.tag_long);
    else if(tree->type == TAG_FLOAT)
        DUMP_NUM(float, tree->payload.tag_float);
    else if(tree->type == TAG_DOUBLE)
        DUMP_NUM(double, tree->payload.tag_double);
    else if(tree->type == TAG_BYTE_ARRAY)
        return dump_byte_array_binary(tree->payload.tag_byte_array, fp);
    else if(tree->type == TAG_STRING)
        return dump_string_binary(tree->payload.tag_string, fp);
    else if(tree->type == TAG_LIST)
        return dump_list_binary(tree->payload.tag_list, fp);
    else if(tree->type == TAG_COMPOUND)
        return dump_compound_binary(tree->payload.tag_compound, fp);

    else
        return NBT_ERR;

    return NBT_OK;

#undef DUMP_NUM
}

nbt_status nbt_dump_binary(const nbt_node* tree, FILE* fp)
{
    if(tree == NULL) return NBT_OK;

    int fd = fileno(fp);
    if(fd == -1) return NBT_EGZ;

    gzFile f = gzdopen(fd, "wb");
    if(f == Z_NULL) return NBT_EGZ;

    nbt_status r = __dump_binary(tree, true, f);

    gzclose(f);

    return r;
}

