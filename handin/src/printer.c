#define _GNU_SOURCE

#include <errno.h>
#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disk.h"
#include "read_partition.h"
#include "util/printer.h"
#include "util/partition.h"

#define IS_EXT2_PARTITION(partition) ((partition)->partition_info->sys_ind == 0x83)

// test if the free_blocks_count in group description is consistent with the map
void verify_block_allocated(partition_t *pt, int group_number)
{
        // get blocks per group
        int blocks_per_group = get_blocks_per_group(pt);

        // get free blocks count
        int free_blocks_count = get_free_blocks_count(pt->groups[group_number]);

        int map_free_blocks_count = 0;
        int start = group_number * blocks_per_group;
        int end = start + blocks_per_group;
        for (int i = start; i < end; i++) {
                if (!block_allocated(pt, i)) {
                        map_free_blocks_count++;
                }
        }

        printf("====== verify block_allocted partition %d: group %d ======\n", pt->id, group_number);
        if (map_free_blocks_count != free_blocks_count) {
                printf("free blocks count not equal, in desc: %d, in map: %d\n",
                       free_blocks_count, map_free_blocks_count);
        } else {
                printf("free blocks count equal: %d\n", free_blocks_count);

        }
}

// test if the free_blocks_count in group description is consistent with the map
void verify_inode_allocated(partition_t *pt, int group_number)
{
        // get inodes per group
        int inodes_per_group = get_inodes_per_group(pt);

        // get free inodes count
        int free_inodes_count = get_free_inodes_count(pt->groups[group_number]);

        int map_free_inodes_count = 0;
        int start = group_number * inodes_per_group + 1;
        int end = start + inodes_per_group;
        for (int i = start; i < end; i++) {
                if (!inode_allocated(pt, i)) {
                        map_free_inodes_count++;
                }
        }

        printf("====== verify inode_allocted partition %d: group %d ======\n", pt->id, group_number);
        if (map_free_inodes_count != free_inodes_count) {
                printf("free inode count not equal, in desc: %d, in map: %d\n",
                       free_inodes_count, map_free_inodes_count);
        } else {
                printf("free inodes count equal: %d\n", free_inodes_count);

        }
}

void verify_all_blocks_allocated(disk_t *disk)
{
        for (int i = 0; i < disk->partition_count; i++) {
                if (IS_EXT2_PARTITION(disk->partitions[i])) {
                        for (int j = 0; j < disk->partitions[i]->group_count; j++) {
                                verify_block_allocated(disk->partitions[i], j);
                        }
                }
        }
}

void verify_all_inodes_allocated(disk_t *disk)
{
        for (int i = 0; i < disk->partition_count; i++) {
                if (IS_EXT2_PARTITION(disk->partitions[i])) {
                        for (int j = 0; j < disk->partitions[i]->group_count; j++) {
                                verify_inode_allocated(disk->partitions[i], j);
                        }
                }
        }
}

void print_all_groups_desc(disk_t *disk)
{
        for (int i = 0; i < disk->partition_count; i++) {
                if (IS_EXT2_PARTITION(disk->partitions[i])) {
                        for (int j = 0; j < disk->partitions[i]->group_count; j++) {
                                print_group_desc(disk->partitions[i], j);
                        }
                }
        }
}

void print_partitions(disk_t *disk, int partition_number)
{
        if (partition_number <= 0 || partition_number > disk->partition_count) {
                printf("-1\n");
                return;
        }

        // [*] partition number start from 1
        partition_t *p = disk->partitions[partition_number-1];
        struct partition *pinfo = p->partition_info;
        printf("0x%02x %d %d\n", pinfo->sys_ind, pinfo->start_sect + p->base_sector, pinfo->nr_sects);
}

void print_dir_info(struct ext2_dir_entry_2 *dir)
{
        char name[EXT2_NAME_LEN];
        printf("====== dir info ======\n");
        printf("inode: %d\n", dir->inode);
        printf("rec_len : %d\n", dir->rec_len);
        printf("name_len: %d\n", dir->name_len);
        printf("file_type: 0x%X\n", dir->file_type);
        strncpy(name, dir->name, dir->name_len);
        name[dir->name_len] = 0;
        printf("name: %s\n", name);
}

void print_inode(partition_t *pt, int inode_id)
{
        struct ext2_inode *inode = get_inode_entry(pt, inode_id);
        printf("====== inode info ======\n");
        printf("inode id %d\n", inode_id);
        printf("mode: 0x%X\n", inode->i_mode);
        printf("block count: %d\n", inode->i_blocks);
}



void verify_file_block_allocated(partition_t *pt, int inode)
{
        block_slice_t *slice = get_block_slice(pt, inode);
        for (int i = 0; i < slice->len; i++) {
                if (!block_allocated(pt, slice->array[i])) {
                        printf("error data block[%d] not allocated in block_bitmap\n", slice->array[i]);
                }
        }
        printf("ok! all data blocks allocated\n");
}

void print_group_desc(partition_t *pt, int group_number)
{
        group_t *g = pt->groups[group_number];

        printf("====== desc of partition %d: group %d ======\n", pt->id, group_number);
        printf("block_bitmap: %d\n", get_block_bitmap_bid(g));
        printf("inode_bitmap: %d\n", get_inode_bitmap_bid(g));
        printf("inode_table: %d\n", get_inode_table_bid(g));
        printf("free_blocks_count: %d\n", g->desc->bg_free_blocks_count);
        printf("free_inodes_count: %d\n", g->desc->bg_free_inodes_count);
        printf("used_dirs_count: %d\n", g->desc->bg_used_dirs_count);
}

static void list_dir_in_block(partition_t *pt, int block_id)
{
        int block_size = get_block_size(pt);
        char *block = read_block(pt, block_id, 1);
        int offset = 0;
        struct ext2_dir_entry_2 dir;

        for (;;) {
                if (offset >= block_size) {
                        break;
                }

                memcpy(&dir, block+offset, sizeof(dir));
                if (dir.rec_len == 0) {
                        break;
                }
                print_dir_info(&dir);

                offset += dir.rec_len;
        }
        free(block);
}


static void print_slice(block_slice_t *slice)
{
        printf("cap: %d\n", slice->cap);
        printf("len: %d\n", slice->len);
        printf("blocks:\n");
        for (int i = 0; i < slice->len; i++) {
                printf("%d ", slice->array[i]);
        }
        printf("\n");
}

void print_ls(partition_t *pt, char *path)
{
        printf("ls %s\n", path);
        struct ext2_dir_entry_2 dir;

        int inode = search_file(pt, path, &dir);
        if (inode == 0) {
                return;
        }
        //struct ext2_inode *inode = get_inode_entry(pt, dir->inode);
        block_slice_t *slice = get_block_slice(pt, dir.inode);
        print_slice(slice);

        for (int i = 0; i < slice->len; i++) {
                list_dir_in_block(pt, slice->array[i]);
        }
        free(slice);
}

static int find_child_in_block(partition_t *pt, int block_id, char *childname, struct ext2_dir_entry_2 *ret)
{
        int block_size = get_block_size(pt);
        char *block = read_block(pt, block_id, 1);
        int offset = 0;
        struct ext2_dir_entry_2 dir;

        int child_inode = 0;
        for (;;) {
                if (offset >= block_size) {
                        break;
                }

                memcpy(&dir, block+offset, sizeof(dir));
                if (dir.rec_len == 0) {
                        break;
                }
                if (dir.name_len == strlen(childname) && strncmp(childname, dir.name, dir.name_len) == 0) {
                        child_inode = dir.inode;
                        memcpy(ret, &dir, sizeof(dir));
                        break;
                }
                offset += dir.rec_len;
        }
        free(block);

        return child_inode;
}

static int find_child(partition_t *pt, struct ext2_dir_entry_2 *parent, char *childname, struct ext2_dir_entry_2 *ret)
{
        block_slice_t *slice = get_block_slice(pt, parent->inode);

        int child_inode = 0;
        for (int i = 0; i < slice->len; i++) {
                print_slice(slice);
                child_inode = find_child_in_block(pt, slice->array[i], childname, ret);
                if (child_inode != 0) {
                        break;
                }
        }
        free(slice);
        return child_inode;
}

int search_file(partition_t *pt, char *path, struct ext2_dir_entry_2 *ret)
{
        char *saveptr;
        struct ext2_dir_entry_2 dir;

        if (path[0] != '/') {
                printf("need absolute path\n");
                return -1;
        }

        // copy the dir name to avoid segfault caused by strtok_r
        char *str = malloc(strlen(path) + 1);
        if (!str) {
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);
        }
        strcpy(str, path);

        get_dir(pt, 2, &dir); // get root
        int ret_inode = 0;
        for (;;) {
                char *token = strtok_r(str, "/", &saveptr);
                str = NULL;
                if (!token) {
                        break;
                }

                ret_inode = find_child(pt, &dir, token, ret);
                if (ret_inode == 0) {
                        printf("not found\n");
                        break;
                }

                print_inode(pt, ret_inode);
                if (is_dir(pt, ret_inode)) {
                        get_dir(pt, ret_inode, &dir);
                }
        }

        free(str);

        return ret_inode;
}

int get_symbolic_path(partition_t *pt, int inode_id, char *path)
{
        if (!is_symbol(pt, inode_id)) {
                fprintf(stderr, "not a symbolic link\n");
                return -1;
        }

        struct ext2_inode *inode = get_inode_entry(pt, inode_id);
        if (inode->i_blocks == 0) {
                memcpy(path, inode->i_block, sizeof(inode->i_block));
                return 0;
        }
        
        printf("too long symbol, not going to handle this moment, i_block: %d\n", inode->i_blocks);
        
        return -1;
}
