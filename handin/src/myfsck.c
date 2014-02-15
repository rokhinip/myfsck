#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "myfsck.h"
#include "disk.h"

const char *optstring = "p:f:i:";
const char *usage_strings[] = {"[-p <partition number>]",
                               "[-f <partition number>]",
                               "[-i /path/to/disk/image/]"};

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
        char read_partition = 0;
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
                }
        }

        // open the disk
        open_disk(path_to_disk_image, &disk);
        // part I
        if (read_partition) {
                print_partitions(&disk, partition_number);
        }

        // part II
        //print_all_groups_desc(disk);
        //printf("\n");
        //verify_all_blocks_allocated(disk);
        //verify_all_inodes_allocated(disk);
        printf("is root inode a dir? %d\n", is_dir(disk.partitions[0], 2));
        printf("is root inode allocated? %d\n", inode_allocated(disk.partitions[0], 2));
        //print_and_verify_partition_info(path_to_disk_image, partition_number, 0);
        struct ext2_dir_entry_2 dir;

        get_dir(disk.partitions[0], 2, &dir);
        //print_dir_info(&dir);

        //print_ls(disk.partitions[0], &dir);

        int inode = search_file(disk.partitions[0], "/lions", &dir);
        printf("inode for %s is %d\n", "/lions", inode);
        
        inode = search_file(disk.partitions[0], "/lions/tigers/bears/ohmy.txt", &dir);
        printf("inode for %s is %d\n", "/lions/tigers/bears/ohmy.txt", dir.inode);
        print_dir_info(&dir);
        
        verify_file_block_allocated(disk.partitions[0], dir.inode);

        inode = search_file(disk.partitions[0], "/oz/tornado/dorothy", &dir);
        printf("inode for %s is %d\n", "/oz/tornado/dorothy", inode);
        print_dir_info(&dir);

        inode = search_file(disk.partitions[0], "/oz/tornado/glinda", &dir);
        printf("inode for %s is %d\n", "/oz/tornado/glinda", inode);
        print_dir_info(&dir);

        char path[256];
        get_symbolic_path(disk.partitions[0], inode, path);
        printf("target dir for glinda: %s\n", path);

        //print_ls(disk.partitions[0], "/");
        
        return 0;
}
