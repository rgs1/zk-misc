#ifndef _LIST_H_
#define _LIST_H_

#include <pthread.h>

typedef struct _list_item list_item;
typedef struct _list_item * list_item_t;

struct _list_item {
  void *value;
  list_item_t next;
};

typedef struct {
  list_item_t head;
  list_item_t tail;
  list_item_t mem;
  list_item_t free;
  int mem_pos;
  int count;
  int size;
  pthread_mutex_t lock;
  pthread_cond_t cond;
} list;

typedef list * list_t;

list_t list_new(int size);
void list_resize(list_t l, int new_size);
void list_destroy(list_t l);
void list_init(list_t l);
void * list_prepend(list_t l, void *value);
void * list_append(list_t l, void *value);
list_item_t list_get_by_value(list_t l, void *value);
list_item_t list_get_by_pos(list_t l, int pos);
void * list_remove_by_value(list_t l, void *value);
void * list_remove_by_pos(list_t l, int pos);
int list_count(list_t l);
void list_set_user_data(list_t l, void *data);
void * list_get_user_data(list_t l);

#define list_for_each(item, l) \
  for (item = (l)->head; item != NULL; item = item->next)

#endif
