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

//
// exported functions
//

// open a disk
int open_disk(char *path, disk_t *disk);

// print partition info
void print_partitions(disk_t *disk, int partition_number);
void print_all_groups_desc(disk_t *disk);
void verify_all_blocks_allocated(disk_t *disk);
void verify_all_inodes_allocated(disk_t *disk);
void print_dir_info(struct ext2_dir_entry_2 *dir);
void print_ls(partition_t *pt, char *path);
void verify_file_block_allocated(partition_t *pt, int inode);
void print_inode(partition_t *pt, int inode);

// get attributes
int is_dir(partition_t *pt, int inode_id);
int get_dir(partition_t *pt, int inode_id, struct ext2_dir_entry_2 *dir);
int next_dir(struct ext2_dir_entry_2 *dir, struct ext2_dir_entry_2 *next);
int block_allocated(partition_t *pt, int block_number);
int inode_allocated(partition_t *pt, int inode_number);
int get_symbolic_path(partition_t *pt, int inode_id, char *path);

// others
int search_file(partition_t *pt, char *path, struct ext2_dir_entry_2 *ret);

#endif
