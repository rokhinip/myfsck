#include <errno.h>
#include <error.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ext2_fs.h"
#include "read_partition.h"
#include "read_superblock.h"
#include "readwrite.h"

extern const unsigned int sector_size_bytes;
extern int device;

const unsigned int super_block_offset = 1024;
const unsigned int block_group_desc_offset = 2048;

#define CHECK_ERROR(status, func)                                       \
        if ((func) < 0) {                                               \
                error_at_line((status), errno, __FILE__, __LINE__, NULL); \
        }

//
// operations on superblock
//
//
// read a superblock struct into the result
int do_read_superblock(char *disk, int partition_number, struct ext2_super_block *result)
{
        int base_sector;
        char buf[sector_size_bytes];

        struct partition p;

        // read partition
        CHECK_ERROR(-1, do_read_partition(disk, partition_number, &p, &base_sector));

        // read the block of the superblock
        int offset = base_sector + p.start_sect + super_block_offset / sector_size_bytes;
        CHECK_ERROR(-1, open_read_close_sect(disk, offset, 1, buf));

        memcpy(result, buf, sizeof(*result));

        return 0;
}

static inline int get_number_of_groups(struct ext2_super_block *sb)
{
        return (sb->s_blocks_count + sb->s_blocks_per_group - 1) / sb->s_blocks_per_group;
}

static inline int get_block_size(struct ext2_super_block *sb)
{
        return 1024 * (int)pow(2, sb->s_log_block_size);
}

static inline int get_blocks_per_group(struct ext2_super_block *sb)
{
        return sb->s_blocks_per_group;
}

static inline int get_inodes_per_group(struct ext2_super_block *sb)
{
        return sb->s_inodes_per_group;
}

int do_print_superblock(char *disk, int partition_number)
{
        struct ext2_super_block ext2_sb;

        CHECK_ERROR(-1, do_read_superblock(disk, partition_number, &ext2_sb));

        printf("====== superblock of partition %d ======\n", partition_number);
        printf("Magic: 0x%X\n", ext2_sb.s_magic);
        printf("Block Size: %d\n", get_block_size(&ext2_sb));
        printf("Fragment Size: %d\n", 1024*(int)pow(2, ext2_sb.s_log_frag_size));
        printf("Inodes count: %d\n", ext2_sb.s_inodes_count);
        printf("Blocks count: %d\n", ext2_sb.s_blocks_count);
        printf("Block group number: %d\n", ext2_sb.s_block_group_nr);
        printf("Blocks per group: %d\n", ext2_sb.s_blocks_per_group);
        printf("Inodes per group: %d\n", get_inodes_per_group(&ext2_sb));
        printf("Number of group: %d\n", get_number_of_groups(&ext2_sb));

        return 0;
}

// return a buffer that contains the data of a block of a specified partition
char * read_block(char *disk, int partition_number, int block_number)
{
        int base_sector;

        struct partition p;
        struct ext2_super_block ext2_sb;

        // get partition info
        CHECK_ERROR(-1, do_read_partition(disk, partition_number, &p, &base_sector));

        // get block size
        CHECK_ERROR(-1, do_read_superblock(disk, partition_number, &ext2_sb));
        int block_size = get_block_size(&ext2_sb);

        char *buf = (char *)malloc(block_size);
        if (!buf) {
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);
        }

        int block_byte_offset = (block_size * block_number); // get to the block
        int sector_offset = base_sector + p.start_sect + block_byte_offset / sector_size_bytes;
        int sectors_per_block = block_size / sector_size_bytes;

        CHECK_ERROR(-1, open_read_close_sect(disk, sector_offset, sectors_per_block, buf));

        return buf;
}

//
// operations on group description
//
//
// read group description struct into result
int do_read_group_desc(char *disk, int partition_number, int group_number, struct ext2_group_desc *result)
{
        struct ext2_super_block ext2_sb;

        // get super block
        CHECK_ERROR(-1, do_read_superblock(disk, partition_number, &ext2_sb));

        // get number of groups
        int number_of_groups = get_number_of_groups(&ext2_sb);
        if (group_number >= number_of_groups) {
                fprintf(stderr, "wrong group_number: %d, max: %d", group_number, number_of_groups-1);
                return -1;
        }

        int gd_size = sizeof(*result);
        char *buf = read_block(disk, partition_number, 2);

        memcpy(result, buf+group_number*gd_size, gd_size);
        free(buf);

        return 0;
}

static inline int get_block_bitmap_bid(struct ext2_group_desc *desc)
{
        return desc->bg_block_bitmap;
}

static inline int get_inode_bitmap_bid(struct ext2_group_desc *desc)
{
        return desc->bg_inode_bitmap;
}

static inline int get_inode_table_bid(struct ext2_group_desc *desc)
{
        return desc->bg_inode_table;
}

static inline int get_free_blocks_count(struct ext2_group_desc *desc)
{
        return desc->bg_free_blocks_count;
}

static inline int get_free_inodes_count(struct ext2_group_desc *desc)
{
        return desc->bg_free_inodes_count;
}

void print_group_desc(char *disk, int partition_number, int group_number)
{
        struct ext2_group_desc ext2_gd;

        // get group description
        CHECK_ERROR(-1, do_read_group_desc(disk, partition_number, group_number, &ext2_gd));

        printf("====== desc of partition %d, group %d======\n", partition_number, group_number);
        printf("block_bitmap: %d\n", get_block_bitmap_bid(&ext2_gd));
        printf("inode_bitmap: %d\n", get_inode_bitmap_bid(&ext2_gd));
        printf("inode_table: %d\n", get_inode_table_bid(&ext2_gd));
        printf("free_blocks_count: %d\n", ext2_gd.bg_free_blocks_count);
        printf("free_inodes_count: %d\n", ext2_gd.bg_free_inodes_count);
        printf("used_dirs_count: %d\n", ext2_gd.bg_used_dirs_count);
}

//
// get bitmaps
//
//
// return a block that contains the block bitmap
char * read_block_bitmap(char *disk, int partition_number, int group_number)
{
        struct ext2_group_desc ext2_gd;

        // get group description
        CHECK_ERROR(-1, do_read_group_desc(disk, partition_number, group_number, &ext2_gd));

        // get the block id of the block_bitmap
        int block_bitmap_id = get_block_bitmap_bid(&ext2_gd);
        return read_block(disk, partition_number, block_bitmap_id);
}

// return a block that contains the inode bitmap
char * read_inode_bitmap(char *disk, int partition_number, int group_number)
{
        struct ext2_group_desc ext2_gd;

        // get group description
        CHECK_ERROR(-1, do_read_group_desc(disk, partition_number, group_number, &ext2_gd));

        // get the block id of the block_bitmap
        int inode_bitmap_id = get_inode_bitmap_bid(&ext2_gd);
        return read_block(disk, partition_number, inode_bitmap_id);
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

// test if the free_blocks_count in group description is consistent with the map
void verify_block_map(char *disk, int partition_number, int group_number)
{
        struct ext2_super_block ext2_sb;
        struct ext2_group_desc ext2_gd;

        // get super block
        CHECK_ERROR(-1, do_read_superblock(disk, partition_number, &ext2_sb));
        int block_size = get_block_size(&ext2_sb);

        // get group description
        CHECK_ERROR(-1, do_read_group_desc(disk, partition_number, group_number, &ext2_gd));

        char *map = read_block_bitmap(disk, partition_number, group_number);

        int free_blocks_count = get_free_blocks_count(&ext2_gd);
        int map_free_blocks_count = 0;
        for (int i = 0; i < block_size*8; i++) {
                if (!allocated(map, i)) {
                        map_free_blocks_count++;
                }
        }

        if (map_free_blocks_count != free_blocks_count) {
                printf("free blocks count not equal, in desc: %d, in map: %d\n",
                       free_blocks_count, map_free_blocks_count);
        } else {
                printf("free blocks count equal: %d\n", free_blocks_count);
        }

        free(map);
}

// test if the free_inodes_count in group description is consistent with the map
void verify_inode_map(char *disk, int partition_number, int group_number)
{
        struct ext2_super_block ext2_sb;
        struct ext2_group_desc ext2_gd;

        // get super block
        CHECK_ERROR(-1, do_read_superblock(disk, partition_number, &ext2_sb));
        int block_size = get_block_size(&ext2_sb);

        // get group description
        CHECK_ERROR(-1, do_read_group_desc(disk, partition_number, group_number, &ext2_gd));

        char *map = read_inode_bitmap(disk, partition_number, group_number);

        int free_inodes_count = get_free_inodes_count(&ext2_gd);
        int map_free_inodes_count = 0;
        for (int i = 0; i < block_size*8; i++) {
                if (!allocated(map, i)) {
                        map_free_inodes_count++;
                }
        }

        if (map_free_inodes_count != free_inodes_count) {
                printf("free inodes count not equal, in desc: %d, in map: %d\n",
                       free_inodes_count, map_free_inodes_count);
        } else {
                printf("free inodes count equal: %d\n", free_inodes_count);
        }

        free(map);
}

//
// get inode table
//
// return a block that contains the inode table
char * read_inode_table(char *disk, int partition_number, int group_number)
{
        struct ext2_group_desc ext2_gd;

        // get group description
        CHECK_ERROR(-1, do_read_group_desc(disk, partition_number, group_number, &ext2_gd));

        // get the block id of the block_bitmap
        int inode_table_id = get_inode_table_bid(&ext2_gd);

        return read_block(disk, partition_number, inode_table_id);
}

// test if a block is allocated in the bitmap
int block_allocated(char *disk, int partition_number, int block_number)
{
        struct ext2_super_block ext2_sb;

        // get inodes_per_group
        CHECK_ERROR(-1, do_read_superblock(disk, partition_number, &ext2_sb));
        int blocks_per_group = get_blocks_per_group(&ext2_sb);

        // block start from 0
        int group_number = block_number / blocks_per_group;
        int block_offset_in_group = block_number % blocks_per_group;

        char *map = read_block_bitmap(disk, partition_number, group_number);
        int ret = allocated(map, block_offset_in_group);
        free(map);

        return ret;
}

// test if an inode is allocated in the bitmap
int inode_allocated(char *disk, int partition_number, int inode_number)
{
        struct ext2_super_block ext2_sb;

        // get inodes_per_group
        CHECK_ERROR(-1, do_read_superblock(disk, partition_number, &ext2_sb));
        int inodes_per_group = get_inodes_per_group(&ext2_sb);

        // inode start from 1
        int group_number = (inode_number - 1) / inodes_per_group;
        int inode_offset_in_group = (inode_number - 1) % inodes_per_group;

        char *map = read_inode_bitmap(disk, partition_number, group_number);
        int ret = allocated(map, inode_offset_in_group);
        free(map);

        return ret;
}

void verify_block_allocted(char *disk, int partition_number, int group_number)
{
        struct ext2_super_block ext2_sb;
        struct ext2_group_desc ext2_gd;

        // get blocks per group
        CHECK_ERROR(-1, do_read_superblock(disk, partition_number, &ext2_sb));
        int blocks_per_group = get_blocks_per_group(&ext2_sb);

        // get free blocks count
        CHECK_ERROR(-1, do_read_group_desc(disk, partition_number, group_number, &ext2_gd));
        int free_blocks_count = get_free_blocks_count(&ext2_gd);

        int map_free_blocks_count = 0;
        int start = group_number * blocks_per_group;
        int end = start + blocks_per_group;
        for (int i = start; i < end; i++) {
                if (!block_allocated(disk, partition_number, i)) {
                        map_free_blocks_count++;
                }
        }

        printf("--- verify block_allocted ---\n");
        if (map_free_blocks_count != free_blocks_count) {
                printf("free blocks count not equal, in desc: %d, in map: %d\n",
                       free_blocks_count, map_free_blocks_count);
        } else {
                printf("free blocks count equal: %d\n", free_blocks_count);

        }
}

void verify_inode_allocted(char *disk, int partition_number, int group_number)
{
        struct ext2_super_block ext2_sb;
        struct ext2_group_desc ext2_gd;

        // get inodes per group
        CHECK_ERROR(-1, do_read_superblock(disk, partition_number, &ext2_sb));
        int inodes_per_group = get_inodes_per_group(&ext2_sb);

        // get free inodes count
        CHECK_ERROR(-1, do_read_group_desc(disk, partition_number, group_number, &ext2_gd));
        int free_inodes_count = get_free_inodes_count(&ext2_gd);

        int map_free_inodes_count = 0;
        int start = group_number * inodes_per_group + 1;
        int end = start + inodes_per_group;
        for (int i = start; i < end; i++) {
                if (!inode_allocated(disk, partition_number, i)) {
                        map_free_inodes_count++;
                }
        }

        printf("--- verify inode_allocted ---\n");
        if (map_free_inodes_count != free_inodes_count) {
                printf("free inodes count not equal, in desc: %d, in map: %d\n",
                       free_inodes_count, map_free_inodes_count);
        } else {
                printf("free inodes count equal: %d\n", free_inodes_count);
        }
}

// read the inode entry into the result
struct ext2_inode * get_inode_entry(char *disk, int partition_number, int inode_number)
{
        struct ext2_super_block ext2_sb;
        struct ext2_inode *inode;

        // get inodes_per_group
        CHECK_ERROR(-1, do_read_superblock(disk, partition_number, &ext2_sb));
        int inodes_per_group = get_inodes_per_group(&ext2_sb);

        // inode start from 1
        int group_number = (inode_number - 1) / inodes_per_group;
        int inode_offset_in_group = (inode_number - 1) % inodes_per_group;

        int table_offset = inode_offset_in_group * sizeof(struct ext2_inode);
        char *table = read_inode_table(disk, partition_number, group_number);

        inode = (struct ext2_inode *)malloc(sizeof(struct ext2_inode));
        if (!inode) {
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);
        }
        memcpy(inode, table+table_offset, sizeof(struct ext2_inode));

        free(table);

        return inode;
}

//
// test inode fields
//
//
int is_dir(struct ext2_inode *inode)
{
        return (inode->i_mode & EXT2_S_IFDIR) != 0;
}

void print_and_verify_partition_info(char *disk, int partition_number, int group_number)
{
        struct ext2_super_block ext2_sb;
        struct ext2_inode *inode;

        do_print_superblock(disk, partition_number);

        // get inodes_per_group
        CHECK_ERROR(-1, do_read_superblock(disk, partition_number, &ext2_sb));

        int number_of_groups = get_number_of_groups(&ext2_sb);

        for (int i = 0; i < number_of_groups; i++) {
                print_group_desc(disk, partition_number, i);
                verify_block_map(disk, partition_number, i);
                verify_inode_map(disk, partition_number, i);
                verify_block_allocted(disk, partition_number, i);
                verify_inode_allocted(disk, partition_number, i);
        }

        // get the root dir
        inode = get_inode_entry(disk, partition_number, 2);

        printf("is root inode a dir? %d\n", is_dir(inode));
        printf("is root inode allocated? %d\n", inode_allocated(disk, partition_number, 2));

        free(inode);
}
