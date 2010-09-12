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

#include <zlib.h>

typedef enum NBT_Status
{
    NBT_OK   = 0,
    NBT_ERR  = -1,
    NBT_EMEM = -2,
    NBT_EGZ  = -3

} NBT_Status;

typedef enum NBT_Type
{
   TAG_End        = 0, /* No name, no payload */
   TAG_Byte       = 1, /* char, 8 bits, signed */
   TAG_Short      = 2, /* short, 16 bits, signed */
   TAG_Int        = 3, /* long, 32 bits, signed */
   TAG_Long       = 4, /* long long, 64 bits, signed */
   TAG_Float      = 5, /* float, 32 bits, signed */
   TAG_Double     = 6, /* double, 64 bits, signed */
   TAG_Byte_Array = 7, /* char *, 8 bits, unsigned, TAG_Int length */
   TAG_String     = 8, /* char *, 8 bits, signed, TAG_Short length */
   TAG_List       = 9, /* X *, X bits, TAG_Int length, no names inside */
   TAG_Compound   = 10 /* NBT_Tag * */

} NBT_Type;

typedef struct NBT_Tag
{
    NBT_Type type; /* Type of the value */
    char *name;    /* Tag name */
    
    void *value;   /* Value to be casted to the corresponding type */

} NBT_Tag;

typedef struct NBT_Byte_Array
{
    int length;
    unsigned char *content;

} NBT_Byte_Array;

typedef struct NBT_List
{
    int length;
    NBT_Type type;

    void **content;
} NBT_List;

typedef struct NBT_Compound
{
    long length;
    NBT_Tag **tags;

} NBT_Compound;

typedef struct NBT_File
{
    gzFile fp;
    NBT_Tag *root;
} NBT_File;

int NBT_Init(NBT_File **nbf);
int NBT_Free(NBT_File *nbf);
int NBT_Free_Tag(NBT_Tag *tag);
int NBT_Free_Type(NBT_Type t, void *v);

/* Freeing special tags */
int NBT_Free_List(NBT_List *l);
int NBT_Free_Byte_Array(NBT_Byte_Array *a);
int NBT_Free_Compound(NBT_Compound *c);

/* Parsing */
int NBT_Parse(NBT_File *nbt, const char *filename);
int NBT_Read_Tag(NBT_File *nbt, NBT_Tag **parent);
int NBT_Read(NBT_File *nbt, NBT_Type type, void **parent);

int NBT_Read_Byte(NBT_File *nbt, char **out);
int NBT_Read_Short(NBT_File *nbt, short **out);
int NBT_Read_Int(NBT_File *nbt, int **out);
int NBT_Read_Long(NBT_File *nbt, long **out);
int NBT_Read_Float(NBT_File *nbt, float **out);
int NBT_Read_Double(NBT_File *nbt, double **out);
int NBT_Read_Byte_Array(NBT_File *nbt, unsigned char **out);
int NBT_Read_String(NBT_File *nbt, char **out);
long NBT_Read_List(NBT_File *nbt, char *type_out, void ***target);
long NBT_Read_Compound(NBT_File *nbt, NBT_Tag ***tagslist); /* Pointer an arr */

char *NBT_Type_To_String(NBT_Type t);

void NBT_Print_Tag(NBT_Tag *t);
void NBT_Print_Value(NBT_Type t, void *val);
void NBT_Print_Byte_Array(unsigned char *ba, int len);

void NBT_Change_Value(NBT_Tag *tag, void *val, size_t size);
void NBT_Change_Name(NBT_Tag *tag, const char *newname);

void NBT_Print_Indent(int lv);

NBT_Tag *NBT_Add_Tag(
        const char *name,
        NBT_Type type,
        void *val,
        size_t size,
        NBT_Tag *parent);

NBT_Tag *NBT_Add_Tag_To_Compound(
        const char *name,
        NBT_Type type,
        void *val,
        size_t size,
        NBT_Compound *parent);

void NBT_Add_Item_To_List(void *val, size_t size, NBT_List *parent);
void NBT_Add_Byte_To_Array(char *val, NBT_Byte_Array *ba);

void NBT_Remove_Tag(NBT_Tag *target, NBT_Tag *parent);

NBT_Tag *NBT_Find_Tag_By_Name(const char *needle, NBT_Tag *haystack);

int NBT_Write(NBT_File *nbt, const char *filename); 
int NBT_Write_Tag(NBT_File *nbt, NBT_Tag *tag);
int NBT_Write_Value(NBT_File *nbt, NBT_Type t, void *val);

int NBT_Write_Byte(NBT_File *nbt, char *val);
int NBT_Write_Short(NBT_File *nbt, short *val);
int NBT_Write_Int(NBT_File *nbt, int *val);
int NBT_Write_Long(NBT_File *nbt, long *val);
int NBT_Write_Float(NBT_File *nbt, float *val);
int NBT_Write_Double(NBT_File *nbt, double *val);
int NBT_Write_String(NBT_File *nbt, char *val);
int NBT_Write_Byte_Array(NBT_File *nbt, NBT_Byte_Array *val);
int NBT_Write_List(NBT_File *nbt, NBT_List *val);
int NBT_Write_Compound(NBT_File *nbt, NBT_Compound *val);

#define DEBUG 1

/* Let's try an unwrapped "char *" first, shall we?
typedef struct NBT_String
{
    short length;
    char *content;
}*/

int indent;

#ifdef __cplusplus
}
#endif

#endif

