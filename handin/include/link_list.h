#ifndef _LINK_LIST_H
#define _LINK_LIST_H



typedef struct node_s {
        struct node_s *prev;
        struct node_s *next;
        void *item;
}node_t;


typedef struct list_s {
        node_t *head;
        node_t *tail;
        int item_size;
        int len;
}list_t;

list_t * ll_new_list(int item_size);
int ll_append(list_t *list, void *item);
int ll_remove(list_t *list, void *item);
int ll_push(list_t *list, void *item);
int ll_pop(list_t *list, void *item);
int ll_delete_list(list_t *list);

#endif
