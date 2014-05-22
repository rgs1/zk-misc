#ifndef _SLAB_H_
#define _SLAB_H_

#include <pthread.h>

typedef struct {
  void *mem;
  int position;
  int size;
  pthread_mutex_t lock;
  pthread_cond_t cond;
} slab;

typedef slab * slab_t;

void slab_init(slab_t s);
slab_t slab_new(int size);
void slab_destroy(slab_t s);
void * slab_get_mem(slab_t s);
void * slab_get_cur(slab_t s);
int slab_get_size(slab_t s);
void slab_update_position(slab_t s, int bytes);
int slab_get_position(slab_t s);
int slab_eof(slab_t s);

#endif
