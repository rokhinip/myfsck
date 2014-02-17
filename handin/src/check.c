#include <errno.h>
#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disk.h"
#include "link_list.h"
#include "slice.h"
#include "util/partition.h"
#include "util/printer.h"

static int *inode_book;
static int book_size;

int breadth_search(list_t* queue, partition_t *pt,
                   int (*func)(partition_t*, int))
{
        int inode_id;

        while (queue->len > 0) {
                // pop
                ll_pop(queue, &inode_id);

                // do something
                func(pt, inode_id);

                //if (inode_id == 0) { // delim
                //        if (queue->len == 0) {
                //                break;
                //        }
                //        ll_append(queue, &inode_id);
                //        continue;
                //}

                // get child list
                slice_t *children = get_child_inodes(pt, inode_id);

                // add to queue
                int c_id;
                for (int i = 2; i < children->len; i++) {
                        get(children, i, &c_id);
                        if (is_dir(pt, c_id)) {
                                ll_append(queue, &c_id);
                        }
                }
                delete_slice(children);
        }

        return 0;
}

int print_dir(partition_t *pt, int inode_id)
{
        printf("inode %d\n", inode_id);
        return 0;
        if (inode_id == 0) {
                printf("\n");
                return 0;
        }
        print_child_dirs(pt, inode_id);

        return 0;
}

void print_dirs(partition_t *pt)
{
        list_t *queue = ll_new_list(sizeof(int));

        int root_inode = 2;
        ll_append(queue, &root_inode); // enqueue the root;

        //int delim = 0;
        //ll_append(queue, &delim); // enqueue the delim;

        breadth_search(queue, pt, print_dir);

        ll_delete_list(queue);
}

int check_self_parent(partition_t *pt, int self_inode, int parent_inode)
{
        struct ext2_dir_entry_2 dir;

        slice_t *s = get_child_dirs(pt, self_inode);

        get(s, 0, &dir);
        if (dir.inode != self_inode) {
                printf("self ptr error\n");

                if (dir.name_len == 1 && strncmp(dir.name, ".", 1) == 0) {
                        // easy to fix
                        printf("easy to fix\n");
                } else {
                        printf("missing self ptr\n");
                }
        }

        get(s, 1, &dir);
        if (dir.inode != parent_inode) {
                if (dir.name_len == 2 && strncmp(dir.name, "..", 2) == 0) {
                        // easy to fix
                        printf("easy to fix\n");
                } else {
                        printf("missing parent ptr\n");
                        struct ext2_inode *entry = get_inode_entry(pt, self_inode);
                        int block_id = entry->i_block[0];
                        list_dir_in_block(pt, block_id);
                        printf("self_inode %d, parent_inode %d\n", self_inode, parent_inode);
                        printf("dir inode %d\n", dir.inode);
                        printf("parent ptr error\n");
                        exit(0);
                }

        }

        free(s);
        return 0;
}

int check_dir(partition_t *pt, int inode_id)
{
        struct ext2_dir_entry_2 dir;

        slice_t *s = get_child_dirs(pt, inode_id);

        if (inode_id == 2) { // root
                get(s, 0, &dir);
                if (strncmp(dir.name, ".", 1) != 0) {
                        printf("root self ptr error\n");
                        return 0;
                }

                get(s, 1, &dir);
                if (strncmp(dir.name, "..", 2) != 0) {
                        printf("root parent ptr error\n");
                        return 0;
                }
        }

        get(s, 0, &dir);
        int parent_inode = dir.inode;
        for (int i = 2; i < s->len; i++) {
                get(s, i, &dir);
                if (is_dir(pt, dir.inode)) {
                        check_self_parent(pt, dir.inode, parent_inode);
                }
        }

        free(s);
        return 0;
}

void check_dir_ptrs(partition_t *pt)
{
        list_t *queue = ll_new_list(sizeof(int));

        int root_inode = 2;
        ll_append(queue, &root_inode); // enqueue the root;

        breadth_search(queue, pt, check_dir);

        ll_delete_list(queue);
}

// assume that we can fit into one block
int write_dirs(partition_t *pt, int inode_id, list_t *list)
{
        int block_size = get_block_size(pt);

        char *block_buf = calloc(1, block_size*2);
        if (!block_buf) {
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);
        }

        int offset = 0;
        int len = list->len;
        struct ext2_dir_entry_2 dir;
        for (int i = 0; i < len-1; i++) {
                ll_pop(list, &dir);
                memcpy(block_buf+offset, &dir, dir.rec_len);
                offset += dir.rec_len;
        }

        ll_pop(list, &dir);
        if ((dir.name_len+8+3)/4*4 + offset > block_size) {
                printf("warning: more than one block\n");
                return 0;
        }
        dir.rec_len = block_size - offset;
        memcpy(block_buf+offset, &dir, dir.rec_len);

        print_block_content(block_buf);

        struct ext2_inode *entry = get_inode_entry(pt, inode_id);
        int block_number = entry->i_block[0];

        write_block(pt, block_number, 1, block_buf);

        free(block_buf);
        return 0;
}

int calloc_inode_book(partition_t *pt)
{
        book_size = get_inodes_per_group(pt)*(pt->group_count) + 1;
        inode_book = calloc(1, sizeof(int) * book_size);
        if (!inode_book) {
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);
        }

        return 0;
}

int mark_child_inodes_in_book(partition_t *pt, int inode)
{
        slice_t *s = get_child_inodes(pt, inode);
        for (int i = 0; i < s->len; i++) {
                int inode_id;
                get(s, i, &inode_id);
                inode_book[inode_id]++;
        }
        return 0;
}

int fix_idle_inodes(partition_t *pt)
{
        // start from root
        for (int i = 2; i < book_size; i++) {
                struct ext2_inode *entry = get_inode_entry(pt, i);
                if (entry->i_links_count == inode_book[i]) {
                        continue;
                }
                printf("found different inode %d: %d count %d\n", i, entry->i_links_count, inode_book[i]);
        }
        return 0;
}

int check_inode_ptr(partition_t *pt)
{
        calloc_inode_book(pt);

        list_t *queue = ll_new_list(sizeof(int));

        int root_inode = 2;
        ll_append(queue, &root_inode); // enqueue the root;

        breadth_search(queue, pt, mark_child_inodes_in_book);

        ll_delete_list(queue);

        //for (int i = 0; i < book_size; i++) {
        //        if (inode_book[i] != 0) {
        //                printf("inode %d\n", i);
        //        }
        //}

        fix_idle_inodes(pt);

        return 0;
}
