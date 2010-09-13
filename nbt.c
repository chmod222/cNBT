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
int nbt_init(nbt_file **nbt)
{
    if ((*nbt = malloc(sizeof(nbt_file))) == NULL)
        return NBT_EMEM;

    indent = 0;

    (*nbt)->root = NULL;

    return NBT_OK;
}

/* Parser */
int nbt_parse(nbt_file *nbt, const char *filename)
{
    if ((nbt->fp = gzopen(filename, "rb")) == Z_NULL)
        return NBT_EGZ;

    nbt->root = malloc(sizeof(nbt_tag));
    if (nbt->root == NULL)
        return NBT_EMEM;

    nbt_read_tag(nbt, &(nbt->root));

    gzclose(nbt->fp);

    return NBT_OK;
}

int nbt_read_tag(nbt_file *nbt, nbt_tag **parent)
{
    nbt_type type = 0;

    /* Read the type */
    gzread(nbt->fp, &type, 1);

    (*parent)->type = type;
    (*parent)->name = NULL;
    (*parent)->value = NULL;

    if (type != TAG_END) /* TAG_END has no name */
        nbt_read_string(nbt, &((*parent)->name));

    nbt_read(nbt, type, &((*parent)->value));

    return type;
}

int nbt_read(nbt_file *nbt, nbt_type type, void **parent)
{
    switch (type)
    {
        case TAG_END:
            break; 

        case TAG_BYTE:
            nbt_read_byte(nbt, (char **)parent);

            break;

        case TAG_SHORT:
            nbt_read_short(nbt, (short **)parent);

            break;

        case TAG_INT:
            nbt_read_int(nbt, (int **)parent);

            break;

        case TAG_LONG:
            nbt_read_long(nbt, (long **)parent);

            break;

        case TAG_FLOAT:
            nbt_read_float(nbt, (float **)parent);

            break;

        case TAG_DOUBLE:
            nbt_read_double(nbt, (double **)parent);

            break;

        case TAG_STRING:
            ;; /* to make it shut up about the variable declaration */
            char *string = NULL;
            
            nbt_read_string(nbt, &string);
            *parent = string;

            break;

        case TAG_BYTE_ARRAY:
            ;; /* ... */
            unsigned char *bytestring;
            int len = nbt_read_byte_array(nbt, &bytestring);
            
            nbt_byte_array *t = malloc(sizeof(nbt_byte_array));
            t->length = len;
            t->content = bytestring;

            *parent = t;

            break;

        case TAG_LIST:
            ;; 
            char type;
            void **target;
            long length = nbt_read_list(nbt, &type, &target);

            nbt_list *l = malloc(sizeof(nbt_list));
            l->length = length;
            l->type = type;
            l->content = target;

            *parent = l;

            break;

        case TAG_COMPOUND:
            ;;
            nbt_compound *c = malloc(sizeof(nbt_compound));
            nbt_tag **tags = NULL;

            long lc = nbt_read_compound(nbt, &tags);

            c->tags = tags;
            c->length = lc;

            *parent = c;
    }
    
    return type; /* Use to abort looping in TAG_Read_compound on TAG_END */
}

int nbt_read_byte(nbt_file *nbt, char **out)
{
    char t;

    gzread(nbt->fp, &t, sizeof(t));

    *out = malloc(sizeof(char));
    memcpy(*out, &t, sizeof(char));

    return 0;
}

int nbt_read_short(nbt_file *nbt, short **out)
{
    short t;

    gzread(nbt->fp, &t, sizeof(t));
    if (get_endianness() == L_ENDIAN)
        swaps((unsigned short *)&t);

    *out = malloc(sizeof(short));
    memcpy(*out, &t, sizeof(short));

    
    return 0;
}

int nbt_read_int(nbt_file *nbt, int **out)
{
    int t;

    gzread(nbt->fp, &t, sizeof(t));
    if (get_endianness() == L_ENDIAN)
        swapi((unsigned int *)&t);
    
    *out = malloc(sizeof(int));
    memcpy(*out, &t, sizeof(int));

    return 0;
}

int nbt_read_long(nbt_file *nbt, long **out)
{
    long t;

    gzread(nbt->fp, &t, sizeof(t));
    if (get_endianness() == L_ENDIAN)
        swapl((unsigned long *)&t);

    *out = malloc(sizeof(long));
    memcpy(*out, &t, sizeof(long));

    return 0;
}

int nbt_read_float(nbt_file *nbt, float **out)
{
    float t;

    gzread(nbt->fp, &t, sizeof(t));
    if (get_endianness() == L_ENDIAN)
        t = swapf(t);

    *out = malloc(sizeof(float));
    memcpy(*out, &t, sizeof(float));

    return 0;
}

int nbt_read_double(nbt_file *nbt, double **out)
{
    double t;

    gzread(nbt->fp, &t, sizeof(t));
    if (get_endianness() == L_ENDIAN)
        t = swapd(t);

    *out = malloc(sizeof(double));
    memcpy(*out, &t, sizeof(double));

    return 0;
}

int nbt_read_byte_array(nbt_file *nbt, unsigned char **out)
{
    int len;

    gzread(nbt->fp, &len, sizeof(len));
    if (get_endianness() == L_ENDIAN)
        swapi((unsigned int *)&len);

    *out = malloc(len);
    gzread(nbt->fp, *out, len);
    
    return len;
}

int nbt_read_string(nbt_file *nbt, char **out)
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

long nbt_read_list(nbt_file *nbt, char *type_out, void ***target)
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
        nbt_read(nbt, type, &((*target)[i]));

    return len;
}

long nbt_read_compound(nbt_file *nbt, nbt_tag ***listptr)
{
    long i;

    *listptr = malloc(sizeof(nbt_tag *)); 

    for (i = 0;; ++i)
    {
        (*listptr)[i] = malloc(sizeof(nbt_tag));
        nbt_type last = nbt_read_tag(nbt, &((*listptr)[i]));

        *listptr = realloc(*listptr, sizeof(nbt_tag *) * (i+2));

        if (last == TAG_END)
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

int nbt_free(nbt_file *nbt)
{
    if (nbt->root != NULL)
        nbt_free_tag(nbt->root);

    free(nbt);

    return NBT_OK;
}

int nbt_free_tag(nbt_tag *t)
{
    free(t->name);
    nbt_free_type(t->type, t->value);
    free(t);

    return 0;
}

int nbt_free_type(nbt_type type, void *payload)
{
    switch (type)
    {
        case TAG_BYTE:
        case TAG_SHORT:
        case TAG_INT:
        case TAG_LONG:
        case TAG_FLOAT:
        case TAG_DOUBLE:
        case TAG_STRING:
            free(payload);
            break;
        case TAG_LIST:
            nbt_free_list(payload);
            break;
        case TAG_BYTE_ARRAY:
            nbt_free_byte_array(payload);
            break;
        case TAG_COMPOUND:
            nbt_free_compound(payload);
            break;
        case TAG_END: /* Why the hell? */
            return 1;
    }

    return 0;
}

int nbt_free_list(nbt_list *l)
{
    int i;

    for (i = 0; i < l->length; ++i)
        nbt_free_type(l->type, l->content[i]);

    free(l->content);
    free(l);

    return 0;
}

int nbt_free_byte_array(nbt_byte_array *a)
{
    free(a->content);
    free(a);
    
    return 0;
}

int nbt_free_compound(nbt_compound *c)
{
    int i;

    for (i = 0; i < c->length; ++i)
    {
        free(c->tags[i]->name);
        nbt_free_type(c->tags[i]->type, c->tags[i]->value);
        free(c->tags[i]);
    }
 
    free(c->tags);
    free(c);

    return 0;
}

char *nbt_type_to_string(nbt_type t)
{
    static char *str;

    switch (t)
    {
        case TAG_END:
            str = "TAG_END";
            break;

        case TAG_BYTE:
            str = "TAG_BYTE";
            break;

        case TAG_SHORT:
            str = "TAG_SHORT";
            break;

        case TAG_INT:
            str = "TAG_INT";
            break;

        case TAG_LONG:
            str = "TAG_LONG";
            break;

        case TAG_FLOAT:
            str = "TAG_FLOAT";
            break;

        case TAG_DOUBLE:
            str = "TAG_DOUBLE";
            break;

        case TAG_BYTE_ARRAY:
            str = "TAG_BYTE_ARRAY";
            break;

        case TAG_STRING:
            str = "TAG_STRING";
            break;

        case TAG_LIST:
            str = "TAG_LIST";
            break;

        case TAG_COMPOUND:
            str = "TAG_COMPOUND";
            break;

        default:
            str = "TAG_Unknown";
            break;
    }

    return str;
}

void nbt_print_tag(nbt_tag *t)
{
    if (t->type == TAG_END)
        return;

    nbt_print_indent(indent);
    printf("%s(\"%s\"): ",
            nbt_type_to_string(t->type),
            t->name);

    nbt_print_value(t->type, t->value);
}

void nbt_print_indent(int lv)
{
    int i = 0;

    for (i = 0; i < lv; ++i)
        printf("   ");

    return;
}
    
void nbt_print_value(nbt_type t, void *v)
{
    int i;

    //printf("%s", indentation);

    switch (t)
    {
        case TAG_BYTE:
            printf("0x%02X (%d)", *((char *)v), *((char *)v));
            break;

        case TAG_SHORT:
            printf("%d", *((short *)v));
            break;

        case TAG_INT:
            printf("%d", *((int *)v));
            break;

        case TAG_LONG:
            printf("%ld", *((long *)v));
            break;

        case TAG_FLOAT:
            printf("%f", *((float *)v));
            break;

        case TAG_DOUBLE:
            printf("%f", *((double *)v));
            break;

        case TAG_STRING:
            printf("\"%s\"", (char *)v);
            break;

        case TAG_BYTE_ARRAY:
            ;;

            nbt_byte_array *arr = (nbt_byte_array *)v;
            nbt_print_byte_array(arr->content, arr->length);
            break;

        case TAG_COMPOUND:
            ;;
            nbt_compound *c = (nbt_compound *)v;
            
            printf("(%ld entries) { \n", c->length);
            indent++;

            for (i = 0; i < c->length; ++i)
                nbt_print_tag(c->tags[i]);

            nbt_print_indent(--indent);
            printf("}\n");

            break;

        case TAG_LIST:
            ;;
            nbt_list *l = (nbt_list *)v;

            printf("(%d entries) { \n", l->length);
            indent++;

            for (i = 0; i < l->length; ++i)
            {
                nbt_print_indent(indent);

                printf("%s: ", nbt_type_to_string(l->type));
                void **content = l->content;
                nbt_print_value(l->type, content[i]);

            }

            nbt_print_indent(--indent);
            printf("}\n");

            break;

        default:
            printf("<not implemented: 0x%02X>", t);
    }

    printf("\n");

    return;
}

void nbt_print_byte_array(unsigned char *ba, int len)
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

void nbt_change_value(nbt_tag *tag, void *val, size_t size)
{
    nbt_free_type(tag->type, tag->value);

    void *t = malloc(size);
    memcpy(t, val, size);

    tag->value = t;

    return;
}

void nbt_change_name(nbt_tag *tag, const char *newname)
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

nbt_tag *nbt_add_Tag(const char *name, 
                 nbt_type type,
                 void *val,
                 size_t size,
                 nbt_tag *parent)
{
    nbt_tag *res;

    if (parent->type == TAG_COMPOUND)
    {
        nbt_compound *c = (nbt_compound *)parent->value;

        res = nbt_add_tag_to_compound(name, type, val, size, c);
    }
    else if (parent->type == TAG_LIST)
    {
        nbt_list *l = (nbt_list *)parent->value;

        if (l->type == type)
            nbt_add_item_to_list(val, size, l);

        res = NULL;
    }
    else if ((parent->type == TAG_BYTE_ARRAY) && (type == TAG_BYTE))
    {
        nbt_byte_array *ba = (nbt_byte_array *)parent->value;

        nbt_add_byte_to_array(val, ba);

        res = NULL;
    }
    else
        return NULL;

    return res;
}

nbt_tag *nbt_add_tag_to_compound(const char *name,
                            nbt_type type,
                            void *val,
                            size_t size,
                            nbt_compound *parent)
{
    nbt_tag **tags_temp = NULL;
    tags_temp = realloc(parent->tags, 
                        sizeof(nbt_tag *) * (parent->length + 1));

    if (tags_temp != NULL)
    {
        nbt_tag *temp = malloc(sizeof(nbt_tag));
        if (temp != NULL)
        {
            parent->tags = tags_temp;
            parent->length++;

            temp->name = malloc(strlen(name) + 1);
            strcpy(temp->name, name);

            temp->type = type;

            temp->value = malloc(size);
            memcpy(temp->value, val, size);

            parent->tags[parent->length - 1] = temp;

            return temp;
        }
    }

    return NULL;
}

void nbt_add_item_to_list(void *val, size_t size, nbt_list *parent)
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

void nbt_add_byte_to_array(char *val, nbt_byte_array *parent)
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

void nbt_remove_tag(nbt_tag *target, nbt_tag *parent)
{
    int i;
    int count = 0;
    nbt_tag **templist = NULL;
    nbt_compound *tmp = (nbt_compound *)parent->value;

    if (parent->type != TAG_COMPOUND)
        return;

    templist = malloc(sizeof(nbt_tag *));

    for (i = 0; i < tmp->length; ++i)
    {
        if (tmp->tags[i] != target)
        {
            templist[count] = tmp->tags[i];
            templist = realloc(templist, sizeof(nbt_tag *) * (count+2));

            ++count;
        }
        else
        {
            nbt_free_tag(tmp->tags[i]);
        }
    }

    free(tmp->tags);
    tmp->tags = templist;
    tmp->length = count;

    return;
}

nbt_tag *nbt_find_tag_by_name(const char *needle, nbt_tag *haystack)
{
    if (haystack->type == TAG_COMPOUND)
    {
        nbt_compound *c = (nbt_compound *)haystack->value;
        int i;

        for (i = 0; i < c->length; ++i)
            if (strcmp(c->tags[i]->name, needle) == 0)
                return c->tags[i];
    }

    return NULL;
}

int nbt_write(nbt_file *nbt, const char *filename)
{
    if ((nbt->fp = gzopen(filename, "wb")) == Z_NULL)
        return NBT_EGZ;

    if (nbt->root != NULL)
    {
        int size = nbt_write_tag(nbt, nbt->root);

        gzclose(nbt->fp);

        return size;
    }
   
    return NBT_ERR;
}

int nbt_write_tag(nbt_file *nbt, nbt_tag *tag)
{
    int size = 0;

    size += gzwrite(nbt->fp, &(tag->type), sizeof(char));

    if (tag->type != TAG_END)
    {
        /* Every tag but TAG_END has a name */
        size += nbt_write_string(nbt, tag->name);
        size += nbt_write_value(nbt, tag->type, tag->value);
    }

    return size;
}

int nbt_write_value(nbt_file *nbt, nbt_type t, void *value)
{
    int written = 0;

    switch (t)
    {
        case TAG_END: /* WHY is this even in? */
            break;

        case TAG_BYTE:
            written = nbt_write_byte(nbt, (char *)value);

            break;

        case TAG_SHORT:
            written = nbt_write_short(nbt, (short *)value);

            break;

        case TAG_INT:
            written = nbt_write_int(nbt, (int *)value);

            break;

        case TAG_LONG:
            written = nbt_write_long(nbt, (long *)value);

            break;

        case TAG_FLOAT:
            written = nbt_write_float(nbt, (float *)value);

            break;

        case TAG_DOUBLE:
            written = nbt_write_double(nbt, (double *)value);

            break;

        case TAG_STRING:
            written = nbt_write_string(nbt, (char *)value);

            break;

        case TAG_BYTE_ARRAY:
            written = nbt_write_byte_array(nbt, (nbt_byte_array *)value);

            break;

        case TAG_LIST:
            written = nbt_write_list(nbt, (nbt_list *)value);

            break;

        case TAG_COMPOUND:
            written = nbt_write_compound(nbt, (nbt_compound *)value);

            break;

        default:
            /* Maybe moan about a very unknown tag? Not yet... */
            break;

    }

    return written;
}

int nbt_write_byte(nbt_file *nbt, char *val)
{
    /* bytes, simple enough */
    return gzwrite(nbt->fp, val, sizeof(char));
}

int nbt_write_short(nbt_file *nbt, short *val)
{
    short temp = *val;

    /* Needs swapping first? */
    if (get_endianness() == L_ENDIAN)
        swaps((unsigned short *)&temp);

    return gzwrite(nbt->fp, &temp, sizeof(short));
}

int nbt_write_int(nbt_file *nbt, int *val)
{
    int temp = *val;

    if (get_endianness() == L_ENDIAN)
        swapi((unsigned int *)&temp);

    return gzwrite(nbt->fp, &temp, sizeof(int));
}

int nbt_write_long(nbt_file *nbt, long *val)
{
    long temp = *val;

    if (get_endianness() == L_ENDIAN)
        swapl((unsigned long *)&temp);

    return gzwrite(nbt->fp, &temp, sizeof(long));
}

int nbt_write_float(nbt_file *nbt, float *val)
{
    float temp = *val;

    if (get_endianness() == L_ENDIAN)
        temp = swapf(temp);

    return gzwrite(nbt->fp, &temp, sizeof(float));
}

int nbt_write_double(nbt_file *nbt, double *val)
{
    double temp = *val;

    if (get_endianness() == L_ENDIAN)
        temp = swapd(temp);

    return gzwrite(nbt->fp, &temp, sizeof(double));
}

int nbt_write_string(nbt_file *nbt, char *val)
{
    int size = 0;
    short len = strlen(val);

    /* Write length first */
    size += nbt_write_short(nbt, &len);

    /* Write content */
    size += gzwrite(nbt->fp, val, len);

    return size;
}

int nbt_write_byte_array(nbt_file *nbt, nbt_byte_array *val)
{
    int size = 0;
    
    /* Length first again, then content */
    size += nbt_write_int(nbt, &(val->length));
    size += gzwrite(nbt->fp, val->content, val->length);

    return size;
}

int nbt_write_list(nbt_file *nbt, nbt_list *val)
{
    int i;
    int size = 0;

    /* Write type id first */
    size += nbt_write_byte(nbt, (char *)&(val->type));
    size += nbt_write_int(nbt, &(val->length));

    for (i = 0; i < val->length; ++i)
        size += nbt_write_value(nbt, val->type, val->content[i]);

    return size;    
}

int nbt_write_compound(nbt_file *nbt, nbt_compound *val)
{
    int endtag = 0;
    int i;
    int size = 0;

    for (i = 0; i < val->length; ++i)
        size += nbt_write_tag(nbt, val->tags[i]);

    size += gzwrite(nbt->fp, &endtag, sizeof(char));

    return size;
}
