#include <stdio.h>

#include "disk.h"
#include "link_list.h"
#include "slice.h"
#include "util/partition.h"
#include "util/printer.h"




int breadth_search(list_t* queue, partition_t *pt,
                   int (*func)(partition_t*, int))
{
        int inode_id;
        
        while (queue->len > 0) {
                // pop
                ll_pop(queue, &inode_id);
                
                // do something
                func(pt, inode_id);
                printf("ha\n");
                
                // get child list
                slice_t *children = get_child_inodes(pt, inode_id);

                // add to queue
                int c_id;
                printf("len %d\n", children->len);
                for (int i = 0; i < children->len; i++) {
                        get(children, i, &c_id);
                        printf("cid %d\n", c_id);
                        if (is_dir(pt, c_id)) {
                                ll_append(queue, &c_id);
                        }
                }
        }

        return 0;
}

int print_dir(partition_t *pt, int inode_id)
{
        printf("inode %d\n", inode_id);
        return 0;
}

void print_dirs(partition_t *pt)
{
        list_t *queue = ll_new_list(sizeof(int));

        int root_inode = 2;
        ll_append(queue, &root_inode); // enqueue the root;

        breadth_search(queue, pt, print_dir);
}

