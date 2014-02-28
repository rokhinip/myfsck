#include <errno.h>
#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disk.h"
#include "genhd.h"
#include "readwrite.h"
#include "slice.h"

#define MIN(a, b) (a) < (b) ? (a) : (b)

#define NEW_INSTANCE(ret, structure)                                    \
        if (((ret) = malloc(sizeof(structure))) == NULL) {              \
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);     \
        }

#define IS_EXT2_PARTITION(partition) ((partition)->partition_info->sys_ind == 0x83)

extern const unsigned int sector_size_bytes;
extern int device;

extern unsigned int super_block_offset;
extern unsigned int group_desc_block_offset;

// getters for a partition
int get_number_of_groups(partition_t *pt)
{
        return (pt->super_block->s_blocks_count +
                pt->super_block->s_blocks_per_group - 1)
                / pt->super_block->s_blocks_per_group;
}

int get_block_size(partition_t *pt)
{
        return 1024 << pt->super_block->s_log_block_size;
}

int get_blocks_per_group(partition_t *pt)
{
        return pt->super_block->s_blocks_per_group;
}

int get_inodes_per_group(partition_t *pt)
{
        return pt->super_block->s_inodes_per_group;
}

char * read_block(partition_t *pt, int block_index, int count)
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

int write_block(partition_t *pt, int block_index, int count, char *buf)
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

        write_sectors(sector_offset, sectors_per_block*count, buf);

        return 0;
}

// getters for one group
int get_block_bitmap_bid(group_t *g)
{
        return g->desc->bg_block_bitmap;
}

int get_inode_bitmap_bid(group_t *g)
{
        return g->desc->bg_inode_bitmap;
}

int get_inode_table_bid(group_t *g)
{
        return g->desc->bg_inode_table;
}

int get_free_blocks_count(group_t *g)
{
        return g->desc->bg_free_blocks_count;
}

int get_free_inodes_count(group_t *g)
{
        return g->desc->bg_free_inodes_count;
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
        return (inode->i_mode & EXT2_S_IFDIR) == EXT2_S_IFDIR;
}

int is_symbol(partition_t *pt, int inode_id)
{
        struct ext2_inode *inode = get_inode_entry(pt, inode_id);
        return (inode->i_mode & EXT2_S_IFLNK) == EXT2_S_IFLNK;
}

int get_dir(partition_t *pt, int inode_id, struct ext2_dir_entry_2 *dir)
{
        struct ext2_inode *inode = get_inode_entry(pt, inode_id);

        char *block = read_block(pt, inode->i_block[0], 1);
        memcpy(dir, block, sizeof(*dir));
        free(block);

        return 0;
}

static int get_indirect_block(slice_t *slice, partition_t *pt, int block_id)
{
        int entries_per_block = get_block_size(pt) / 4; // 32-bit int
        int *block = (int *)read_block(pt, block_id, 1);
        int i;

        for (i = 0; i < entries_per_block; i++) {
                if (block[i] == 0) {
                        return 0;
                }
                append(slice, &block[i]);
        }

        free(block);
        return i;
}

static int get_double_indirect_block(slice_t *slice, partition_t *pt, int block_id)
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

static int get_triple_indirect_block(slice_t *slice, partition_t *pt, int block_id)
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

slice_t * get_blocks(partition_t *pt, int inode_id)
{
        struct ext2_inode *inode = get_inode_entry(pt, inode_id);
        int cap = inode->i_blocks / (2 << pt->super_block->s_log_block_size);

        slice_t *slice = make_slice(cap, sizeof(int));

        for (int i = 0; i < 12; i++) {
                if (inode->i_block[i] == 0) {
                        return slice;
                }
                append(slice, &inode->i_block[i]);
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

static int add_child_inodes(partition_t *pt, slice_t *s, int block_id)
{
        int block_size = get_block_size(pt);
        char *block = read_block(pt, block_id, 1);
        int offset = 0;
        struct ext2_dir_entry_2 dir;

        for(;;) {
                if (offset >= block_size) {
                        break;
                }
                memcpy(&dir, block+offset, MIN(sizeof(dir), (block_size - offset)));
                if (dir.inode == 0) {
                        break;
                }

                append(s, &dir.inode);
                offset += dir.rec_len;
        }

        free(block);

        return 0;
}

static int add_child_dirs(partition_t *pt, slice_t *s, int block_id)
{
        int block_size = get_block_size(pt);
        char *block = read_block(pt, block_id, 1);
        int offset = 0;
        struct ext2_dir_entry_2 dir;

        for(;;) {
                if (offset >= block_size) {
                        break;
                }
                memcpy(&dir, block+offset, MIN(sizeof(dir), (block_size - offset)));
                if (dir.inode == 0) {
                        break;
                }

                append(s, &dir);
                offset += dir.rec_len;
        }

        free(block);

        return 0;
}

slice_t *get_child_inodes(partition_t *pt, int inode_id)
{
        slice_t *inode_slice = make_slice(1024, sizeof(int));
        slice_t *block_slice = get_blocks(pt, inode_id);

        for (int i = 0; i < block_slice->len; i++) {
                int block_id;
                get(block_slice, i, &block_id);
                add_child_inodes(pt, inode_slice, block_id);
        }

        delete_slice(block_slice);

        return inode_slice;
}

slice_t * get_child_dirs(partition_t *pt, int inode_id)
{
        slice_t *dir_slice = make_slice(1024, sizeof(struct ext2_dir_entry_2));
        slice_t *block_slice = get_blocks(pt, inode_id);

        for (int i = 0; i < block_slice->len; i++) {
                int block_id;
                get(block_slice, i, &block_id);
                add_child_dirs(pt, dir_slice, block_id);
        }

        delete_slice(block_slice);

        return dir_slice;
}

int get_lost_found_inode(partition_t *pt)
{
        slice_t *s = get_child_dirs(pt, 2); // get chilren of root

        char *lost_found = "lost+found";

        struct ext2_dir_entry_2 dir;
        for (int i = 0; i < s->len; i++) {
                get(s, i, &dir);
                if (strncmp(dir.name, "lost+found", strlen(lost_found)) == 0) {
                        return dir.inode;
                }
        }
        printf("warning: no lost+found\n");
        return 0;
}
