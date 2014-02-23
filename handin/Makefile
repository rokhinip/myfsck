CC = gcc
CFLAGS = -Wall -Werror -std=c99 -g

SRCDIR = src
IDIR = include

_SRC = readwrite.c read_partition.c disk.c link_list.c partition.c printer.c slice.c check.c
SRC = $(patsubst %, $(SRCDIR)/%, $(_SRC))

OBJ = $(patsubst %.c, %.o, $(_SRC))

all: myfsck

$(OBJ): $(SRC)
	$(CC) -I$(IDIR) $(CFLAGS) $(SRC) -c

readwrite: $(SRCDIR)/readwrite.c
	$(CC) $(CFLAGS) -DTESTREADWRITE $(SRCDIR)/readwrite.c -o readwrite

testlist: $(SRCDIR)/link_list.c
	$(CC) -I$(IDIR) $(CFLAGS) -DTESTLINKLIST $(SRCDIR)/link_list.c -o testlist

testslice: $(SRCDIR)/slice.c
	$(CC) -I$(IDIR) $(CFLAGS) -DTESTSLICE $(SRCDIR)/slice.c $(SRCDIR)/link_list.c -o testslice

myfsck: $(SRCDIR)/myfsck.c $(OBJ)
	$(CC) -I$(IDIR) $(CFLAGS) $(OBJ) $(SRCDIR)/myfsck.c $(LIB) -o myfsck


.PHONY: clean
clean:
	@rm *.o -f
	@rm readwrite -f
	@rm myfsck -f
	@rm testlist -f
	@rm testslice -f
