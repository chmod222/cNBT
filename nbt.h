/*
 * -----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <webmaster@flippeh.de> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return. Lukas Niederbremer.
 * -----------------------------------------------------------------------------
 */

#ifndef NBT_H
#define NBT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h> /* for size_t */
#include <stdint.h>
#include <stdio.h> /* for FILE* */

#include "list.h" /* For struct list_entry etc. */

/* I wish I could just use stdbool.h :( */
#ifndef bool
#define bool int
#endif

#ifndef true
#define true 1
#endif

#ifndef false
#define false 0
#endif

typedef enum {
    NBT_OK   =  0, /* No error. */
    NBT_ERR  = -1, /* Generic error, most likely of the parsing variety. */
    NBT_EMEM = -2, /* Out of memory. */
    NBT_EGZ  = -3  /* GZip decompression error. */
} nbt_status;

typedef enum
{
   TAG_BYTE       = 1, /* char, 8 bits, signed */
   TAG_SHORT      = 2, /* short, 16 bits, signed */
   TAG_INT        = 3, /* long, 32 bits, signed */
   TAG_LONG       = 4, /* long long, 64 bits, signed */
   TAG_FLOAT      = 5, /* float, 32 bits, signed */
   TAG_DOUBLE     = 6, /* double, 64 bits, signed */
   TAG_BYTE_ARRAY = 7, /* char *, 8 bits, unsigned, TAG_INT length */
   TAG_STRING     = 8, /* char *, 8 bits, signed, TAG_SHORT length */
   TAG_LIST       = 9, /* X *, X bits, TAG_INT length, no names inside */
   TAG_COMPOUND   = 10 /* nbt_tag * */

} nbt_type;

/*
 * Represents a single node in the tree. You should switch on `type' and ONLY
 * access the union member it signifies. tag_compound and tag_list contain
 * recursive nbt_node entries, so those will have to be switched on too. I
 * recommended being VERY comfortable with recursion before traversing this
 * beast.
 */
typedef struct nbt_node {
    nbt_type type;
    char* name; /* This may be NULL. Check your damn pointers. */

    union { /* payload */

        /* tag_end has no payload */
        int8_t  tag_byte;
        int16_t tag_short;
        int32_t tag_int;
        int64_t tag_long;
        float   tag_float;
        double  tag_double;

        struct nbt_byte_array {
            unsigned char* data;
            int32_t length;
        } tag_byte_array;

        char* tag_string; /* TODO: technically, this should be a UTF-8 string */

        /*
         * Design addendum: we make tag_list a linked list instead of an array
         * so that nbt_node can be a true recursive data structure. If we used
         * an array, it would be incorrect to call free() on any element except
         * the first one. By using a linked list, the context of the node is
         * irrelevant. One tradeoff of this design is that we don't get tight
         * list packing when memory is a concern and huge lists are created.
         *
         * For more information on using the linked list, see `list.h'. It was
         * ripped wholesale from the linux kernel, so feel free to see driver
         * source code for usage information.
         */
        struct tag_list {
            struct nbt_node* data; /* A single node's data. */
            struct list_head entry;
        } *tag_list, /* The only difference between a list and a compound is its name */
          *tag_compound;

    } payload;
} nbt_node;

/* Creation and destruction functions. */

/*
 * Loads a NBT tree binary dump from memory. The tree MUST NOT be compressed. If
 * an error occurs, NULL will be returned, and errno will be set to the
 * appropriate nbt_status. Please check your damn pointers.
 *
 * TODO: Allow a compressed tree to be used. I'm not familiar enough with zlib
 *       to write this.
 */
nbt_node* nbt_parse(const void* memory, size_t length);

/*
 * Loads an NBT tree binary dump from a file. The file MUST have been compressed
 * with gzip. If an error occurs, NULL will be returned and errno will be set to
 * the appropriate nbt_status. Please check your damn pointers.
 */
nbt_node* nbt_parse_file(FILE* fp);

/*
 * Dumps an nbt tree to a file, in ascii format. It will be indented and
 * displayed as nicely as possible. With spaces and not tabs, of course ;)
 * 
 * If an error occurs, the function will return the appropriate nbt_status.
 * Please check your damn error codes.
 */
nbt_status nbt_dump_ascii(const nbt_node* tree, FILE* fp);
nbt_status nbt_dump_binary(const nbt_node* tree, FILE* fp);

/*
 * Clones an existing tree. Returns NULL on memory errors.
 */
nbt_node* nbt_clone(nbt_node*);

/*
 * Recursively deallocates a node and all its children. If this is used on a an
 * entire tree, no memory will be leaked.
 */
void nbt_free(nbt_node*);

/* Utility functions. */

/*
 * A visitor function to traverse the tree. Return true to keep going, false to
 * stop. `aux' is an optional parameter which will be passed to your visitor
 * from the parent function.
 */
typedef bool (*nbt_visitor_t)(nbt_node* node, void* aux);

/*
 * A function which directs the overall algorithm with its return type.
 * `aux' is an optional parameter which will be passed to your predicate from
 * the parent function.
 */
typedef bool (*nbt_predicate_t)(const nbt_node* node, void* aux);

/*
 * Traverses the tree until a visitor says stop or all elements are exhausted.
 * Returns false if it was terminated by a visitor, true otherwise. In most
 * cases this can be ignored.
 *
 * TODO: Is there a way to do this without expensive function pointers? Maybe
 * something like the kernel's list_for_each_entry?
 */
bool nbt_map(nbt_node* tree, nbt_visitor_t, void* aux);

/*
 * Returns a new tree, consisting of a copy of all the nodes the predicate
 * returned `true' for. If the new tree is empty, this function will return
 * NULL. If an out of memory error occured, errno will be set to NBT_EMEM.
 */
nbt_node* nbt_filter(const nbt_node* tree, nbt_predicate_t, void* aux);

/*
 * The exact same as nbt_filter, except instead of returning a new tree, the
 * existing tree is modified in place, and then returned for convenience.
 */
nbt_node* nbt_filter_inplace(nbt_node* tree, nbt_predicate_t, void* aux);

/*
 * Returns the first node which causes the predicate to return true. If all
 * nodes are rejected, NULL is returned. If you want to find every instance of
 * something, consider using nbt_map with a visitor that keeps track.
 *
 * Since const-ing `tree' would me const-ing the return value, you'll just have
 * to take my word for it that nbt_find DOES NOT modify the tree.
 */
nbt_node* nbt_find(nbt_node* tree, nbt_predicate_t, void* aux);

/* Returns the number of nodes in the tree. */
size_t nbt_size(const nbt_node* tree);

/*
 * Converts a type to a print-friendly string. The string is statically
 * allocated, and therefore does not have to be freed by the user.
*/
const char* nbt_type_to_string(nbt_type);

/* TODO: More utilities as requests are made and patches contributed. */

#ifdef __cplusplus
}
#endif

#endif

