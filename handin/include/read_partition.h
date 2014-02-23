#ifndef _READ_PARTITION_H
#define _READ_PARTITION_H

#include "genhd.h"

int do_read_partition(int partition_number, struct partition *result, int *base);

#endif
