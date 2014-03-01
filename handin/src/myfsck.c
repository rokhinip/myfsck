#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "myfsck.h"
#include "check.h"
#include "disk.h"
#include "util/partition.h"
#include "util/printer.h"

const char *optstring = "p:f:i:";
const char *usage_strings[] = {"[-p <partition number>]",
                               "[-f <partition number>]",
                               "[-i /path/to/disk/image/]"};

int pass = 0;

void print_usage(char *name)
{
        printf("usage: %s ", name);
        for (int i = 0; i < sizeof(usage_strings) / sizeof(char*); i++) {
                printf("%s ", usage_strings[i]);
        }
        printf("\n");
        exit(-1);
}

int main(int argc, char *argv[])
{
        int read_partition = 0;
        int fix_partition = 0;

        int fix_partition_number;
        int partition_number, opt;
        char path_to_disk_image[256];

        disk_t disk;

        if (argc < 5) {
                print_usage(argv[0]);
        }

        while ((opt = getopt(argc, argv, optstring)) != -1) {
                switch (opt) {
                case 'p':
                        partition_number = atoi(optarg);
                        read_partition = 1;
                        break;
                case 'i':
                        if (strlen(optarg) > 256) {
                                printf("path too long!\n");
                                print_usage(argv[0]);
                        }
                        strncpy(path_to_disk_image, optarg, sizeof(path_to_disk_image));
                        break;
                case 'f':
                        fix_partition_number = atoi(optarg);
                        if (fix_partition_number < 0) {
                                printf("wrong partition number %d\n", fix_partition_number);
                                return -1;
                        }
                        fix_partition = 1;
                        break;
                }
        }

        // open the disk
        open_disk(path_to_disk_image, &disk);
        // part I
        if (read_partition) {
                print_partitions(&disk, partition_number);
        }

        // part II
        //print_part2(&disk);

        // part III

        if (!fix_partition) {
                return 0;
        }

        if (fix_partition_number == 0) {
                for (int i = 0; i < disk.partition_count; i++) {
                        pass = 0;
                        if (is_ext2_partition(disk.partitions[i])) {
                                do_check(disk.partitions[i]);
                        }
                }
                return 0;
        }

        if (!is_ext2_partition(disk.partitions[fix_partition_number-1])) {
                printf("Trying to run fsck on an invalid partition\n");
                return -1;
        }

        pass = 0;
        do_check(disk.partitions[fix_partition_number-1]);

        return 0;
}
