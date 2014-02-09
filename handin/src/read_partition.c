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

static void close_and_exit()
{
        close(device);
        exit(0);
}

static void get_extended_sect(char *buf, struct partition *p, int *extended_base_sect)
{
        int i;
        for (i = 0; i < 4; i++) {
                int offset = partition_offset + partition_entry_size * i;
                memcpy(p, buf+offset, sizeof(*p));
                if (p->sys_ind == 0x05) {
                        break;
                }
        }
        if (i >= 4) { // no extented partition found
                printf("-1\n");
                return close_and_exit();
        }

        *extended_base_sect = p->start_sect; // start of EBR
        read_sectors(*extended_base_sect, 1, buf);

        return;
}

static void get_logical_sect(int partition_number, int extended_base_sect, char *buf, int *logical_base_sect)
{
        struct partition p;
        int index_of_lbr = (partition_number - 4 - 1); // i.e. for partition 5, index_of_lbr = 0

        for (; index_of_lbr > 0; index_of_lbr--) {
                // get the 2nd entry
                int offset = partition_offset + partition_entry_size * (2 - 1);
                memcpy(&p, buf+offset, sizeof(p));
                if (p.start_sect == 0) { // reach the end
                        printf("-1\n");
                        return close_and_exit();
                }

                // get the next EBR sector
                *logical_base_sect = p.start_sect + extended_base_sect;
                read_sectors(*logical_base_sect, 1, buf);
        }
        return;
}

static void print_entry_info(int base, int offset, char *buf)
{
        struct partition p;

        memcpy(&p, buf+offset, sizeof(p));
        printf("0x%02x %d %d\n", p.sys_ind, p.start_sect + base, p.nr_sects);

        return close_and_exit();
}

void do_read_partition(int partition_number, char *disk)
{
        int offset;
        char buf[sector_size_bytes];
        struct partition p;

        if (partition_number <= 0) {
                printf("-1\n");
                exit(-1);
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
                return print_entry_info(0, offset, buf);
        }

        int extended_base_sect;
        get_extended_sect(buf, &p, &extended_base_sect);

        // get the correspoding logical sect
        int logical_base_sect = extended_base_sect;
        get_logical_sect(partition_number, extended_base_sect, buf, &logical_base_sect);

        // get the 1st entry and print
        offset = partition_offset;
        print_entry_info(logical_base_sect, offset, buf);

        return;
}
