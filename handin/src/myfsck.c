#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "myfsck.h"

const char *optstring = "p:i:";
const char *usage_string = "[-p <partition number>] [-i /path/to/disk/image/]";

void print_usage(char *name) {

        printf("usage: %s %s\n", name, usage_string);
        exit(-1);
}

int main(int argc, char *argv[])
{
        int partition_number, opt;
        char path_to_disk_image[256];

        if (argc < 5) {
                print_usage(argv[0]);
        }

        while ((opt = getopt(argc, argv, optstring)) != -1) {
                switch (opt) {
                case 'p':
                        partition_number = atoi(optarg);
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
        printf("%d %s\n", partition_number, path_to_disk_image);

        return 0;
}
