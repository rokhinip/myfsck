#ifndef _DISK_H
#define _DISK_H

#include "ext2_fs.h"

typedef struct block_slice_s {
        int *array;

        // length and capcity
        int len;
        int cap;
}block_slice_t;

// struct for one block group
typedef struct group_s {
        int id;
        struct ext2_group_desc *desc;
        char *block_bitmap;
        char *inode_bitmap;
        int entry_count;
        struct ext2_inode **inode_table;
}group_t;

// struct for one partition
typedef struct partition_s {
        int id;
        int base_sector;
        struct partition *partition_info;
        struct ext2_super_block *super_block;

        int group_count;
        group_t **groups;
}partition_t;

// struct for the disk
typedef struct disk_s {
        int partition_count;
        partition_t **partitions;
}disk_t;

// open a disk
int open_disk(char *path, disk_t *disk);
int is_ext2_partition(partition_t *pt);

#endif
