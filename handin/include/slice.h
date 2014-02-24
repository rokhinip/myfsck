#ifndef _SLICE_H
#define _SLICE_H

#include "link_list.h"

typedef struct slice_s {
        void *array;

        int item_size;
        // length and capcity
        int len;
        int cap;
}slice_t;

slice_t *make_slice(int cap, int item_size);
int append(slice_t *s, void *item);
int get(slice_t *s, int i, void *item);
int set(slice_t *s, int i, void *item);
void delete_slice(slice_t *s);
list_t * slice_to_list(slice_t *s);

#endif
