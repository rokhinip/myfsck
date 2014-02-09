CC = gcc
CFLAGS = -Wall -Werror -std=c99 -g

SRCDIR = src
IDIR = include

_SRC = readwrite.c read_partition.c
SRC = $(patsubst %, $(SRCDIR)/%, $(_SRC))

OBJ = $(patsubst %.c, %.o, $(_SRC))

all: myfsck

$(OBJ): $(SRC)
	$(CC) -I$(IDIR) $(CFLAGS) $(SRC) -c

readwrite: $(SRCDIR)/readwrite.c
	$(CC) $(CFLAGS) -DTESTREADWRITE $(SRCDIR)/readwrite.c -o readwrite

myfsck: $(SRCDIR)/myfsck.c $(OBJ)
	$(CC) -I$(IDIR) $(CFLAGS) $(OBJ) $(SRCDIR)/myfsck.c -o myfsck


.PHONY: clean
clean:
	@rm *.o -fr
	@rm readwrite -fr
	@rm myfsck -fr