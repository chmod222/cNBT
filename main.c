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

    printf("# PRE ------------\n");
    NBT_Print_Tag(nbt->root);

    t = nbt->root;
    NBT_Change_Name(t, "Supergrief");

    if (t->type == TAG_Compound)
    {
        NBT_Tag *target;
        target = NBT_Find_Tag_By_Name("Data", t);

        if (target != NULL)
        {
            NBT_Tag *spawnx, *spawny, *spawnz;
            NBT_Tag *playerdata;

            spawnx = NBT_Find_Tag_By_Name("SpawnX", target);
            spawny = NBT_Find_Tag_By_Name("SpawnY", target);
            spawnz = NBT_Find_Tag_By_Name("SpawnZ", target);

            if ((spawnx == NULL) || (spawny == NULL) || (spawnz == NULL))
            {
                fprintf(stderr, "Couldn't get Spawn X, Y or Z\n");
            }
            else
            {
                int *sx, *sy, *sz;

                sx = (int *)spawnx->value;
                sy = (int *)spawny->value;
                sz = (int *)spawnz->value;

                playerdata = NBT_Find_Tag_By_Name("Player", target);
                if (playerdata != NULL)
                {
                    NBT_Tag *pos = NBT_Find_Tag_By_Name("Pos", playerdata);
                    if (pos != NULL)
                    {
                        NBT_List *l = (NBT_List *)pos->value;
                        if (l->length != 3)
                            fprintf(stderr, "Malformed position data\n");
                        else
                        {
                            double *x, *y, *z;

                            x = (double *)l->content[0];
                            y = (double *)l->content[1];
                            z = (double *)l->content[2];

                            printf("X,Y,Z: %.2f,%.2f,%.2f\n", *x, *y, *z);
                            printf("SX, SY, SZ: %d,%d,%d\n", *sx, *sy, *sz);
                        }
                    }
                    else
                    {
                        fprintf(stderr, "No position data\n");
                    }
                }
                else
                {
                    fprintf(stderr, "Couldn't fetch playerdata\n");
                }
            }    
        }
    }

    printf("# POST ------------\n");

    NBT_Print_Tag(nbt->root);
    NBT_Free(nbt);

    return 0;
}
