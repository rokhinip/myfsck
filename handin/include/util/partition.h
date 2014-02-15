#ifndef _UTIL_PARTITION_H
#define _UTIL_PARTITION_H

#include "disk.h"

char * read_block(partition_t *pt, int block_index, int count);

// get attributes for partition
int get_number_of_groups(partition_t *pt);
int get_inodes_per_group(partition_t *pt);
int get_block_size(partition_t *pt);
int get_blocks_per_group(partition_t *pt);

// get attributes for group
int get_block_bitmap_bid(group_t *g);
int get_inode_bitmap_bid(group_t *g);
int get_inode_table_bid(group_t *g);
int get_free_blocks_count(group_t *g);
int get_free_inodes_count(group_t *g);

// get item
struct ext2_inode * get_inode_entry(partition_t *pt, int inode_id);
int get_dir(partition_t *pt, int inode_id, struct ext2_dir_entry_2 *dir);
block_slice_t * get_block_slice(partition_t *pt, int inode_id);

//
int is_dir(partition_t *pt, int inode_id);
int is_symbol(partition_t *pt, int inode_id);
int block_allocated(partition_t *pt, int block_number);
int inode_allocated(partition_t *pt, int inode_number);

#endif
