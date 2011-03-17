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
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/* works around a bug in clang. */
char* strdup(const char*);

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

#define CHECKED_MALLOC(var, n, on_error) do {       \
    var = malloc(n);                                \
    if(var == NULL) { errno = NBT_EMEM; on_error; } \
} while(0)

/* Parses a tag, given a name (may be NULL) and a type. Fills in the payload. */
static nbt_node* parse_unnamed_tag(nbt_type type, char* name, const char** memory, size_t* length);

static inline void free_list(struct tag_list* list)
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
    if(tree == NULL) return;

    free(tree->name);

    if(tree->type == TAG_LIST || tree->type == TAG_COMPOUND)
        free_list(tree->payload.tag_list);

    if(tree->type == TAG_BYTE_ARRAY)
        free(tree->payload.tag_byte_array.data);

    if(tree->type == TAG_STRING)
        free(tree->payload.tag_string);

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

    free_list(ret);
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
    free_list(ret);

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

    if(errno != NBT_OK) goto parse_error;

    return ret;

parse_error:
    if(errno == NBT_OK)
        errno = NBT_ERR;

    free(name);
    return NULL;
}

typedef struct {
    char*  d; /*  data  */
    size_t l; /* length */
} buffer_t;

/* parses the whole file into a buffer */
static inline buffer_t __parse_file(gzFile fp)
{
    buffer_t ret = { NULL, 0 };

    /* WARNING: This loop runs in O(n^2). TODO: Fix this! */
    for(;;)
    {
        { /* resize buffer */
            char* temp = realloc(ret.d, ret.l + 4096);
            if(temp == NULL) goto parse_error;
            ret.d = temp;
        }

        /* copy in */
        size_t bytes_read = gzread(fp, ret.d + ret.l, 4096);

        int err;
        gzerror(fp, &err);
        if(err) { errno = NBT_EGZ; goto parse_error; }

        if(bytes_read == 0) break;

        /* fix ret.l */
        ret.l += bytes_read;
    }

    return ret;

parse_error:
    if(errno == NBT_OK)
        errno = NBT_EMEM;

    free(ret.d);
    return ret;
}

/*
 * No incremental parsing goes on. We just dump the whole decompressed file into
 * memory then pass the job off to nbt_parse.
 */
nbt_node* nbt_parse_file(FILE* fp)
{
    nbt_node* ret;

    /*
     * We need to keep these declarations up here as opposed to where they're
     * used because they're referenced by the parse_error block.
     */
    buffer_t buf = { NULL, 0 };
    gzFile f = Z_NULL;

    if(fp == NULL) return NULL;

    int fd = fileno(fp);
    if(fd == -1)    goto parse_error;

    f = gzdopen(fd, "rb");
    if(f == Z_NULL) goto parse_error;

    buf = __parse_file(f);

    if(buf.d == NULL)      goto parse_error;
    if(gzclose(f) != Z_OK) goto parse_error;

    ret = nbt_parse(buf.d, buf.l);

    free(buf.d);

    return ret;

parse_error:
    if(errno == NBT_OK)
        errno = NBT_EGZ;

    free(buf.d);

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

/* prints the node's name, or "" if it has none. */
#define SAFE_NAME(node) ((node)->name ? (node)->name : "")

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

    if(gzwrite(fp, &dumped_length, sizeof dumped_length) == 0)
        return NBT_EGZ;

    if(ba.length) assert(ba.data);

    if(gzwrite(fp, ba.data, ba.length) == 0)
        return NBT_EGZ;

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

        if(gzwrite(fp, &dumped_len, sizeof dumped_len) == 0)
            return NBT_EGZ;
    }

    if(gzwrite(fp, name, len) == 0)
        return NBT_EGZ;

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

static nbt_status __dump_binary(const nbt_node*, gzFile);

static nbt_status dump_list_binary(const struct tag_list* list, gzFile fp)
{
    nbt_type type;

    size_t len = list_length(&list->entry);

    if(len == 0) /* empty lists can just be silently ignored */
        return NBT_OK;

    if(len > 2147483647 /* INT_MAX */)
        return NBT_ERR;

    assert(list_is_homogenous(list));
    if((type = list_is_homogenous(list)) == TAG_INVALID)
        return NBT_ERR;

    if(gzwrite(fp, &type, 1) == 0)
        return NBT_EGZ;

    {
        int32_t dumped_len = (int32_t)len;
        ne2be(&dumped_len, sizeof dumped_len);
        if(gzwrite(fp, &dumped_len, sizeof dumped_len) == 0)
            return NBT_EGZ;
    }

    const struct list_head* pos;
    list_for_each(pos, &list->entry)
    {
        const struct tag_list* entry = list_entry(pos, const struct tag_list, entry);
        nbt_status ret;

        if((ret = __dump_binary(entry->data, fp)) != NBT_OK)
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

        if((ret = __dump_binary(entry->data, fp)) != NBT_OK)
            return ret;
    }

    /* write out TAG_End */
    uint8_t zero = 0;
    if(gzwrite(fp, &zero, 1) == 0)
        return NBT_EGZ;

    return NBT_OK;
}

static inline nbt_status __dump_binary(const nbt_node* tree, gzFile fp)
{
    { /* write out the type */
        int8_t type = (int8_t)tree->type;

        if(gzwrite(fp, &type, 1) == 0)
            return NBT_EGZ;
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
    if(gzwrite(fp, &temp, sizeof temp) == 0) \
        return NBT_EGZ;                      \
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

        return NBT_ERR;

    return NBT_OK;

#undef DUMP_NUM
}

nbt_status nbt_dump_binary(const nbt_node* tree, FILE* fp)
{
    if(tree == NULL) return NBT_OK;

    gzFile f = gzdopen(fileno(fp), "wb");
    nbt_status r = __dump_binary(tree, f);
    gzclose(f);

    return r;
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
    free_list(ret);
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

    free_list(ret);
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
