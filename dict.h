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
  int (*key_comparator)(void *a, void *b); /* 0 if =, -1 if a < b, 1 if a > b */
  int (*hash_func)(void *key, int size);
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
list_t dict_keys(dict_t d);
list_t dict_values(dict_t d);
void * dict_unset(dict_t d, void *key);
int dict_count(dict_t d);
void dict_set_key_comparator(dict_t d, int (*comparator)(void *, void *));
void dict_set_hash_func(dict_t d, int (*hash_func)(void *, int));
void dict_use_string_keys(dict_t d);
void dict_set_user_data(dict_t d, void *data);
void * dict_get_user_data(dict_t q);

#endif


