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

#define NEW_INSTANCE(ret, structure)                                    \
        if (((ret) = malloc(sizeof(structure))) == NULL) {              \
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);     \
        }

extern const unsigned int sector_size_bytes;
extern int device;

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

int open_disk(char *path, disk_t *disk)
{
        device = open(path, O_RDWR);
        if (device < 0) {
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);
        }

        load_partitions(disk);

        // get_partition();

        // get_groups()
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
