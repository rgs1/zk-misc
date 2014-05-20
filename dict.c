/*
 * a simple, fixed size & thread-safe, dictionary
 */


#include "dict.h"
#include "util.h"

#include <assert.h>
#include <stdlib.h>

void dict_init(dict_t d)
{
  if (pthread_mutex_init(&d->lock, 0)) {
    error(EXIT_SYSTEM_CALL, "Failed to init mutex");
  }
}

dict_t dict_new(int size)
{
  dict_t d = safe_alloc(sizeof(dict));
  d->keys = (dict_key *)safe_alloc(sizeof(dict_key) * size);
  d->size = size;
  d->step_factor = 32;
  dict_init(d);
  return d;
}

/* TODO: free extra key_pair for each dict_key */
void dict_destroy(dict_t d)
{
  assert(d);
  assert(d->keys);
  free(d->keys);
  free(d);
}

static dict_key * get_key(dict_t d, void *key)
{
  int pos = (int)(((long)key / d->step_factor) % d->size);
  return &d->keys[pos];
}

static dict_key_pair * get_key_pair(dict_key *k, void *key)
{
  int i;

  for (i=0; i < k->count; i++)
    if (k->key_pair[i].key == key)
      return &k->key_pair[i];

  return NULL;
}

static void add_key_pair(dict_key *k, void *key, void *value)
{
  dict_key_pair *kp;

  if (k->count < DICT_NUM_KEY_COLLISIONS) {
    kp = &k->key_pair[k->count++];
  } else {
    if (k->extra_count == k->extra_avail) {
      /* add more space */
      
    }
    kp = &k->extra[k->extra_count++];
  }

  kp->key = key;
  kp->value = value;
}

/* This returns:
 *  - the old value, if the key existed
 *  - the new value, if there was no key
 *  - NULL, if the dictionary is full
 */
void *dict_set(dict_t d, void *key, void *value)
{
  void *old = NULL;
  dict_key *k = NULL;
  dict_key_pair *kp = NULL;

  pthread_mutex_lock(&d->lock);

  if (d->count == d->size)
    goto out;

  k = get_key(d, key);
  assert(k);

  kp = get_key_pair(k, key);

  if (kp) {
    old = kp->value;
    kp->value = value;
  } else {
    add_key_pair(k, key, value);
    d->count++;
  }

out:
  pthread_mutex_unlock(&d->lock);
  return old;
}

int dict_count(dict_t d)
{
  return d->count;
}

#ifdef RUN_TESTS

static void test_add(void)
{
  dict_t d = dict_new(10);
  dict_set(d, "hello", "goodbye");
  info("dict has %d keys", dict_count(d));
  assert(dict_count(d) == 1);
}

int main(int argc, char **argv)
{
  run_test("add", &test_add);

  return 0;
}


#endif
