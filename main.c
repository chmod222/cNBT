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

#include <getopt.h>

#include "endianness.h"
#include "nbt.h"

void dump_nbt(char *filename);

int main(int argc, char **argv)
{
    int c;
    static int opt_dump = 0;

    //opterr = 0;

    for (;;)
    {
        static struct option long_options[] =
        {
            {"dump",    no_argument, &opt_dump, 1},
            {"version", no_argument, NULL, 'v'},
            {NULL,      no_argument, NULL, 0}
        };

        int option_index = 0;

        c = getopt_long(argc, argv, "dv", long_options, &option_index);

        if (c == -1)
            break;

        switch (c)
        {
            case 0:
                if (long_options[option_index].flag != 0)
                    break;

                break;

            case 'v':
                printf("nbttool 1.0 (%s, %s)\n", __DATE__, __TIME__);

                return EXIT_SUCCESS;

            case '?':
                break;
        }
    }

    if (optind < argc)
    {
        /* There is more in argv */

        if (opt_dump)
            dump_nbt(argv[optind]); 
    }


    return 0;
}

void dump_nbt(char *filename)
{
    NBT_File *nbt = NULL;

    /* Enough parameters given? */
    if (NBT_Init(&nbt, filename) != NBT_OK)
    {
        fprintf(stderr, "NBT_Init(): Failure initializing\n");
        fprintf(stderr, "File: %s\n", filename);

        return;
    }

    /* Try parsing */
    NBT_Parse(nbt);
    NBT_Print_Tag(nbt->root);
    NBT_Free(nbt);

    return;
}
