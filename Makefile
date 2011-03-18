# -----------------------------------------------------------------------------
# "THE BEER-WARE LICENSE" (Revision 42):
# <webmaster@flippeh.de> wrote this file. As long as you retain this notice you
# can do whatever you want with this stuff. If we meet some day, and you think
# this stuff is worth it, you can buy me a beer in return. Lukas Niederbremer.
# -----------------------------------------------------------------------------

CFLAGS=-g -Wall -Wextra -std=c99 -pedantic -Wno-implicit-function-declaration

all: nbtreader check

nbtreader: main.o libnbt.a
	$(CC) $(CFLAGS) main.o -L. -lnbt -lz -o nbtreader

check: check.c libnbt.a
	$(CC) $(CFLAGS) check.c -L. -lnbt -lz -o check

main.o: main.c

libnbt.a: nbt.o
	ar -rcs libnbt.a nbt.o

nbt.o: nbt.h nbt.c
