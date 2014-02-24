#include <errno.h>
#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slice.h"

#define NEW_INSTANCE(ret, structure)                                    \
        if (((ret) = malloc(sizeof(structure))) == NULL) {              \
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);     \
        }

slice_t *make_slice(int cap, int item_size)
{
        slice_t *s;
        NEW_INSTANCE(s, slice_t);
        s->item_size = item_size;
        s->cap = cap;
        s->len = 0;

        s->array = malloc(item_size * cap);
        if (!s->array) {
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);
        }

        return s;
}

int append(slice_t *s, void *item)
{
        if (s->len < s->cap) {
                memcpy(s->array + (s->len*s->item_size), item, s->item_size);
                s->len++;
                return s->len;
        }

        // realloc
        s->array = realloc(s->array, s->item_size * 1024);
        if (!s->array) {
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);
        }
        s->cap += 1024;

        return append(s, item);
}

int get(slice_t *s, int i, void *item)
{
        if (i >= s->len) {
                printf("index too big\n");
                return -1;
        }

        memcpy(item, s->array+i*s->item_size, s->item_size);
        return 0;
}

int set(slice_t *s, int i, void *item)
{
        if (i >= s->len) {
                printf("index too big\n");
                return -1;
        }

        memcpy(s->array+i*s->item_size, item, s->item_size);
        return 0;
}

void delete_slice(slice_t *s)
{
        free(s->array);
        free(s);
}

list_t * slice_to_list(slice_t *s)
{
        void *item = malloc(s->item_size);
        if (!item) {
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);
        }

        list_t *list = ll_new_list(s->item_size);
        for (int i = 0; i < s->len; i++) {
                get(s, i, item);
                ll_append(list, item);
        }
        free(item);

        return list;
}

#ifdef TESTSLICE

int main(int argc, char *argv[])
{
        slice_t *s = make_slice(5, sizeof(int));

        for (int i = 0; i < 10; i++) {
                append(s, &i);
        }

        int item;
        for (int i = 0; i < s->len; i++) {
                get(s, i, &item);
                printf("slice[%d] = %d\n", i, item);
        }
        printf("slice len: %d cap: %d\n", s->len, s->cap);
        delete_slice(s);

        s = make_slice(5, sizeof(int));

        for (int i = 0; i < 10; i++) {
                append(s, &i);
        }

        list_t *list = slice_to_list(s);

        int len = list->len;
        for (int i = 0; i < len; i++) {
                ll_pop(list, &item);
                printf("link_list[%d] = %d\n", i, item);
        }

        ll_delete_list(list);
        delete_slice(s);

        s = make_slice(5, sizeof(int));
        for (int i = 0; i < 10; i++) {
                append(s, &i);
        }

        for (int i = 0; i < 10; i++) {
                set(s, 9-i, &i);
        }
        for (int i = 0; i < s->len; i++) {
                get(s, i, &item);
                printf("slice[%d] = %d\n", i, item);
        }

        delete_slice(s);

        return 0;
}

#endif
