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

static inline int compute_rec_len(struct ext2_dir_entry_2 *dir)
{
        return (8 + dir->name_len + 3) / 4 * 4; // hard_code
}

static int modify_dir(struct ext2_dir_entry_2 *dir, int inode_id, char *name, int name_len)
{
        dir->inode = inode_id;
        dir->name_len = name_len;
        dir->file_type = EXT2_FT_DIR;
        strncpy(dir->name, name, name_len);
        dir->rec_len = compute_rec_len(dir);
        return 0;
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

        //print_block_content(block_buf);

        struct ext2_inode *entry = get_inode_entry(pt, inode_id);
        int block_number = entry->i_block[0];

        write_block(pt, block_number, 1, block_buf);

        free(block_buf);
        return 0;
}

int check_self_parent(partition_t *pt, int self_inode, int parent_inode)
{
        int need_write_back = 0;

        struct ext2_dir_entry_2 self_dir;
        struct ext2_dir_entry_2 parent_dir;

        slice_t *s = get_child_dirs(pt, self_inode);
        list_t *list = slice_to_list(s);

        ll_pop(list, &self_dir);
        ll_pop(list, &parent_dir);

        if (parent_dir.inode != parent_inode) {
                need_write_back = 1;
                printf("parent ptr error for inode %d\n", self_inode);
                if (parent_dir.name_len != 2 || strncmp(parent_dir.name, "..", 2) != 0) {
                        ll_push(list, &parent_dir);
                }
                modify_dir(&parent_dir, parent_inode, "..", 2);
        }

        if (self_dir.inode != self_inode) {
                need_write_back = 1;
                printf("self ptr error for inode %d\n", self_inode);
                if (self_dir.name_len != 1 || strncmp(self_dir.name, ".", 1) != 0) {
                        ll_push(list, &self_dir);
                }
                modify_dir(&self_dir, self_inode, ".", 1);
        }

        ll_push(list, &parent_dir);
        ll_push(list, &self_dir);

        if (need_write_back) {
                printf("writing back dir for inode %d\n", self_inode);
                write_dirs(pt, self_inode, list);
        }

        ll_delete_list(list);
        delete_slice(s);

        return 0;
}

int check_dir(partition_t *pt, int inode_id)
{
        struct ext2_dir_entry_2 dir;
        struct ext2_dir_entry_2 self_dir;
        struct ext2_dir_entry_2 parent_dir;

        slice_t *s = get_child_dirs(pt, inode_id);
        list_t *list = slice_to_list(s);

        if (inode_id == 2) { // root
                int need_write_back = 0;

                ll_pop(list, &self_dir);
                ll_pop(list, &parent_dir);

                if (strncmp(parent_dir.name, "..", 2) != 0) {
                        need_write_back = 1;
                        printf("root parent ptr error\n");
                        ll_push(list, &parent_dir);
                        modify_dir(&parent_dir, 2, "..", 2);
                }

                if (strncmp(self_dir.name, ".", 1) != 0) {
                        need_write_back = 1;
                        printf("root self ptr error\n");
                        ll_push(list, &self_dir);
                        modify_dir(&self_dir, 1, ".", 1);
                }

                if (need_write_back) {
                        printf("writing back dir for inode %d\n", 2);
                        write_dirs(pt, 2, list);
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

static int get_blocks_count_for_inodes(partition_t *pt)
{
        int inodes_per_block = get_block_size(pt) / sizeof(struct ext2_inode);
        int inodes_per_group = get_inodes_per_group(pt);
        int blocks_of_inodes_per_group = (inodes_per_group + (inodes_per_block - 1)) / inodes_per_block;
        int total_blocks = blocks_of_inodes_per_group * pt->group_count;

        return total_blocks;
}

static char ** allocate_inode_block(partition_t *pt) {
        int inodes_per_block = get_block_size(pt) / sizeof(struct ext2_inode);
        int inodes_per_group = get_inodes_per_group(pt);
        int blocks_of_inodes_per_group = (inodes_per_group + (inodes_per_block - 1)) / inodes_per_block;
        int total_blocks = blocks_of_inodes_per_group * pt->group_count;

        char ** block_group = malloc(sizeof(char *) * total_blocks);
        if (!block_group) {
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);
        }

        for (int i = 0; i < pt->group_count; i++) {
                int table_id_started = get_inode_table_bid(pt->groups[i]);
                for (int j = 0; j < blocks_of_inodes_per_group; j++) {
                        block_group[i*blocks_of_inodes_per_group+j] = read_block(pt, table_id_started+j, 1);
                }
        }

        return block_group;
}

static int index_to_bid(partition_t *pt, int index)
{
        int inodes_per_block = get_block_size(pt) / sizeof(struct ext2_inode);
        int inodes_per_group = get_inodes_per_group(pt);
        int blocks_of_inodes_per_group = (inodes_per_group + (inodes_per_block - 1)) / inodes_per_block;

        int group_number = index / blocks_of_inodes_per_group;
        int block_offset = index % blocks_of_inodes_per_group;

        int block_start = get_inode_table_bid(pt->groups[group_number]);

        return block_start + block_offset;
}

static int modify_inode_block_array(partition_t *pt, char **block_array,
                                    int *dirty_array, int inode_id, struct ext2_inode *entry)
{
        int inode_entry_per_block = get_block_size(pt) / sizeof(struct ext2_inode);

        int block_index = (inode_id - 1) / inode_entry_per_block;
        int offset_in_block = (inode_id - 1) % inode_entry_per_block * sizeof(struct ext2_inode);

        //struct ext2_inode old_entry;
        //memcpy(&old_entry, block_array[block_index]+offset_in_block, sizeof(struct ext2_inode));
        //printf("old links count %d\n", old_entry.i_links_count);

        memcpy(block_array[block_index]+offset_in_block, entry, sizeof(struct ext2_inode));
        dirty_array[block_index] = 1;

        return 0;
}

static int create_lost_dir(struct ext2_dir_entry_2 *lost_dir, int inode)
{
        lost_dir->inode = inode;
        sprintf(lost_dir->name, "%d", inode);
        lost_dir->file_type = EXT2_FT_REG_FILE;
        lost_dir->name_len = strlen(lost_dir->name);
        lost_dir->rec_len = compute_rec_len(lost_dir);

        return 0;
}

int fix_idle_inodes(partition_t *pt)
{
        int add_lost_found = 0;

        struct ext2_dir_entry_2 lost_dir;
        int lost_found_inode = get_lost_found_inode(pt);
        slice_t *lost_found = get_child_dirs(pt, lost_found_inode);
        int old_last_dir_index = lost_found->len - 1;

        char **block_array = allocate_inode_block(pt);

        int total_blocks = get_blocks_count_for_inodes(pt);
        int *dirty_array = malloc(sizeof(int) * total_blocks);
        if (!dirty_array) {
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);
        }

        // start from root
        for (int i = 2; i < book_size; i++) {
                struct ext2_inode *entry = get_inode_entry(pt, i);
                if (entry->i_links_count == inode_book[i]) {
                        continue;
                }
                printf("found different inode %d: %d count %d\n", i, entry->i_links_count, inode_book[i]);
                if (entry->i_links_count > 0 && inode_book[i] == 0) {
                        // lost and found
                        create_lost_dir(&lost_dir, i);
                        append(lost_found, &lost_dir);
                        add_lost_found = 1;
                }
                entry->i_links_count = inode_book[i];
                modify_inode_block_array(pt, block_array, dirty_array, i, entry);
                // modify entry and store it in block
        }

        for (int i = 0; i < total_blocks; i++) {
                if (dirty_array[i]) {
                        //printf("%d \n", index_to_bid(pt, i));
                        write_block(pt, index_to_bid(pt, i), 1, block_array[i]);
                }
        }

        if (add_lost_found) {
                // modify the rec_len of the last dir
                struct ext2_dir_entry_2 old_last;
                get(lost_found, old_last_dir_index, &old_last);
                old_last.rec_len = compute_rec_len(&old_last);
                set(lost_found, old_last_dir_index, &old_last);

                list_t *list = slice_to_list(lost_found);
                write_dirs(pt, lost_found_inode, list);
                free(list);
        }

        free(lost_found);
        free(dirty_array);

        for (int i = 0; i < total_blocks; i++) {
                free(block_array[i]);
        }
        free(block_array);

        return add_lost_found;
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

        int ret = fix_idle_inodes(pt);
        if (ret != 0) {
                check_dir_ptrs(pt);
                check_inode_ptr(pt);
        }

        return 0;
}
