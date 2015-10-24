#include <stdio.h>
#include <string.h>
#include "nbt.h"

int main(int argc, const char *argv[]) {
    FILE *in = stdin;
    if (argc > 1) {
        in = fopen(argv[1], "rb");
        if (!in) {
            perror("fopen");
            return -1;
        }
    }

    while (__AFL_LOOP(10000)) {
        char buf[65536];
        size_t len = fread(buf, 1, sizeof(buf), in);
        nbt_free(nbt_parse(buf, len));
    }
}
