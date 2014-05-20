/*
 * a simple thread-safe list
 */


#include "list.h"
#include "util.h"

#include <assert.h>
#include <stdlib.h>


#define LOCK(l)  pthread_mutex_lock(&l->lock)
#define UNLOCK(l)  pthread_mutex_unlock(&l->lock)

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

out:
  UNLOCK(l);
  return item;
}

void * list_append(list_t l, void *value)
{
  list_item_t item;

  LOCK(l);

  if (l->count == l->size)
    goto out;

  item = get_free_item(l);
  item->value = value;
  item->next = NULL;

  if (l->tail)
    l->tail->next = item;

  l->tail = item;

out:
  UNLOCK(l);
  return item;
}

list_item_t list_get_by_value(list_t l, void *value)
{
  list_item_t item = NULL;

  LOCK(l);

  list_for_each(item, l) {
    if (item->value == value)
      goto out;
  }

out:
  UNLOCK(l);
  return item;
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

void * list_remove_by_value(list_t l, void *value)
{
  return NULL;
}

void * list_remove_by_pos(list_t l, int pos)
{
  return NULL;
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

int main(int argc, char **argv)
{
  run_test("add", &test_add);

  return 0;
}


#endif
