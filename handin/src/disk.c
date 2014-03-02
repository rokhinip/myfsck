#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "disk.h"
#include "genhd.h"
#include "readwrite.h"
#include "read_partition.h"
#include "util/partition.h"

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

                // partition index starts from 1
                disk->partitions[i]->id = i+1;
        }

        return 0;
}

static int load_inode_table(partition_t *pt, int group_id)
{
        group_t * g = pt->groups[group_id];

        g->entry_count = get_inodes_per_group(pt) - get_free_inodes_count(g);
        g->inode_table = malloc(sizeof(struct ext2_inode *) * get_inodes_per_group(pt));
        if (!g->inode_table) {
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);
        }

        int block_count = (get_inodes_per_group(pt) * sizeof(struct ext2_inode) + (get_block_size(pt) - 1)) /  get_block_size(pt);
        char *table = read_block(pt, get_inode_table_bid(g), block_count);

        for (int i = 0; i < get_inodes_per_group(pt); i++) {
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

int open_disk(char *path, disk_t *disk, int fix_partition)
{
        device = open(path, O_RDWR);
        if (device < 0) {
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);
        }

        load_partitions(disk);

        if (!fix_partition) { // short cut for part I
                return 0;
        }
        
        // load groups for each partition
        for (int i = 0; i < disk->partition_count; i++) {
                if (IS_EXT2_PARTITION(disk->partitions[i])) {
                        // only process ext2 format
                        load_groups(disk->partitions[i]);
                }
        }
        return 0;
}

int free_disk(disk_t *disk)
{
        close(device);
        for (int i = 0; i < disk->partition_count; i++) {
                partition_t *pt = disk->partitions[i];
                free(pt->partition_info);
                free(pt->super_block);

                for (int j = 0; j < pt->group_count; j++) {
                        group_t *g = pt->groups[j];
                        free(g->desc);
                        free(g->block_bitmap);
                        free(g->inode_bitmap);

                        for (int k = 0; k < g->entry_count; k++) {
                                free(g->inode_table[k]);
                        }
                        free(g->inode_table);
                        free(g);
                }
                free(pt->groups);
                free(pt);
        }
        free(disk->partitions);

        return 0;
}

int is_ext2_partition(partition_t *pt)
{
        return IS_EXT2_PARTITION(pt);
}
