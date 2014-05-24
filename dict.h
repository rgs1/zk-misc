#ifndef _DICT_H_
#define _DICT_H_

#include <pthread.h>

#include "list.h"
#include "pool.h"


typedef struct {
  void *key;
  void *value;
} dict_key_value;

typedef dict_key_value * dict_key_value_t;

typedef struct {
  list_t *keys;
  pool_t pool;
  int count;
  int size;
  int step_factor;  /* usually, the number of bytes between each key */
  pthread_mutex_t lock;
  pthread_cond_t cond;
  void *user_data;
} dict;

typedef dict * dict_t;

dict_t dict_new(int size);
void dict_destroy(dict_t d);
void dict_init(dict_t d);
void * dict_set(dict_t d, void *key, void *value);
void * dict_get(dict_t d, void *key);
void * dict_unset(dict_t d, void *key);
int dict_count(dict_t d);
void dict_set_user_data(dict_t d, void *data);
void * dict_get_user_data(dict_t q);

#endif


