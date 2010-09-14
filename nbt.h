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

#include <stdint.h>
#include <zlib.h>

typedef enum nbt_status
{
    NBT_OK   = 0,
    NBT_ERR  = -1,
    NBT_EMEM = -2,
    NBT_EGZ  = -3

} nbt_status;

typedef enum nbt_type
{
   TAG_END        = 0, /* No name, no payload */
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

typedef struct nbt_tag
{
    nbt_type type; /* Type of the value */
    char *name;    /* tag name */
    
    void *value;   /* value to be casted to the corresponding type */

} nbt_tag;

typedef struct nbt_byte_array
{
    int32_t length;
    unsigned char *content;

} nbt_byte_array;

typedef struct nbt_list
{
    int32_t length;
    nbt_type type;

    void **content;
} nbt_list;

typedef struct nbt_compound
{
    int32_t length;
    nbt_tag **tags;

} nbt_compound;

typedef struct nbt_file
{
    gzFile fp;
    nbt_tag *root;
} nbt_file;

int nbt_init(nbt_file **nbf);
int nbt_free(nbt_file *nbf);
int nbt_free_tag(nbt_tag *tag);
int nbt_free_type(nbt_type t, void *v);

/* Freeing special tags */
int nbt_free_list(nbt_list *l);
int nbt_free_byte_array(nbt_byte_array *a);
int nbt_free_compound(nbt_compound *c);

/* Parsing */
int nbt_parse(nbt_file *nbt, const char *filename);
int nbt_read_tag(nbt_file *nbt, nbt_tag **parent);
int nbt_read(nbt_file *nbt, nbt_type type, void **parent);

int nbt_read_byte(nbt_file *nbt, char **out);
int nbt_read_short(nbt_file *nbt, int16_t **out);
int nbt_read_int(nbt_file *nbt, int32_t **out);
int nbt_read_long(nbt_file *nbt, int64_t **out);
int nbt_read_float(nbt_file *nbt, float **out);
int nbt_read_double(nbt_file *nbt, double **out);
int nbt_read_byte_array(nbt_file *nbt, unsigned char **out);
int nbt_read_string(nbt_file *nbt, char **out);
int32_t nbt_read_list(nbt_file *nbt, char *type_out, void ***target);
int32_t nbt_read_compound(nbt_file *nbt, nbt_tag ***tagslist); /* Pointer an arr */

char *nbt_type_to_string(nbt_type t);

void nbt_print_tag(nbt_tag *t);
void nbt_print_value(nbt_type t, void *val);
void nbt_print_byte_array(unsigned char *ba, int32_t len);

void nbt_change_value(nbt_tag *tag, void *val, size_t size);
void nbt_change_name(nbt_tag *tag, const char *newname);

void nbt_print_indent(int lv);

nbt_tag *nbt_add_tag(
        const char *name,
        nbt_type type,
        void *val,
        size_t size,
        nbt_tag *parent);

nbt_tag *nbt_add_tag_to_compound(
        const char *name,
        nbt_type type,
        void *val,
        size_t size,
        nbt_compound *parent);

void nbt_add_item_to_list(void *val, size_t size, nbt_list *parent);
void nbt_add_byte_to_array(char *val, nbt_byte_array *ba);

void nbt_remove_tag(nbt_tag *target, nbt_tag *parent);

nbt_tag *nbt_find_tag_by_name(const char *needle, nbt_tag *haystack);

int nbt_write(nbt_file *nbt, const char *filename); 
int nbt_write_tag(nbt_file *nbt, nbt_tag *tag);
int nbt_write_value(nbt_file *nbt, nbt_type t, void *val);

int nbt_write_byte(nbt_file *nbt, char *val);
int nbt_write_short(nbt_file *nbt, int16_t *val);
int nbt_write_int(nbt_file *nbt, int32_t *val);
int nbt_write_long(nbt_file *nbt, int64_t *val);
int nbt_write_float(nbt_file *nbt, float *val);
int nbt_write_double(nbt_file *nbt, double *val);
int nbt_write_string(nbt_file *nbt, char *val);
int nbt_write_byte_array(nbt_file *nbt, nbt_byte_array *val);
int nbt_write_list(nbt_file *nbt, nbt_list *val);
int nbt_write_compound(nbt_file *nbt, nbt_compound *val);

#define DEBUG 1

int indent;

#ifdef __cplusplus
}
#endif

#endif

