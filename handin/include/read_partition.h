#ifndef _READ_PARTITION_H
#define _READ_PARTITION_H

#include "genhd.h"

int do_read_partition(char *disk, int partition_number, struct partition *result, int *base);
int do_print_partition(char *disk, int partition_number);
#endif
