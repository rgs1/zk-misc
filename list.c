/*
 * a simple thread-safe list
 */


#include "list.h"
#include "pool.h"
#include "util.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>


static void * list_remove_if_matches(list_t,
                                     int (*)(int, void *, void *),
                                     void *);


void list_init(list_t l)
{
  if (pthread_mutex_init(&l->lock, 0)) {
    error(EXIT_SYSTEM_CALL, "Failed to init mutex");
  }
}

list_t list_new(int size)
{
  list_t l = safe_alloc(sizeof(list));
  l->head = l->tail = NULL;
  l->pool = pool_new(size * sizeof(list_item), sizeof(list_item));
  l->size = size;
  list_init(l);
  return l;
}

void list_resize(list_t l, int new_size)
{
  assert(new_size > l->size);

  pool_resize(l->pool, sizeof(list_item) * new_size);
  l->size = new_size;
}

void list_destroy(list_t l)
{
  assert(l);
  assert(l->pool);
  pool_destroy(l->pool);
  free(l);
}

static list_item_t get_free_item(list_t l)
{
  list_item_t item;

  assert(l->count < l->size);

  item = (list_item_t)pool_get(l->pool);
  l->count++;

  return item;
}

void * list_prepend(list_t l, void *value)
{
  list_item_t item = NULL;

  LOCK(l);

  if (l->count == l->size)
    goto out;

  item = get_free_item(l);
  item->value = value;
  item->next = l->head;
  l->head = item;

  if (l->count == 1)
    l->tail = item;

out:
  UNLOCK(l);
  return item;
}

static void * do_list_append(list_t l, void *value)
{
  list_item_t item = NULL;

  if (l->count == l->size)
    return NULL;

  item = get_free_item(l);
  item->value = value;
  item->next = NULL;

  if (l->tail)
    l->tail->next = item;

  l->tail = item;

  if (l->count == 1)
    l->head = item;

  return item;
}

void * list_append(list_t l, void *value)
{
  list_item_t item = NULL;

  LOCK(l);

  item = do_list_append(l, value);

  UNLOCK(l);

  return item;
}

static void * item_value_transform(list_item_t item)
{
  return item->value;
}

void list_concat(list_t left, list_t right)
{
  list_concat_with_transform(left, right, item_value_transform);
}

void list_concat_with_transform(list_t left,
                                list_t right,
                                void *(*transform)(list_item_t))
{
  list_item_t item = NULL;

  LOCK(right);
  LOCK(left);

  list_for_each_item(item, right) {
    do_list_append(left, transform(item));
  }

  UNLOCK(left);
  UNLOCK(right);
}

void * list_get(list_t l, int pos)
{
  list_item_t item = NULL;
  int i = 0;

  assert(pos < l->count);

  LOCK(l);

  list_for_each_item(item, l) {
    if (i++ == pos)
      goto out;
  }

out:
  UNLOCK(l);
  return item ? item->value : NULL;
}

static int match_by_value(int pos, void *value, void *user_data)
{
  return value == user_data;
}

void * list_remove_by_value(list_t l, void *value)
{
  return list_remove_if_matches(l, match_by_value, value);
}

static int match_by_pos(int pos, void *value, void *user_data)
{
  return pos == (int)(long)user_data;
}

void * list_remove_by_pos(list_t l, int pos)
{
  return list_remove_if_matches(l, match_by_pos, (void *)(long)pos);
}

/* TODO: move value comparison to a matcher func */
int list_contains(list_t l, void *value)
{
  int ret = 0;
  list_item_t item;

  LOCK(l);

  list_for_each_item(item, l) {
    if (item->value == value) {
      ret = 1;
      break;
    }
  }

  UNLOCK(l);

  return ret;
}

static void * list_remove_if_matches(list_t l,
                                     int (*matcher)(int, void *, void *),
                                     void *user_data)
{
  list_item_t prev = l->head;
  list_item_t item;
  list_item_t found = NULL;
  void *rv = NULL;
  int i = 0;

  LOCK(l);

  assert(l->count > 0);

  list_for_each_item(item, l) {
    if (matcher(i, item->value, user_data)) {
      found = item;
      rv = item->value;
      goto out;
    }
    prev = item;
    i++;
  }

out:
  if (found) {
    if (l->head == found) {
      l->head = found->next;
      if (l->count == 1)
        l->tail = NULL;
    } else if (l->tail == found) {
      l->tail = prev;
      l->tail->next = NULL;
    } else {
      prev->next = found->next;
    }

    pool_put(l->pool, found);
    l->count--;
  }
  UNLOCK(l);

  return rv;
}

int list_count(list_t l)
{
  return l->count;
}

int list_full(list_t l)
{
  return l->count == l->size;
}

void list_set_user_data(list_t l, void *data)
{

}

void * list_get_user_data(list_t l)
{
  return NULL;
}


#ifdef RUN_TESTS

static void test_add(void)
{
  list_t l = list_new(10);
  list_prepend(l, "hello");
  list_append(l, "goodbye");
  info("list has %d items", list_count(l));
  assert(list_count(l) == 2);
}

static void test_add_no_space(void)
{
  list_t l = list_new(1);

  assert(list_prepend(l, "hello"));
  assert(list_append(l, "goodbye") == NULL);
  info("list has %d items", list_count(l));
  assert(list_count(l) == 1);
}

static void test_remove(void)
{
  list_t l = list_new(10);

  list_prepend(l, "hello");
  list_append(l, "goodbye");
  info("list has %d items", list_count(l));
  assert(list_count(l) == 2);

  list_remove_by_value(l, "goodbye");
  info("list has %d items, head = %s", list_count(l), l->head->value);
  assert(list_count(l) == 1);

  list_remove_by_value(l, "hello");
  info("list has %d items", list_count(l));
  assert(list_count(l) == 0);

  list_append(l, "florence");
  list_append(l, "tuscany");
  assert(list_count(l) == 2);
  list_remove_by_pos(l, 0);
  info("list has %d items", list_count(l));
  assert(list_count(l) == 1);
  list_remove_by_pos(l, 0);
  info("list has %d items", list_count(l));
  assert(list_count(l) == 0);
}

static void test_get(void)
{
  list_t l = list_new(10);

  list_append(l, "one");
  list_append(l, "two");
  list_append(l, "three");
  info("list has %d items", list_count(l));
  assert(list_count(l) == 3);

  assert(strcmp((const char *)list_get(l, 0), "one") == 0);
  assert(strcmp((const char *)list_get(l, 1), "two") == 0);
  assert(strcmp((const char *)list_get(l, 2), "three") == 0);
}

static void test_resize(void)
{
  list_t l = list_new(2);

  list_append(l, "one");
  list_append(l, "two");
  info("list has %d items", list_count(l));
  assert(list_append(l, "three") == NULL);
  assert(list_count(l) == 2);

  list_resize(l, 4);
  list_append(l, "three");
  list_append(l, "four");
  info("list has %d items", list_count(l));
  assert(list_count(l) == 4);

  assert(strcmp((const char *)list_get(l, 0), "one") == 0);
  assert(strcmp((const char *)list_get(l, 1), "two") == 0);
  assert(strcmp((const char *)list_get(l, 2), "three") == 0);
  assert(strcmp((const char *)list_get(l, 3), "four") == 0);
}

int main(int argc, char **argv)
{
  run_test("add", &test_add);
  run_test("no more space", &test_add_no_space);
  run_test("remove", &test_remove);
  run_test("get by pos", &test_get);
  run_test("resize", &test_resize);

  return 0;
}


#endif
