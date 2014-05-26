/*
 * a simple, fixed size & thread-safe, dictionary
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "dict.h"
#include "list.h"
#include "util.h"

#define DICT_KEY_COLLISIONS     10

#define keys_for_each_list(dict, i, list)                \
  for (i = 0, list = dict->keys[0]; i < dict->size; i++, list = dict->keys[i])


void dict_init(dict_t d)
{
  INIT_LOCK(d);
}

dict_t dict_new(int size)
{
  int i;
  dict_t d = safe_alloc(sizeof(dict));

  /* init lists */
  d->keys = (list_t *)safe_alloc(sizeof(list_t) * size);
  for (i=0; i < size; i++)
    d->keys[i] = list_new(DICT_KEY_COLLISIONS);

  d->pool = pool_new(sizeof(dict_key_value) * size, sizeof(dict_key_value));
  d->size = size;
  d->step_factor = 32;
  dict_init(d);
  return d;
}

void dict_destroy(dict_t d)
{
  int i;

  assert(d);
  assert(d->keys);

  for (i=0; i < d->size; i++)
    list_destroy(d->keys[i]);

  free(d->keys);
  free(d);
}

static list_t get_keys(dict_t d, void *key)
{
  int pos = (int)(((long)key / d->step_factor) % d->size);
  return d->keys[pos];
}

static dict_key_value_t get_key_value(list_t keys, void *key)
{
  dict_key_value_t kv;
  int i;

  for (i=0; i < list_count(keys); i++) {
    kv = list_get(keys, i);
    if (kv && kv->key == key)
      return kv;
  }

  return NULL;
}

static void add_key_value(list_t keys, dict_key_value_t kv)
{
  if (list_full(keys))
    list_resize(keys, list_count(keys) * 2);
  list_append(keys, kv);
}

static void * remove_key_value(list_t keys, dict_key_value_t kv)
{
  return list_remove_by_value(keys, kv);
}

/* This returns:
 *  - the old value, if the key existed
 *  - the new value, if there was no key
 *  - NULL, if the dictionary is full
 */
void *dict_set(dict_t d, void *key, void *value)
{
  void *old = NULL;
  list_t keys = NULL;
  dict_key_value_t kv = NULL;

  LOCK(d);

  if (d->count == d->size)
    goto out;

  keys = get_keys(d, key);
  assert(keys);
  kv = get_key_value(keys, key);

  if (kv) {
    old = kv->value;
    kv->value = value;
  } else {
    kv = pool_get(d->pool);
    kv->key = key;
    old = kv->value = value;
    add_key_value(keys, kv);
    d->count++;
  }

out:
  UNLOCK(d);
  return old;
}


/* the value associated to the key, or NULL */
void * dict_get(dict_t d, void *key)
{
  list_t keys = NULL;
  dict_key_value_t kv = NULL;
  void * value = NULL;

  LOCK(d);

  keys = get_keys(d, key);
  assert(keys);
  kv = get_key_value(keys, key);
  if (kv)
    value = kv->value;

  UNLOCK(d);

  return value;
}

void * dict_unset(dict_t d, void *key)
{
  list_t keys = NULL;
  dict_key_value_t kv = NULL;
  void * value = NULL;

  LOCK(d);

  keys = get_keys(d, key);
  assert(keys);
  kv = get_key_value(keys, key);
  if (kv) {
    value = kv->value;
    remove_key_value(keys, kv);
    d->count--;
  }

  UNLOCK(d);

  return value;
}

static void * key_transform(list_item_t item)
{
  dict_key_value_t kv = (dict_key_value_t)item->value;
  return kv->key;
}

list_t dict_keys(dict_t d)
{
  list_t keys;
  list_t collision_list;
  int i;

  LOCK(d);
  keys = list_new(d->count);
  keys_for_each_list(d, i, collision_list) {
    list_concat_with_transform(keys, collision_list, key_transform);
  }
  UNLOCK(d);

  return keys;
}

list_t dict_values(dict_t d)
{
  return NULL;
}

int dict_count(dict_t d)
{
  return d->count;
}

#ifdef RUN_TESTS

static void test_basic(void)
{
  dict_t d = dict_new(10);
  list_t keys;

  dict_set(d, "hello", "goodbye");

  info("dict has %d keys", dict_count(d));
  assert(dict_count(d) == 1);
  assert(strcmp(dict_get(d, "hello"), "goodbye") == 0);
  assert(dict_get(d, "nokey") == NULL);

  dict_set(d, "hello", "updated");

  info("dict has %d keys", dict_count(d));
  assert(dict_count(d) == 1);
  assert(strcmp(dict_get(d, "hello"), "updated") == 0);

  dict_unset(d, "hello");

  info("dict has %d keys", dict_count(d));
  assert(dict_count(d) == 0);
  assert(dict_get(d, "hello") == NULL);

  dict_set(d, "a", "1");
  dict_set(d, "b", "2");
  keys = dict_keys(d);

  info("dict has %d keys", dict_count(d));
  assert(strcmp(list_get(keys, 0), "a") == 0);
  assert(strcmp(list_get(keys, 1), "b") == 0);

  list_destroy(keys);
}

int main(int argc, char **argv)
{
  run_test("basic: add, set, get & remove", &test_basic);

  return 0;
}


#endif
