/* a memory pool manager */

#include <assert.h>
#include <stdlib.h>

#include "pool.h"
#include "slab.h"
#include "util.h"

void pool_init(pool_t p)
{
  INIT_LOCK(p);
}

static void add_slab(pool_t p, int size)
{
  int index = p->slab_count++;
  size_t old = sizeof(slab_t) * index;
  size_t new = sizeof(slab_t) * p->slab_count;

  p->slabs = safe_realloc(p->slabs, old, new);
  p->slabs[index] = slab_new(size);

  /* update free list */
  old = sizeof(void *) * index;
  new = sizeof(void *) * (p->size / p->item_size);
  p->free_list = safe_realloc(p->free_list, old, new);
}

pool_t pool_new(int size, int item_size)
{
  pool_t p = safe_alloc(sizeof(pool));
  pool_init(p);
  p->size = size;
  p->item_size = item_size;
  add_slab(p, size);
  return p;
}

void pool_destroy(pool_t p)
{
  int i;

  assert(p);
  assert(p->slabs);
  assert(p->free_list);

  for (i=0; i < p->slab_count; i++) {
    slab_destroy(p->slabs[i]);
  }

  free(p->slabs);
  free(p->free_list);
  free(p);
}

static slab_t get_usable_slab(pool_t p)
{
  slab_t s = p->slabs[p->slab_curr];

  if (!slab_eof(s))
    return s;

  if (p->slab_curr + 1 < p->slab_count) {
    return p->slabs[++p->slab_curr];
  }

  return NULL;
}

void * pool_get(pool_t p)
{
  slab_t s;
  void *item = NULL;
  int last;

  LOCK(p);

  if (p->free_count > 0) {
    item = p->free_list[0];
    last = --p->free_count;
    p->free_list[0] = p->free_list[last];
  } else {
    s = get_usable_slab(p);
    if (s) {
      item = slab_get_cur(s);
      slab_update_position(s, p->item_size);
    }
  }

  UNLOCK(p);

  return item;
}

void pool_put(pool_t p, void *item)
{
  assert(p->free_count >= 0);

  LOCK(p);
  p->free_list[p->free_count++] = item;
  UNLOCK(p);
}

void pool_resize(pool_t p, int new_size)
{
  size_t size;

  LOCK(p);

  assert(new_size > p->size);

  size = new_size - p->size;
  p->size = new_size;
  add_slab(p, size);
  UNLOCK(p);
}

#ifdef RUN_TESTS

static void test_basic(void)
{
  int i;
  void *start = NULL;
  void *item;
  pool_t p = pool_new(100, 10);

  /* get all items */
  for (i=0; i < 10; i++) {
    item = pool_get(p);
    if (!i)
      start = item;
    assert(item);
  }

  assert(pool_get(p) == NULL);

  /* put all items */
  for (i=0; i < 10; i++)
    pool_put(p, start + (i * 10));

  /* get them back */
  for (i=0; i < 10; i++)
    assert(pool_get(p));

  pool_destroy(p);
}

static void test_resize(void)
{
  void *a, *b, *c;
  pool_t p = pool_new(20, 10);

  a = pool_get(p);
  assert(a);

  b = pool_get(p);
  assert(b);

  assert(pool_get(p) == NULL);

  pool_resize(p, 30);

  c = pool_get(p);
  assert(c);

  assert(pool_get(p) == NULL);

  /* put them back */
  pool_put(p, a);
  pool_put(p, b);
  pool_put(p, c);

  /* get them again */
  a = pool_get(p);
  assert(a);

  b = pool_get(p);
  assert(b);

  c = pool_get(p);
  assert(c);

  assert(pool_get(p) == NULL);

  /* put them back, again */
  pool_put(p, a);
  pool_put(p, b);
  pool_put(p, c);

  pool_destroy(p);
}

int main(int argc, char **argv)
{
  run_test("basic", &test_basic);
  run_test("resize", &test_resize);

  return 0;
}

#endif
