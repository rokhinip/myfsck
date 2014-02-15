#define _GNU_SOURCE

#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "disk.h"
#include "genhd.h"
#include "readwrite.h"
#include "read_partition.h"

#define CHECK_ERROR(status, func)                                       \
        if ((func) < 0) {                                               \
                error_at_line((status), errno, __FILE__, __LINE__, NULL); \
        }

#define NEW_INSTANCE(ret, structure)                                    \
        if (((ret) = malloc(sizeof(structure))) == NULL) {              \
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);     \
        }

#define IS_EXT2_PARTITION(partition) ((partition)->partition_info->sys_ind == 0x83)

extern const unsigned int sector_size_bytes;
extern int device;

const unsigned int super_block_offset = 1024;
const unsigned int group_desc_block_offset = 2;

// printers
void print_all_groups_desc(disk_t *disk);
void print_partitions(disk_t *disk, int partition_number);
void verify_all_blocks_allocated(disk_t *disk);
void verify_all_inodes_allocated(disk_t *disk);

static int load_partitions(disk_t *disk)
{
        int i, base_sector;

        struct partition p;

        for (i = 0;;i++) {
                // [*] partition number start from 1
                if (do_read_partition(i+1, NULL, NULL) < 0) {
                        break; // reach the end
                }
        }

        // create partition array
        disk->partition_count = i;
        disk->partitions = malloc(sizeof(partition_t *) * disk->partition_count);
        if (!disk->partitions) {
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);
        }

        // load partition info
        for (i = 0; i < disk->partition_count; i++) {
                do_read_partition(i+1, &p, &base_sector);

                NEW_INSTANCE(disk->partitions[i], partition_t);

                // copy parititon info and base_sector
                NEW_INSTANCE(disk->partitions[i]->partition_info, struct partition);
                memcpy(disk->partitions[i]->partition_info, &p, sizeof(struct partition));
                disk->partitions[i]->base_sector = base_sector;

                // partition index starts from 1
                disk->partitions[i]->id = i+1;
        }

        return 0;
}

// getters for a partition
static inline int get_number_of_groups(partition_t *pt)
{
        return (pt->super_block->s_blocks_count +
                pt->super_block->s_blocks_per_group - 1)
                / pt->super_block->s_blocks_per_group;
}

static inline int get_block_size(partition_t *pt)
{
        return 1024 << pt->super_block->s_log_block_size;
}

static inline int get_blocks_per_group(partition_t *pt)
{
        return pt->super_block->s_blocks_per_group;
}

static inline int get_inodes_per_group(partition_t *pt)
{
        return pt->super_block->s_inodes_per_group;
}

static char * read_block(partition_t *pt, int block_index, int count)
{
        int block_size = get_block_size(pt);
        if (block_size < 0) {
                exit(-1);
        }

        int block_byte_offset = block_size * block_index;
        int sector_offset =
                pt->base_sector +
                pt->partition_info->start_sect +
                (block_byte_offset / sector_size_bytes);

        int sectors_per_block = block_size / sector_size_bytes;

        char *buf = (char *)malloc(block_size*count);
        if (!buf) {
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);
        }

        read_sectors(sector_offset, sectors_per_block*count, buf);

        return buf;
}

// getters for one group
static inline int get_block_bitmap_bid(group_t *g)
{
        return g->desc->bg_block_bitmap;
}

static inline int get_inode_bitmap_bid(group_t *g)
{
        return g->desc->bg_inode_bitmap;
}

static inline int get_inode_table_bid(group_t *g)
{
        return g->desc->bg_inode_table;
}

static inline int get_free_blocks_count(group_t *g)
{
        return g->desc->bg_free_blocks_count;
}

static inline int get_free_inodes_count(group_t *g)
{
        return g->desc->bg_free_inodes_count;
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

// test if the inode/block is allocated in the bitmap
static inline int allocated(char *map, int offset)
{
        // [*] offset is the inode number's offset in
        // one group, it starts from 0
        int byte_offset = offset / 8;
        int bit_offset = offset % 8;

        return (map[byte_offset] >> bit_offset) & 0x1;
}

// test if a block is allocated in the bitmap
int block_allocated(partition_t *pt, int block_number)
{
        // get inodes_per_group
        int blocks_per_group = get_blocks_per_group(pt);

        // block start from 0
        int group_number = block_number / blocks_per_group;
        int block_offset_in_group = block_number % blocks_per_group;

        return allocated(pt->groups[group_number]->block_bitmap, block_offset_in_group);
}

// test if a block is allocated in the bitmap
int inode_allocated(partition_t *pt, int inode_number)
{
        // get inodes_per_group
        int inodes_per_group = get_inodes_per_group(pt);

        // inode start from 1
        int group_number = (inode_number - 1) / inodes_per_group;
        int inode_offset_in_group = (inode_number - 1) % inodes_per_group;

        return allocated(pt->groups[group_number]->inode_bitmap, inode_offset_in_group);
}

struct ext2_inode * get_inode_entry(partition_t *pt, int inode_id)
{
        int inodes_per_group = get_inodes_per_group(pt);

        // inode start from 1
        int group_number = (inode_id - 1) / inodes_per_group;
        int inode_offset_in_group = (inode_id - 1) % inodes_per_group;

        return pt->groups[group_number]->inode_table[inode_offset_in_group];
}

int is_dir(partition_t *pt, int inode_id)
{
        struct ext2_inode *inode = get_inode_entry(pt, inode_id);
        return (inode->i_mode & EXT2_S_IFDIR) != 0;
}

int is_symbol(partition_t *pt, int inode_id)
{
        struct ext2_inode *inode = get_inode_entry(pt, inode_id);
        return (inode->i_mode & EXT2_S_IFLNK) != 0;
}

int get_dir(partition_t *pt, int inode_id, struct ext2_dir_entry_2 *dir)
{
        struct ext2_inode *inode = get_inode_entry(pt, inode_id);

        char *block = read_block(pt, inode->i_block[0], 1);
        memcpy(dir, block, sizeof(*dir));
        free(block);

        return 0;
}

int append(block_slice_t *slice, int item)
{
        if (slice->len >= slice->cap) {
                fprintf(stderr, "append failed: exceed capcity\n");
                return -1;
        }
        slice->array[slice->len] = item;
        slice->len++;

        return slice->len;
}

int get_indirect_block(block_slice_t *slice, partition_t *pt, int block_id)
{
        int entries_per_block = get_block_size(pt) / 4; // 32-bit int
        int *block = (int *)read_block(pt, block_id, 1);
        int i;

        for (i = 0; i < entries_per_block; i++) {
                if (block[i] == 0) {
                        return 0;
                }
                append(slice, block[i]);
        }

        free(block);
        return i;
}

int get_double_indirect_block(block_slice_t *slice, partition_t *pt, int block_id)
{
        int entries_per_block = get_block_size(pt) / 4; // 32-bit int
        int *indirect_block = (int *)read_block(pt, block_id, 1);
        int i;

        for (i = 0; i < entries_per_block; i++) {
                if (indirect_block[i] == 0) {
                        return 0;
                }
                int ret = get_indirect_block(slice, pt, indirect_block[i]);
                if (ret == 0) {
                        return 0;
                }
        }

        free(indirect_block);
        return i;
}

int get_triple_indirect_block(block_slice_t *slice, partition_t *pt, int block_id)
{
        int entries_per_block = get_block_size(pt) / 4; // 32-bit int
        int *double_indirect_block = (int *)read_block(pt, block_id, 1);
        int i;

        for (i = 0; i < entries_per_block; i++) {
                if (double_indirect_block[i] == 0) {
                        return 0;
                }
                int ret = get_double_indirect_block(slice, pt, double_indirect_block[i]);
                if (ret == 0) {
                        return 0;
                }
        }

        free(double_indirect_block);
        return i;
}

block_slice_t * get_block_slice(partition_t *pt, int inode_id)
{
        struct ext2_inode *inode = get_inode_entry(pt, inode_id);
        int cap = inode->i_blocks / (2 << pt->super_block->s_log_block_size);

        block_slice_t *slice;
        NEW_INSTANCE(slice, block_slice_t);

        slice->cap = cap;
        slice->len = 0;
        slice->array = malloc(sizeof(int) * slice->cap);
        if (!slice->array) {
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);
        }

        for (int i = 0; i < 12; i++) {
                if (inode->i_block[i] == 0) {
                        return slice;
                }
                append(slice, inode->i_block[i]);
        }

        if (get_indirect_block(slice, pt, inode->i_block[12]) == 0) {
                return slice;
        }
        if (get_double_indirect_block(slice, pt, inode->i_block[13]) == 0) {
                return slice;
        }
        get_triple_indirect_block(slice, pt, inode->i_block[14]);

        return slice;
}

static int load_inode_table(partition_t *pt, int group_id)
{
        group_t * g = pt->groups[group_id];

        g->entry_count = get_inodes_per_group(pt) - get_free_inodes_count(g);
        g->inode_table = malloc(sizeof(struct ext2_inode *) * g->entry_count);
        if (!g->inode_table) {
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);
        }

        int block_count = (g->entry_count * sizeof(struct ext2_inode) + (get_block_size(pt) - 1)) /  get_block_size(pt);
        char *table = read_block(pt, get_inode_table_bid(g), block_count);

        for (int i = 0; i < g->entry_count; i++) {
                NEW_INSTANCE(g->inode_table[i], struct ext2_inode);
                memcpy(g->inode_table[i], table+i*sizeof(struct ext2_inode), sizeof(struct ext2_inode));
        }
        free(table);

        return 0;
}

static int load_groups(partition_t *pt)
{
        char buf[sector_size_bytes];

        // load superblock
        NEW_INSTANCE(pt->super_block, struct ext2_super_block);
        int offset = pt->base_sector + pt->partition_info->start_sect + (super_block_offset / sector_size_bytes);
        read_sectors(offset, 1, buf);
        memcpy(pt->super_block, buf, sizeof(struct ext2_super_block));

        // make the group array
        pt->group_count = get_number_of_groups(pt);
        pt->groups = malloc(sizeof(group_t *) * pt->group_count);
        if (!pt->groups) {
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);
        }

        char *group_desc_table = read_block(pt, group_desc_block_offset, 1); // read group descriptor table

        // load group descriptor and data for each group
        for (int i = 0; i < pt->group_count; i++) {
                NEW_INSTANCE(pt->groups[i], group_t);
                NEW_INSTANCE(pt->groups[i]->desc, struct ext2_group_desc);
                memcpy(pt->groups[i]->desc,
                       group_desc_table+(sizeof(struct ext2_group_desc)*i),
                       sizeof(struct ext2_group_desc));

                // group's index starts from 0
                pt->groups[i]->id = i;

                // get bitmaps
                pt->groups[i]->block_bitmap = read_block(pt, get_block_bitmap_bid(pt->groups[i]), 1);
                pt->groups[i]->inode_bitmap = read_block(pt, get_inode_bitmap_bid(pt->groups[i]), 1);

                // get inode table
                load_inode_table(pt, i);
        }
        free(group_desc_table);

        return 0;
}

int open_disk(char *path, disk_t *disk)
{
        device = open(path, O_RDWR);
        if (device < 0) {
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);
        }

        load_partitions(disk);

        // load groups for each partition
        for (int i = 0; i < disk->partition_count; i++) {
                if (IS_EXT2_PARTITION(disk->partitions[i])) {
                        // only process ext2 format
                        load_groups(disk->partitions[i]);
                }
        }
        return 0;
}

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

void print_slice(block_slice_t *slice)
{
        printf("cap: %d\n", slice->cap);
        printf("len: %d\n", slice->len);
        printf("blocks:\n");
        for (int i = 0; i < slice->len; i++) {
                printf("%d ", slice->array[i]);
        }
        printf("\n");
}

void list_dir_in_block(partition_t *pt, int block_id)
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

int find_child_in_block(partition_t *pt, int block_id, char *childname, struct ext2_dir_entry_2 *ret)
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

int find_child(partition_t *pt, struct ext2_dir_entry_2 *parent, char *childname, struct ext2_dir_entry_2 *ret)
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
