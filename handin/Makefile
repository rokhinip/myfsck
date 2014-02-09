CC = gcc
CFLAGS = -Wall -Werror -std=c99

SRCDIR = src
IDIR = include

_HEADERS = ext2_fs.h, genhd.h
HEADERS = $(patsubst %, $(IDIR)/%, $(_HEADERS))
SRC = readwrite.c
OBJ = $(patsubst %.o, %.c, $(SRC))

all: myfsck

readfile: $(SRCDIR)/readwrite.c
	$(CC) $(CFLAGS) -DTESTREADWRITE $(SRCDIR)/readwrite.c -o readwrite

myfsck: $(SRCDIR)/myfsck.c
	$(CC) -I$(IDIR) $(CFLAGS) $(SRCDIR)/myfsck.c -o myfsck


.PHONY: clean
clean:
	@rm *.o -fr
	@rm readwrite -fr
	@rm myfsck -fr