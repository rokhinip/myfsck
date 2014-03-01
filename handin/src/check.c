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

#include "readwrite.h"

#define MAP_UNIT_SIZE 8
#define SET_BIT(map, offset) ((map)[((offset)-1) / MAP_UNIT_SIZE] = (map)[((offset)-1) / MAP_UNIT_SIZE] | (0x1 << (((offset)-1) % MAP_UNIT_SIZE)))
#define CLR_BIT(map, offset) ((map)[((offset)-1) / MAP_UNIT_SIZE] = (map)[((offset)-1) / MAP_UNIT_SIZE] & ~(0x1 << (((offset)-1) % MAP_UNIT_SIZE)))

#define GET_BIT(map, offset) (((map)[((offset)-1) / MAP_UNIT_SIZE] >> (((offset)-1) % MAP_UNIT_SIZE)) & 0x1)

static int *inode_book;
static int book_size;

static char *block_bmap;
static int block_num;

extern int pass;

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

static int delete_other_parent(partition_t *pt, int child_inode, int parent_inode)
{
        if (parent_inode > pt->super_block->s_inodes_count) {
                return -1;
        }
        int modified = 0;
        slice_t *s = get_child_dirs(pt, parent_inode);
        list_t *list = slice_to_list(s);
        list_t *modified_list = ll_new_list(list->item_size);

        while (list->len > 0) {
                struct ext2_dir_entry_2 dir;
                ll_pop(list, &dir);
                if (dir.inode == child_inode) {
                        modified = 1;
                        continue;
                }
                ll_append(modified_list, &dir);
        }

        if (modified) {
                // write
                //printf("write\n");
                //printf("child %d parent %d\n", child_inode, parent_inode);
                write_dirs(pt, parent_inode, modified_list);
                //exit(0);
        }

        delete_slice(s);
        ll_delete_list(list);
        ll_delete_list(modified_list);

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
                if (pass == 1) {
                        printf("parent ptr error for inode %d, should point to %d, found %d\n", self_inode, parent_inode, parent_dir.inode);
                }
                if (parent_dir.name_len != 2 || strncmp(parent_dir.name, "..", 2) != 0) {
                        ll_push(list, &parent_dir);
                }
                delete_other_parent(pt, self_inode, parent_dir.inode);
                modify_dir(&parent_dir, parent_inode, "..", 2);
        }

        if (self_dir.inode != self_inode) {
                need_write_back = 1;
                if (pass == 1) {
                        printf("self ptr error for inode %d\n", self_inode);
                }
                if (self_dir.name_len != 1 || strncmp(self_dir.name, ".", 1) != 0) {
                        ll_push(list, &self_dir);
                }
                modify_dir(&self_dir, self_inode, ".", 1);
        }

        ll_push(list, &parent_dir);
        ll_push(list, &self_dir);

        if (need_write_back) {
                write_dirs(pt, self_inode, list);
                if (pass == 1) {
                        printf("fixed\n");
                }
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
                        if (pass == 1) {
                                printf("root parent ptr error\n");
                        }

                        ll_push(list, &parent_dir);
                        modify_dir(&parent_dir, 2, "..", 2);
                }

                if (strncmp(self_dir.name, ".", 1) != 0) {
                        need_write_back = 1;
                        if (pass == 1) {
                                printf("root self ptr error\n");
                        }

                        ll_push(list, &self_dir);
                        modify_dir(&self_dir, 1, ".", 1);
                }

                if (need_write_back) {
                        write_dirs(pt, 2, list);
                        if (pass == 1) {
                                printf("fixed\n");
                        }
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

        delete_slice(s);
        return 0;
}

void check_dir_ptrs(partition_t *pt)
{
        if (pass == 1) {
                printf("Pass %d: Checking directory structure\n", pass);
        }

        list_t *queue = ll_new_list(sizeof(int));

        int root_inode = 2;
        ll_append(queue, &root_inode); // enqueue the root;

        breadth_search(queue, pt, check_dir);

        ll_delete_list(queue);
}

int calloc_inode_book(partition_t *pt)
{
        book_size = pt->super_block->s_inodes_count + 1;
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
                if (inode_id >= book_size) {
                        continue;
                }
                inode_book[inode_id]++;
        }
        delete_slice(s);
        return 0;
}

// group id starts from 0
//static int get_blocks_count_for_inodes_in_group(partition_t *pt, int group_id)
//{
//        int inodes_in_group;
//        int inodes_per_block = get_block_size(pt) / sizeof(struct ext2_inode);
//
//        int count = pt->super_block->s_inodes_count / pt->super_block->s_inodes_per_group / (group_id + 1);
//        printf("pt->super %d pt->per goup %d id %d\n, count", pt->super_block->s_inodes_count, pt->super_block->s_inodes_per_group, group_id);
//        if (count > 0) {
//                inodes_in_group = pt->super_block->s_inodes_per_group;
//        } else {
//                inodes_in_group = pt->super_block->s_inodes_count % pt->super_block->s_inodes_per_group;
//        }
//
//        int blocks_of_inodes_in_group = (inodes_in_group + (inodes_per_block - 1)) / inodes_per_block;
//
//        return blocks_of_inodes_in_group;
//}

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

static inline int get_file_type(int imode)
{
        if ((imode & EXT2_S_IFSOCK) == EXT2_S_IFSOCK) {
                return EXT2_FT_SOCK;
        }
        if ((imode & EXT2_S_IFLNK) == EXT2_S_IFLNK) {
                return EXT2_FT_SYMLINK;
        }
        if ((imode & EXT2_S_IFREG) == EXT2_S_IFREG) {
                return EXT2_FT_REG_FILE;
        }
        if ((imode & EXT2_S_IFBLK) == EXT2_S_IFBLK) {
                return EXT2_FT_BLKDEV;
        }
        if ((imode & EXT2_S_IFDIR) == EXT2_S_IFDIR) {
                return EXT2_FT_DIR;
        }
        if ((imode & EXT2_S_IFCHR) == EXT2_S_IFCHR) {
                return EXT2_FT_CHRDEV;
        }
        if ((imode & EXT2_S_IFIFO) == EXT2_S_IFIFO) {
                return EXT2_FT_FIFO;
        }
        return EXT2_FT_UNKNOWN;
}

static int create_lost_dir(partition_t *pt, struct ext2_dir_entry_2 *lost_dir, int inode)
{
        struct ext2_inode *entry = get_inode_entry(pt, inode);

        lost_dir->inode = inode;
        sprintf(lost_dir->name, "%d", inode);
        lost_dir->name_len = strlen(lost_dir->name);
        lost_dir->rec_len = compute_rec_len(lost_dir);
        lost_dir->file_type = get_file_type(entry->i_mode);

        return 0;
}

int fix_idle_inodes(partition_t *pt)
{
        pass++;
        if (pass == 3) {
                printf("Pass 3: Checking directory connectivity\n");
        }

        int add_lost_found = 0;

        struct ext2_dir_entry_2 lost_dir;
        int lost_found_inode = get_lost_found_inode(pt);
        slice_t *lost_found = get_child_dirs(pt, lost_found_inode);
        int old_last_dir_index = lost_found->len - 1;

        int inodes_per_block = get_block_size(pt) / sizeof(struct ext2_inode);
        int inodes_per_group = get_inodes_per_group(pt);
        int blocks_of_inodes_per_group = (inodes_per_group + (inodes_per_block - 1)) / inodes_per_block;
        int total_blocks = blocks_of_inodes_per_group * pt->group_count;

        char **block_array = allocate_inode_block(pt);

        int *dirty_array = calloc(1, sizeof(int) * total_blocks);
        if (!dirty_array) {
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);
        }

        // start from root
        for (int i = 2; i < book_size; i++) {
                struct ext2_inode *entry = get_inode_entry(pt, i);
                if (entry->i_links_count == inode_book[i]) {
                        continue;
                }
                if (pass == 3) {
                        printf("Inode %d ref count is %d, should be %d.\n", i, entry->i_links_count, inode_book[i]);
                }
                if (entry->i_links_count > 0 && inode_book[i] == 0) {
                        // lost and found
                        printf("Unconnected directory inode %d\n", i);
                        create_lost_dir(pt, &lost_dir, i);
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
                ll_delete_list(list);
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
        if (pass == 2) {
                printf("Pass 2: Checking directory connectivity\n");
        }

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

//
// check block allocation bitmap
//
static int alloc_block_bitmap(partition_t *pt)
{
        block_num = get_block_size(pt) * pt->group_count * MAP_UNIT_SIZE;
        block_bmap = (char *)calloc(sizeof(char), block_num / MAP_UNIT_SIZE);
        if (!block_bmap) {
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);
        }
        return 0;
}

static int mark_child_blocks_in_book(partition_t *pt, int inode)
{
        int i, j;
        int inode_id, block_id;
        slice_t *child_slice, *blocks;

        child_slice = get_child_inodes(pt, inode);
        for (i = 0; i < child_slice->len; i++) {
                get(child_slice, i, &inode_id);

                //printf("child inode_id %d\n", inode_id);
                if (inode_id > book_size) {
                        continue;
                }

                if (is_symbol(pt, inode_id)) {
                        // do not count symbolic link
                        continue;
                }

                blocks = get_allocated_blocks(pt, inode_id);

                for (j = 0; j < blocks->len; j++) {
                        get(blocks, j, &block_id);
                        SET_BIT(block_bmap, block_id);
                }
                delete_slice(blocks);
        }

        delete_slice(child_slice);
        return 0;
}

static int is_pre_allocated(partition_t *pt, int id)
{
        int inodes_size = get_inodes_per_group(pt) *  sizeof(struct ext2_inode);
        int inodes_blocks = (inodes_size + get_block_size(pt) - 1) / get_block_size(pt);

        for (int i = 0; i < pt->group_count; i++) {
                int start = i * get_blocks_per_group(pt);
                int inodes_table_start = get_inode_table_bid(pt->groups[i]);
                int end = inodes_table_start + inodes_blocks;
                if (id >= start && id < end) {
                        return 1;
                }
        }
        return 0;
}

static inline void fix_bit(int i, int v)
{
        if (v == 0) {
                CLR_BIT(block_bmap, i);
                printf("after clear %d\n", GET_BIT(block_bmap, i));
                exit(0);
                return;
        }
        SET_BIT(block_bmap, i);
}

static int fix_block_bitmap(partition_t *pt)
{
        int changed = 0;
        for (int i = 1; i <= block_num; i++) {
                if (GET_BIT(block_bmap, i) != block_allocated(pt, i)) {
                        if (is_pre_allocated(pt, i)) {
                                SET_BIT(block_bmap, i);
                                continue;
                        }

                        if (i >= pt->super_block->s_blocks_count) {
                                fix_bit(i, block_allocated(pt, i));
                                continue;
                        }
                        changed = 1;
                        if (GET_BIT(block_bmap, i) == 1) {
                                printf("Block bitmap differences +%d\n", i);
                        } else {
                                printf("Block bitmap differences -%d\n", i);
                        }
                }
        }

        //int total = 1;
        //for (int i = 1; i <= block_num; i++) {
        //        if (GET_BIT(block_bmap, i) == 1) {
        //                total++;
        //        }
        //}
        //printf("total used %d / %d\n", total, block_num);
        //printf("reserved blocks %d / %d\n", pt->super_block->s_r_blocks_count, pt->super_block->s_blocks_count);
        //
        //int total_free = get_free_blocks_count(pt->groups[0]) + get_free_blocks_count(pt->groups[1]) + get_free_blocks_count(pt->groups[2]);
        //printf("tottttal block true %d\n", block_num);
        //printf("total used from desc %d\n", block_num - total_free);

        // write back the bitmap
        if (changed) {
                for (int i = 0; i < pt->group_count; i++) {
                        int bitmap_block_start = get_block_bitmap_bid(pt->groups[i]);
                        write_block(pt, bitmap_block_start, 1, block_bmap+i*get_block_size(pt));
                }
        }
        return 0;
}

int check_block_bitmap(partition_t *pt)
{
        printf("Pass 4: Checking group summary information\n");

        alloc_block_bitmap(pt);

        list_t *queue = ll_new_list(sizeof(int));

        int root_inode = 2;
        slice_t *blocks = get_blocks(pt, 2);
        for (int j = 0; j < blocks->len; j++) {
                int block_id;
                get(blocks, j, &block_id);
                SET_BIT(block_bmap, block_id);
        }

        ll_append(queue, &root_inode); // enqueue the root;

        breadth_search(queue, pt, mark_child_blocks_in_book);

        fix_block_bitmap(pt);

        ll_delete_list(queue);
        delete_slice(blocks);

        return 0;
}

int do_check(partition_t *pt)
{
        pass++;
        check_dir_ptrs(pt);

        pass++;
        check_inode_ptr(pt);

        pass++;
        check_block_bitmap(pt);

        return 0;
}
