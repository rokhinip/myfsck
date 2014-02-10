#ifndef _READ_SUPERBLOCK_H
#define _READ_SUPERBLOCK_H

#include "ext2_fs.h"

void print_and_verify_partition_info(char *disk, int partition_number, int group_number);

#endif
