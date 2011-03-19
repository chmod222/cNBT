#include "nbt.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void die(const char* message)
{
    fprintf(stderr, "%s\n", message);
    exit(1);
}

static void die_with_err(int err)
{
    if(err == NBT_OK)
        die("No error.");
    else if(err == NBT_ERR)
        die("Parse error.");
    else if(err == NBT_EMEM)
        die("Out of memory.");
    else if(err == NBT_EGZ)
        die("GZip error.");

    else
    {
        fprintf(stderr, "errno: %i\n", err);
        die("Unknown error.");
    }
}

static nbt_node* get_tree(const char* filename)
{
    FILE* fp = fopen(filename, "rb");
    if(fp == NULL) die("Could not open the file for reading.");

    nbt_node* ret = nbt_parse_file(fp);
    if(ret == NULL) die_with_err(errno);
    fclose(fp);

    return ret;
}

/* Returns 1 if one is null and the other isn't. */
static int safe_strcmp(const char* a, const char* b)
{
    if(a == NULL)
        return b != NULL; /* a is NULL, b is not */

    if(b == NULL) /* b is NULL, a is not */
        return 1;

    return strcmp(a, b);
}

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

static inline bool floats_are_close(double a, double b)
{
    double epsilon = 0.000001;
    return (min(a, b) + epsilon) >= max(a, b);
}

bool check_tree_equal(const nbt_node* restrict a, const nbt_node* restrict b)
{
    if(a->type != b->type)
        return false;

    if(safe_strcmp(a->name, b->name) != 0)
        return false;

    switch(a->type)
    {
    case TAG_BYTE:
        return a->payload.tag_byte == b->payload.tag_byte;
    case TAG_SHORT:
        return a->payload.tag_short == b->payload.tag_short;
    case TAG_INT:
        return a->payload.tag_int == b->payload.tag_int;
    case TAG_LONG:
        return a->payload.tag_long == b->payload.tag_long;
    case TAG_FLOAT:
        return floats_are_close((double)a->payload.tag_float, (double)b->payload.tag_float);
    case TAG_DOUBLE:
        return floats_are_close(a->payload.tag_double, b->payload.tag_double);
    case TAG_BYTE_ARRAY:
        if(a->payload.tag_byte_array.length != b->payload.tag_byte_array.length) return false;
        return memcmp(a->payload.tag_byte_array.data,
                      b->payload.tag_byte_array.data,
                      a->payload.tag_byte_array.length) == 0;
    case TAG_STRING:
        return strcmp(a->payload.tag_string, b->payload.tag_string) == 0;
    case TAG_LIST:
    case TAG_COMPOUND:
        if(list_length(&a->payload.tag_list->entry) != list_length(&b->payload.tag_list->entry))
            return false;

        for(struct list_head* ai = a->payload.tag_list->entry.flink,
                            * bi = b->payload.tag_list->entry.flink;
            ai != &a->payload.tag_list->entry;
            ai = ai->flink,
            bi = bi->flink)
        {
            struct tag_list* ae = list_entry(ai, struct tag_list, entry);
            struct tag_list* be = list_entry(bi, struct tag_list, entry);

            if(!check_tree_equal(ae->data, be->data))
                return false;
        }

        return true;

    default: /* wtf invalid type */
        return false;
    }
}

int main(int argc, char** argv)
{
    if(argc == 1 || strcmp(argv[1], "--help") == 0)
    {
        printf("Usage: %s [nbt file]\n", argv[0]);
        return 0;
    }

    nbt_node* tree = get_tree(argv[1]);

    printf("Parsing...\n");
    nbt_status err;
    if((err = nbt_dump_ascii(tree, stdout)) != NBT_OK)
        die_with_err(err);

    {
        printf("Checking nbt_clone... ");
        nbt_node* clone = nbt_clone(tree);
        if(!check_tree_equal(tree, clone))
            die("FAILED.");
        nbt_free(clone);
        printf("OK.\n");
    }

    FILE* temp = fopen("delete_me.nbt", "wb");
    if(temp == NULL) die("Could not open a temporary file.");

    printf("Dumping binary...\n");
    if((err = nbt_dump_binary(tree, temp)) != NBT_OK)
        die_with_err(err);

    fclose(temp);
    temp = fopen("delete_me.nbt", "rb");
    if(temp == NULL) die("Could not re-open a temporary file.");

    printf("Reparsing...\n");
    nbt_node* tree_copy = nbt_parse_file(temp);
    if(tree_copy == NULL) die_with_err(errno);

    printf("Checking trees...\n");
    if(!check_tree_equal(tree, tree_copy))
    {
        printf("Reread tree:\n");
        if((err = nbt_dump_ascii(tree_copy, stdout)) != NBT_OK)
            die_with_err(err);

        die("Trees not equal.");
    }

    fclose(temp);

    nbt_free(tree);
    nbt_free(tree_copy);

    printf("OK.\n");
    return 0;
}
