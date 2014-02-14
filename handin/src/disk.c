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

static char * read_block(partition_t *pt, int block_index)
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

        char *buf = (char *)malloc(block_size);
        if (!buf) {
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);
        }

        read_sectors(sector_offset, sectors_per_block, buf);

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

void print_group_desc(group_t *g, int partition_number, int group_number)
{
        printf("====== desc of partition %d: group %d ======\n", partition_number, group_number);
        printf("block_bitmap: %d\n", get_block_bitmap_bid(g));
        printf("inode_bitmap: %d\n", get_inode_bitmap_bid(g));
        printf("inode_table: %d\n", get_inode_table_bid(g));
        printf("free_blocks_count: %d\n", g->desc->bg_free_blocks_count);
        printf("free_inodes_count: %d\n", g->desc->bg_free_inodes_count);
        printf("used_dirs_count: %d\n", g->desc->bg_used_dirs_count);
}

void print_all_groups_desc(disk_t *disk)
{
        for (int i = 0; i < disk->partition_count; i++) {
                if (IS_EXT2_PARTITION(disk->partitions[i])) {
                        for (int j = 0; j < disk->partitions[i]->group_count; j++) {
                                print_group_desc(disk->partitions[i]->groups[j], i, j);
                        }
                }
        }
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

        char *group_desc_table = read_block(pt, group_desc_block_offset); // read group descriptor table
        
        // load group descriptor and data for each group
        for (int i = 0; i < pt->group_count; i++) {
                NEW_INSTANCE(pt->groups[i], group_t);
                NEW_INSTANCE(pt->groups[i]->desc, struct ext2_group_desc);
                memcpy(pt->groups[i]->desc,
                       group_desc_table+(sizeof(struct ext2_group_desc)*i),
                       sizeof(struct ext2_group_desc));

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

        print_all_groups_desc(disk);

        return 0;
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
