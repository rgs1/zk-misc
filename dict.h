#ifndef _DICT_H_
#define _DICT_H_

#include <pthread.h>


#define DICT_NUM_KEY_COLLISIONS     10

typedef struct {
  void *key;
  void *value;
} dict_key_pair;

typedef struct {
  dict_key_pair key_pair[DICT_NUM_KEY_COLLISIONS];
  int count;
  dict_key_pair *extra;
  int extra_avail;
  int extra_count;
} dict_key;

typedef struct {
  dict_key *keys;
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
void * dict_remove(dict_t d, void *key);
int dict_count(dict_t d);
void dict_set_user_data(dict_t d, void *data);
void * dict_get_user_data(dict_t q);

#endif


