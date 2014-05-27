/*
 * a simple, fixed size & thread-safe, dictionary
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <assert.h>
#include <stdio.h>
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

static int default_key_comparator(void *a, void *b)
{
  return a == b ? 0 : (a < b ? -1 : 1);
}


/* Note:
 *  the step)_factor size usually is the number of bytes between each key
 */
static int default_hash_func(void *key, int size)
{
  return (int)(((long)key / 32) % size);
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
  d->key_comparator = &default_key_comparator;
  d->hash_func = &default_hash_func;
  dict_init(d);
  return d;
}

void dict_set_key_comparator(dict_t d, int (*comparator)(void *, void *))
{
  d->key_comparator = comparator;
}

void dict_set_hash_func(dict_t d, int (*hash_func)(void *, int))
{
  d->hash_func = hash_func;
}

static int strings_cmp(void *a, void *b)
{
  return strcmp((const char *)a, (const char *)b);
}

static int strings_hash(void *key, int size)
{
  const char *s = (const char *)key;
  int sum = 0, i;

  assert(s);

  for (i=0; i < strlen(s); i++)
    sum += (int)s[i];

  return sum % size;
}

void dict_use_string_keys(dict_t d)
{
  dict_set_key_comparator(d, &strings_cmp);
  dict_set_hash_func(d, &strings_hash);
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
  int pos = d->hash_func(key, d->size);
  return d->keys[pos];
}

static dict_key_value_t key_value_for(dict_t d, void *key, list_t *keys)
{
  dict_key_value_t kv;
  list_item_t item;

  *keys = get_keys(d, key);
  assert(*keys);
  list_for_each(item, kv, *keys) {
    if (kv && d->key_comparator(kv->key, key) == 0)
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

  kv = key_value_for(d, key, &keys);

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

  kv = key_value_for(d, key, &keys);
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

  kv = key_value_for(d, key, &keys);
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

  assert(list_contains(keys, "a") > 0);
  assert(list_contains(keys, "b") > 0);

  list_destroy(keys);
}

static void test_string_keys(void)
{
  dict_t d = dict_new(10);

  dict_use_string_keys(d);

  dict_set(d, "hello", "goodbye");
  info("dict has %d keys", dict_count(d));
  assert(dict_count(d) == 1);
  assert(strcmp(dict_get(d, strdup("hello")), "goodbye") == 0);
  dict_set(d, strdup("hello"), "updated");
  assert(dict_count(d) == 1);
  assert(strcmp(dict_get(d, strdup("hello")), "updated") == 0);
}

static char * randstr(void)
{
  static char buf[40];
  sprintf(buf, "abcdefghijklmnopqrstuvwyz0123456789");
  return strdup(strfry(buf));
}

static void test_big_dict(void)
{
  char *k;
  int num_keys = 1 << 13;
  dict_t d = dict_new(num_keys);
  int i;
  list_t keys = list_new(num_keys);
  list_item_t item;

  dict_use_string_keys(d);

  /* add keys */
  for (i=0; i < num_keys; i++) {
    k = randstr();
    dict_set(d, k, k);
    list_append(keys, k);
  }

  info("dict has %d keys", dict_count(d));
  assert(dict_count(d) > 1);

  /* check keys are there */
  list_for_each(item, k, keys) {
    assert(strcmp(dict_get(d, k), k) == 0);
  }
}

int main(int argc, char **argv)
{
  run_test("basic: add, set, get & remove", &test_basic);
  run_test("string keys", &test_string_keys);
  run_test("big dict", &test_big_dict);

  return 0;
}


#endif
