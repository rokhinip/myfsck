#ifndef _CHECK_H
#define _CHECK_H

#include "util/partition.h"

int breadth_search(partition_t *pt, int (*func)(partition_t*, struct ext2_dir_entry_2 *));
void print_dirs(partition_t *pt);
void check_dir_ptrs(partition_t *pt);
int check_inode_ptr(partition_t *pt);

#endif