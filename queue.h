#ifndef _QUEUE_H_
#define _QUEUE_H_

#include <pthread.h>

typedef struct {
  void **ptrs;
  int head;
  int tail;
  int count;
  int size;
  pthread_mutex_t lock;
  pthread_cond_t cond;
  void *user_data;
} queue;

typedef queue * queue_t;

queue_t queue_new(int size);
void queue_destroy(queue_t q);
void queue_init(queue_t q);
int queue_add(queue_t q, void *item);
void * queue_remove(queue_t q);
int queue_empty(queue_t q);
int queue_count(queue_t q);
void queue_set_user_data(queue_t q, void *data);
void *queue_get_user_data(queue_t q);

#endif

