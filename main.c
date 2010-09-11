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
#include <string.h>

#include "endianness.h"
#include "nbt.h"

int main(int argc, char **argv)
{
    NBT_File *nbt = NULL;
    NBT_Tag *t;

    /* Enough parameters given? */
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <file>\n",
                argv[0]);

        return EXIT_FAILURE;
    }

    if (NBT_Init(&nbt, argv[1]) != NBT_OK)
    {
        fprintf(stderr, "NBT_Init(): Failure initializing\n");
        return EXIT_FAILURE;
    }

    /* Try parsing */
    NBT_Parse(nbt);
    NBT_Print_Tag(nbt->root);

    t = nbt->root;

    /*if (t->type == TAG_Compound)
    {
        NBT_Compound *compound = (NBT_Compound *)(t->value);
        int i;

        for (i = 0; i < compound->length; ++i)
        {
           if (compound->tags[i]->type == TAG_Byte_Array)
           {
               printf("Adding...\n");
               char byte = 0xFF;

               NBT_Add_Tag(NULL, TAG_Byte, &byte, sizeof(byte), compound->tags[i]);
           }
        }
    }*/

    //NBT_Add_Tag("Failure By Design", TAG_String, "I BE FIRIN MAH LAZOR - BAHHHH!", 50, t);

    //NBT_Print_Tag(nbt->root);
    
    NBT_Free(nbt);

    return 0;
}
