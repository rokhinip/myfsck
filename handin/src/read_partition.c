#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "genhd.h"
#include "readwrite.h"

extern const unsigned int sector_size_bytes;
extern int device;

const unsigned int partition_offset = 0x1BE;
const unsigned int partition_entry_size = 16;

static int get_extended_sect(char *buf, int *extended_base_sect)
{
        struct partition p;
        int i;
        for (i = 0; i < 4; i++) {
                int offset = partition_offset + partition_entry_size * i;
                memcpy(&p, buf+offset, sizeof(p));
                if (p.sys_ind == 0x05) {
                        break;
                }
        }
        if (i >= 4) { // no extented partition found
                return -1;
        }

        *extended_base_sect = p.start_sect; // start of EBR
        read_sectors(*extended_base_sect, 1, buf);

        return 0;
}

static int get_logical_sect(int partition_number, int extended_base_sect, char *buf, int *logical_base_sect)
{
        struct partition p;
        int index_of_lbr = (partition_number - 4 - 1); // i.e. for partition 5, index_of_lbr = 0

        for (; index_of_lbr > 0; index_of_lbr--) {
                // get the 2nd entry
                int offset = partition_offset + partition_entry_size * (2 - 1);
                memcpy(&p, buf+offset, sizeof(p));
                if (p.start_sect == 0) { // reach the end
                        return -1;
                }

                // get the next EBR sector
                *logical_base_sect = p.start_sect + extended_base_sect;
                read_sectors(*logical_base_sect, 1, buf);
        }
        return 0;
}

// read the partition entry into result,
// read the beginning sector number of the partition into base_sector.
int do_read_partition(char *disk, int partition_number, struct partition *result, int *base_sector)
{
        int offset;
        char buf[sector_size_bytes];

        if (partition_number <= 0) {
                return -1;
        }

        device = open(disk, O_RDONLY);
        if (device < 0) {
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);
        }

        // read MBR
        read_sectors(0, 1, buf);

        // read entries
        if (partition_number <= 4) {
                offset = partition_offset + partition_entry_size * (partition_number - 1);
                if (result && base_sector) {
                        memcpy(result, buf+offset, sizeof(*result));
                        *base_sector = 0;
                }
                close(device);
                return 0;
        }

        int extended_base_sect;
        int ret = get_extended_sect(buf, &extended_base_sect);
        if (ret < 0) {
                close(device);
                return -1;
        }

        // get the correspoding logical sect
        int logical_base_sect = extended_base_sect;
        ret = get_logical_sect(partition_number, extended_base_sect, buf, &logical_base_sect);
        if (ret < 0) {
                close(device);
                return -1;
        }

        // get the 1st entry and print
        offset = partition_offset;

        if (result && base_sector) {
                memcpy(result, buf+offset, sizeof(*result));
                *base_sector = logical_base_sect;
        }

        close(device);
        return 0;
}

int do_print_partition(char *disk, int partition_number)
{
        int base_sector, ret;
        struct partition p;

        ret = do_read_partition(disk, partition_number, &p, &base_sector);
        if (ret < 0) {
                printf("-1\n");
                return -1;
        }

        printf("0x%02x %d %d\n", p.sys_ind, p.start_sect + base_sector, p.nr_sects);
        return 0;
}
