/*
* -----------------------------------------------------------------------------
* "THE BEER-WARE LICENSE" (Revision 42):
* <webmaster@flippeh.de> wrote this file. As long as you retain this notice you
* can do whatever you want with this stuff. If we meet some day, and you think
* this stuff is worth it, you can buy me a beer in return. Lukas Niederbremer.
* -----------------------------------------------------------------------------
*/

#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>
#include <string.h>

#include "endianness.h"
#include "nbt.h"

/* Initialization subroutine(s) */
int NBT_Init(NBT_File **nbt, const char *filename)
{
    if ((*nbt = malloc(sizeof(NBT_File))) == NULL)
        return NBT_EMEM;

    if (((*nbt)->fp = gzopen(filename, "rb")) == Z_NULL)
    {
        free(*nbt);
        return NBT_EGZ;
    }

    indent = 0;

    (*nbt)->root = NULL;

    return NBT_OK;
}

/* Parser */
int NBT_Parse(NBT_File *nbt)
{
    nbt->root = malloc(sizeof(NBT_Tag));
    if (nbt->root == NULL)
        return NBT_EMEM;

    NBT_Read_Tag(nbt, &(nbt->root));

    return NBT_OK;
}

int NBT_Read_Tag(NBT_File *nbt, NBT_Tag **parent)
{
    NBT_Type type = 0;

    /* Read the type */
    gzread(nbt->fp, &type, 1);

    (*parent)->type = type;
    (*parent)->name = NULL;
    (*parent)->value = NULL;

    if (type != TAG_End) /* TAG_End has no name */
        NBT_Read_String(nbt, &((*parent)->name));

    NBT_Read(nbt, type, &((*parent)->value));

    return type;
}

int NBT_Read(NBT_File *nbt, NBT_Type type, void **parent)
{
    switch (type)
    {
        case TAG_End:
            break; 

        case TAG_Byte:
            NBT_Read_Byte(nbt, (char **)parent);

            break;

        case TAG_Short:
            NBT_Read_Short(nbt, (short **)parent);

            break;

        case TAG_Int:
            NBT_Read_Int(nbt, (int **)parent);

            break;

        case TAG_Long:
            NBT_Read_Long(nbt, (long **)parent);

            break;

        case TAG_Float:
            NBT_Read_Float(nbt, (float **)parent);

            break;

        case TAG_Double:
            NBT_Read_Double(nbt, (double **)parent);

            break;

        case TAG_String:
            ;; /* To make it shut up about the variable declaration */
            char *string = NULL;
            
            NBT_Read_String(nbt, &string);
            *parent = string;

            break;

        case TAG_Byte_Array:
            ;; /* ... */
            unsigned char *bytestring;
            int len = NBT_Read_Byte_Array(nbt, &bytestring);
            
            NBT_Byte_Array *t = malloc(sizeof(NBT_Byte_Array));
            t->length = len;
            t->content = bytestring;

            *parent = t;

            break;

        case TAG_List:
            ;; 
            char type;
            void **target;
            long length = NBT_Read_List(nbt, &type, &target);

            NBT_List *l = malloc(sizeof(NBT_List));
            l->length = length;
            l->type = type;
            l->content = target;

            *parent = l;

            break;

        case TAG_Compound:
            ;;
            NBT_Compound *c = malloc(sizeof(NBT_Compound));
            NBT_Tag **tags = NULL;

            long lc = NBT_Read_Compound(nbt, &tags);

            c->tags = tags;
            c->length = lc;

            *parent = c;
    }
    
    return type; /* Use to abort looping in TAG_Read_Compound on TAG_End */
}

int NBT_Read_Byte(NBT_File *nbt, char **out)
{
    char t;

    gzread(nbt->fp, &t, sizeof(t));

    *out = malloc(sizeof(char));
    memcpy(*out, &t, sizeof(char));

    return 0;
}

int NBT_Read_Short(NBT_File *nbt, short **out)
{
    short t;

    gzread(nbt->fp, &t, sizeof(t));
    if (get_endianness() == L_ENDIAN)
        swaps((unsigned short *)&t);

    *out = malloc(sizeof(short));
    memcpy(*out, &t, sizeof(short));

    
    return 0;
}

int NBT_Read_Int(NBT_File *nbt, int **out)
{
    int t;

    gzread(nbt->fp, &t, sizeof(t));
    if (get_endianness() == L_ENDIAN)
        swapi((unsigned int *)&t);
    
    *out = malloc(sizeof(int));
    memcpy(*out, &t, sizeof(int));

    return 0;
}

int NBT_Read_Long(NBT_File *nbt, long **out)
{
    long t;

    gzread(nbt->fp, &t, sizeof(t));
    if (get_endianness() == L_ENDIAN)
        swapl((unsigned long *)&t);

    *out = malloc(sizeof(long));
    memcpy(*out, &t, sizeof(long));

    return 0;
}

int NBT_Read_Float(NBT_File *nbt, float **out)
{
    float t;

    gzread(nbt->fp, &t, sizeof(t));
    if (get_endianness() == L_ENDIAN)
        t = swapf(t);

    *out = malloc(sizeof(float));
    memcpy(*out, &t, sizeof(float));

    return 0;
}

int NBT_Read_Double(NBT_File *nbt, double **out)
{
    double t;

    gzread(nbt->fp, &t, sizeof(t));
    if (get_endianness() == L_ENDIAN)
        t = swapd(t);

    *out = malloc(sizeof(double));
    memcpy(*out, &t, sizeof(double));

    return 0;
}

int NBT_Read_Byte_Array(NBT_File *nbt, unsigned char **out)
{
    int len;

    gzread(nbt->fp, &len, sizeof(len));
    if (get_endianness() == L_ENDIAN)
        swapi((unsigned int *)&len);

    *out = malloc(len);
    gzread(nbt->fp, *out, len);
    
    return len;
}

int NBT_Read_String(NBT_File *nbt, char **out)
{
    short len;

    gzread(nbt->fp, &len, sizeof(len));
    if (get_endianness() == L_ENDIAN)
        swaps((unsigned short *)&len);

    *out = malloc(len + 1);
    memset(*out, 0, len + 1);
    gzread(nbt->fp, *out, len);

    return len;
}

long NBT_Read_List(NBT_File *nbt, char *type_out, void ***target)
{
    char type;
    int len;
    int i;

    gzread(nbt->fp, &type, 1);
    *type_out = type;

    gzread(nbt->fp, &len, sizeof(len));

    if (get_endianness() == L_ENDIAN)
        swapi((unsigned int *)&len);


    *target = malloc(len * sizeof(void *));

    for (i = 0; i < len; ++i)
        NBT_Read(nbt, type, &((*target)[i]));

    return len;
}

long NBT_Read_Compound(NBT_File *nbt, NBT_Tag ***listptr)
{
    long i;

    *listptr = malloc(sizeof(NBT_Tag *)); 

    for (i = 0;; ++i)
    {
        (*listptr)[i] = malloc(sizeof(NBT_Tag));
        NBT_Type last = NBT_Read_Tag(nbt, &((*listptr)[i]));

        *listptr = realloc(*listptr, sizeof(NBT_Tag *) * (i+2));

        if (last == TAG_End)
        {
            //(*listptr)[++i] = NULL;
            free((*listptr)[i]); /* This is an ugly, UGLY hack, let's remove 
                                    this ASAP! */

            break;
        }
    }

    return i;
}

/* Cleanup subroutines */

int NBT_Free(NBT_File *nbt)
{
    if (nbt->root != NULL)
        NBT_Free_Tag(nbt->root);

    gzclose(nbt->fp);
    free(nbt);

    return NBT_OK;
}

int NBT_Free_Tag(NBT_Tag *t)
{
    free(t->name);
    NBT_Free_Type(t->type, t->value);
    free(t);

    return 0;
}

int NBT_Free_Type(NBT_Type type, void *payload)
{
    switch (type)
    {
        case TAG_Byte:
        case TAG_Short:
        case TAG_Int:
        case TAG_Long:
        case TAG_Float:
        case TAG_Double:
        case TAG_String:
            free(payload);
            break;
        case TAG_List:
            NBT_Free_List(payload);
            break;
        case TAG_Byte_Array:
            NBT_Free_Byte_Array(payload);
            break;
        case TAG_Compound:
            NBT_Free_Compound(payload);
            break;
        case TAG_End: /* Why the hell? */
            return 1;
    }

    return 0;
}

int NBT_Free_List(NBT_List *l)
{
    int i;

    for (i = 0; i < l->length; ++i)
        NBT_Free_Type(l->type, l->content[i]);

    free(l->content);
    free(l);

    return 0;
}

int NBT_Free_Byte_Array(NBT_Byte_Array *a)
{
    free(a->content);
    free(a);
    
    return 0;
}

int NBT_Free_Compound(NBT_Compound *c)
{
    int i;

    for (i = 0; i < c->length; ++i)
    {
        free(c->tags[i]->name);
        NBT_Free_Type(c->tags[i]->type, c->tags[i]->value);
        free(c->tags[i]);
    }
 
    free(c->tags);
    free(c);

    return 0;
}

char *NBT_Type_To_String(NBT_Type t)
{
    static char *str;

    switch (t)
    {
        case TAG_End:
            str = "TAG_End";
            break;

        case TAG_Byte:
            str = "TAG_Byte";
            break;

        case TAG_Short:
            str = "TAG_Short";
            break;

        case TAG_Int:
            str = "TAG_Int";
            break;

        case TAG_Long:
            str = "TAG_Long";
            break;

        case TAG_Float:
            str = "TAG_Float";
            break;

        case TAG_Double:
            str = "TAG_Double";
            break;

        case TAG_Byte_Array:
            str = "TAG_Byte_Array";
            break;

        case TAG_String:
            str = "TAG_String";
            break;

        case TAG_List:
            str = "TAG_List";
            break;

        case TAG_Compound:
            str = "TAG_Compound";
            break;

        default:
            str = "TAG_Unknown";
            break;
    }

    return str;
}

void NBT_Print_Tag(NBT_Tag *t)
{
    if (t->type == TAG_End)
        return;

    NBT_Print_Indent(indent);
    printf("%s(\"%s\"): ",
            NBT_Type_To_String(t->type),
            t->name);

    NBT_Print_Value(t->type, t->value);
}

void NBT_Print_Indent(int lv)
{
    int i = 0;

    for (i = 0; i < lv; ++i)
        printf("   ");

    return;
}
    
void NBT_Print_Value(NBT_Type t, void *v)
{
    int i;

    //printf("%s", indentation);

    switch (t)
    {
        case TAG_Byte:
            printf("0x%02X (%d)", *((char *)v), *((char *)v));
            break;

        case TAG_Short:
            printf("%d", *((short *)v));
            break;

        case TAG_Int:
            printf("%d", *((int *)v));
            break;

        case TAG_Long:
            printf("%ld", *((long *)v));
            break;

        case TAG_Float:
            printf("%f", *((float *)v));
            break;

        case TAG_Double:
            printf("%f", *((double *)v));
            break;

        case TAG_String:
            printf("\"%s\"", (char *)v);
            break;

        case TAG_Byte_Array:
            ;;

            NBT_Byte_Array *arr = (NBT_Byte_Array *)v;
            NBT_Print_Byte_Array(arr->content, arr->length);
            break;

        case TAG_Compound:
            ;;
            NBT_Compound *c = (NBT_Compound *)v;
            
            printf("(%ld entries) { \n", c->length);
            indent++;

            for (i = 0; i < c->length; ++i)
                NBT_Print_Tag(c->tags[i]);

            NBT_Print_Indent(--indent);
            printf("}\n");

            break;

        case TAG_List:
            ;;
            NBT_List *l = (NBT_List *)v;

            printf("(%d entries) { \n", l->length);
            indent++;

            for (i = 0; i < l->length; ++i)
            {
                NBT_Print_Indent(indent);

                printf("%s: ", NBT_Type_To_String(l->type));
                void **content = l->content;
                NBT_Print_Value(l->type, content[i]);

            }

            NBT_Print_Indent(--indent);
            printf("}\n");

            break;

        default:
            printf("<not implemented: 0x%02X>", t);
    }

    printf("\n");

    return;
}

void NBT_Print_Byte_Array(unsigned char *ba, int len)
{
    int i;

    printf("(%d entries) [", len);
    for (i = 0; i < len; ++i)
    {
        printf("%02X", ba[i]);

        if (i == (len - 1))
            printf(" ");
        else
            printf(", ");
    }

    printf("]");

    return;
}

void NBT_Change_Value(NBT_Tag *tag, void *val, size_t size)
{
    NBT_Free_Type(tag->type, tag->value);

    void *t = malloc(size);
    memcpy(t, val, size);

    tag->value = t;

    return;
}

void NBT_Change_Name(NBT_Tag *tag, const char *newname)
{
    char *tmp = malloc(strlen(newname) + 1);
    if (tmp != NULL)
    {
        strcpy(tmp, newname);

        free(tag->name);
        tag->name = tmp;
    }

    return;
}

void NBT_Add_Tag(const char *name, 
                 NBT_Type type,
                 void *val,
                 size_t size,
                 NBT_Tag *parent)
{
    if (parent->type == TAG_Compound)
    {
        NBT_Compound *c = (NBT_Compound *)parent->value;

        NBT_Add_Tag_To_Compound(name, type, val, size, c);
    }
    else if (parent->type == TAG_List)
    {
        NBT_List *l = (NBT_List *)parent->value;

        if (l->type == type)
            NBT_Add_Item_To_List(val, size, l);
    }
    else if ((parent->type == TAG_Byte_Array) && (type == TAG_Byte))
    {
        NBT_Byte_Array *ba = (NBT_Byte_Array *)parent->value;

        NBT_Add_Byte_To_Array(val, ba);
    }
    else
        return;
}

void NBT_Add_Tag_To_Compound(const char *name,
                            NBT_Type type,
                            void *val,
                            size_t size,
                            NBT_Compound *parent)
{
    NBT_Tag **tags_temp = NULL;
    tags_temp = realloc(parent->tags, 
                        sizeof(NBT_Tag *) * (parent->length + 1));

    if (tags_temp != NULL)
    {
        NBT_Tag *temp = malloc(sizeof(NBT_Tag));
        if (temp != NULL)
        {
            parent->tags = tags_temp;
            parent->length++;
            printf("New length: %ld\n", parent->length);

            temp->name = malloc(strlen(name) + 1);
            strcpy(temp->name, name);

            temp->type = type;

            temp->value = malloc(size);
            memcpy(temp->value, val, size);

            parent->tags[parent->length - 1] = temp;
            //parent->tags[parent->length - 1] = NULL;
        }
    }

    return;
}

void NBT_Add_Item_To_List(void *val, size_t size, NBT_List *parent)
{
    void **temp = realloc(parent->content, sizeof(void *) * (parent->length + 1));
    if (temp != NULL)
    {
        void *new = malloc(size);
        if (new != NULL)
        {
            parent->content = temp;
            parent->length++;

            memcpy(new, val, size);
            parent->content[parent->length - 1] = new;
        }
        else
            free(temp);
    }

    return;
}

void NBT_Add_Byte_To_Array(char *val, NBT_Byte_Array *parent)
{
    unsigned char *temp = realloc(parent->content, (parent->length + 1));
    if (temp != NULL)
    {
        parent->content = temp;
        parent->length++;
        parent->content[parent->length - 1] = *val;
    }

    return;
}
