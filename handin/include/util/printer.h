#ifndef _UTIL_PRINTER_H
#define _UTIL_PRINTER_H

#include "ext2_fs.h"
#include "disk.h"

void print_partitions(disk_t *disk, int partition_number);
void print_all_groups_desc(disk_t *disk);
void verify_all_blocks_allocated(disk_t *disk);
void verify_all_inodes_allocated(disk_t *disk);
void print_dir_info(struct ext2_dir_entry_2 *dir);
void print_ls(partition_t *pt, char *path);
void verify_file_block_allocated(partition_t *pt, int inode);
void print_inode(partition_t *pt, int inode);
void print_group_desc(partition_t *pt, int group_number);
int get_symbolic_path(partition_t *pt, int inode_id, char *path);

int search_file(partition_t *pt, char *path, struct ext2_dir_entry_2 *ret);

int print_part2(disk_t *disk);

#endif
