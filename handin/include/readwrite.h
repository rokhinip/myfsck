#ifndef _READWRITE_H
#define _READWRITE_H

#include <sys/types.h>

void read_sectors (int64_t start_sector, unsigned int num_sectors, void *into);
void print_sector (unsigned char *buf);

#endif
