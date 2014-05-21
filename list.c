/*
 * a simple thread-safe list
 */


#include "list.h"
#include "util.h"

#include <assert.h>
#include <stdlib.h>


#define LOCK(l)  pthread_mutex_lock(&l->lock)
#define UNLOCK(l)  pthread_mutex_unlock(&l->lock)

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
  l->mem = safe_alloc(sizeof(list_item) * size);
  l->free = NULL;
  l->size = size;
  list_init(l);
  return l;
}

void list_resize(list_t l, int new_size)
{
  
}

void list_destroy(list_t l)
{
  assert(l);
  assert(l->mem);
  free(l->mem);
  free(l);
}

static list_item_t get_free_item(list_t l)
{
  list_item_t item;

  assert(l->count < l->size);

  if (l->mem_pos < l->size) {
    item = &l->mem[l->mem_pos++];
  } else {
    item = l->free;
    l->free = l->free->next;
  }

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

void * list_append(list_t l, void *value)
{
  list_item_t item = NULL;

  LOCK(l);

  if (l->count == l->size)
    goto out;

  item = get_free_item(l);
  item->value = value;
  item->next = NULL;

  if (l->tail)
    l->tail->next = item;

  l->tail = item;

  if (l->count == 1)
    l->head = item;

out:
  UNLOCK(l);
  return item;
}

list_item_t list_get_by_value(list_t l, void *value)
{
  list_item_t item = NULL;
  list_item_t found = NULL;

  LOCK(l);

  list_for_each(item, l) {
    if (item->value == value) {
      found = item;
      goto out;
    }
  }

out:
  UNLOCK(l);
  return found;
}

list_item_t list_get_by_pos(list_t l, int pos)
{
  list_item_t item;
  int i = 0;

  assert(pos < l->count);

  LOCK(l);

  list_for_each(item, l) {
    if (i++ == pos)
      goto out;
  }

out:
  UNLOCK(l);
  return item;
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
  info("pos = %d", (int)(long)user_data);
  return pos == (int)(long)user_data;
}

void * list_remove_by_pos(list_t l, int pos)
{
  return list_remove_if_matches(l, match_by_pos, (void *)(long)pos);
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

  list_for_each(item, l) {
    info("pos = %d", (int)(long)user_data);
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

    found->next = l->free;
    l->free = found;
    l->count--;
  }
  UNLOCK(l);

  return rv;
}

int list_count(list_t l)
{
  return l->count;
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

int main(int argc, char **argv)
{
  run_test("add", &test_add);
  run_test("no more space", &test_add_no_space);
  run_test("remove", &test_remove);
  /* run_test("find by value", &test_get_by_value); */
  /* run_test("find by pos", &test_get_by_pos); */
  /* run_test("resize", &test_resize); */

  return 0;
}


#endif
