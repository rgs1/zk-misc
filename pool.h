#ifndef _POOL_H_
#define _POOL_H_

#include <pthread.h>

#include "slab.h"

typedef struct _free_list * _free_list_t;

typedef struct {
  int item_size;
  int size;
  slab_t *slabs;
  int slab_count;
  int slab_curr;
  void **free_list;
  int free_count;
  pthread_mutex_t lock;
  pthread_cond_t cond;
} pool;

typedef pool * pool_t;

void pool_init(pool_t p);
pool_t pool_new(int size, int item_size);
void pool_destroy(pool_t p);
void * pool_get(pool_t p);
void pool_put(pool_t p, void *item);
void pool_resize(pool_t s, int new_size);

#endif
