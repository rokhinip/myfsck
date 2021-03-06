#ifndef _READWRITE_H
#define _READWRITE_H

#include <sys/types.h>

void read_sectors (int64_t start_sector, unsigned int num_sectors, void *into);
void write_sectors (int64_t start_sector, unsigned int num_sectors, void *from);
void print_sector (unsigned char *buf);
int open_read_close_sect(char *disk, int start_sect, int num_sectors, char *buf);

#endif
