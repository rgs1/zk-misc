/*
 * A very simple queue drop-in implementation for writing
 * quick throw away tests/prototypes.
 *
 * To test:
 *   gcc -DRUN_TESTS -Wall -lpthread queue.c -o queue
 */

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#include "queue.h"
#include "util.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>


void queue_init(queue_t q)
{
  if (pthread_mutex_init(&q->lock, 0)) {
    error(EXIT_SYSTEM_CALL, "Failed to allocated memory");
  }
}

queue_t queue_new(int size)
{
  queue_t q = safe_alloc(sizeof(queue));
  q->ptrs = safe_alloc(sizeof(void *) * size);
  q->size = size;
  queue_init(q);
  return q;
}

void queue_destroy(queue_t q)
{
  assert(q);
  assert(q->ptrs);
  free(q->ptrs);
  free(q);
}

int queue_add(queue_t q, void *item)
{
  int rv = 1;

  pthread_mutex_lock(&q->lock);

  /* queue full?*/
  if (q->count == q->size) {
    warn("Can't add item, queue is full");
    rv = 0;
    goto out;
  }

  if (q->tail >= q->size) {
    q->tail = 0;
  }


  q->ptrs[q->tail] = item;
  q->tail++;
  q->count++;

  pthread_cond_broadcast(&q->cond);

out:
  pthread_mutex_unlock(&q->lock);

  return rv;
}

/*
 * you need to lock the queue to call this
 * returns 1 if empty, 0 otherwise
 */
int queue_empty(queue_t q)
{
  return q->count == 0;
}

/* blocks until there's an element to remove */
void *queue_remove(queue_t q)
{
  void *item = NULL;

  pthread_mutex_lock(&q->lock);

  while (queue_empty(q)) {
    pthread_cond_wait(&q->cond, &q->lock);
  }

  if (q->head >= q->size)
    q->head = 0;

  item = q->ptrs[q->head];
  q->head++;
  q->count--;

  pthread_mutex_unlock(&q->lock);

  return item;
}

int queue_count(queue_t q)
{
  return q->count;
}


void queue_set_user_data(queue_t q, void *data)
{
  if (q->user_data)
    info("overwriting user_data");

  q->user_data = data;
}

void *queue_get_user_data(queue_t q)
{
  return q->user_data;
}


/*
 * tests
 */

#ifdef RUN_TESTS

void *producer(void *data)
{
  char *a = "hello";
  char *b = "goodbye";
  queue_t q = (queue_t)data;

  info("Adding a=%s", a);
  queue_add(q, a);
  sleep(2);
  info("Adding b=%s", b);
  queue_add(q, b);

  info("count = %d", queue_count(q));

  /* wait until all is consumed */
  while (!queue_empty(q))
    sleep(1);

  return NULL;
}

void *consumer(void *data)
{
  queue_t q = (queue_t)data;

  info("removed item = %s", (char *)queue_remove(q));
  sleep(4);
  info("removed item = %s", (char *)queue_remove(q));

  info("count = %d", queue_count(q));

  return NULL;
}

void test_basic(void)
{
  queue_t q = queue_new(2);
  pthread_t producer_tid, consumer_tid;

  pthread_create(&producer_tid, NULL, &producer, q);
  pthread_create(&consumer_tid, NULL, &consumer, q);

  pthread_join(producer_tid, NULL);
  pthread_join(consumer_tid, NULL);

  assert(queue_count(q) == 0);

  queue_destroy(q);
}

void test_queue_full(void)
{
  char *a = "hello";
  char *b = "goodbye";
  queue_t q = queue_new(1);

  assert(queue_add(q, a));
  info("count = %d", queue_count(q));
  assert(queue_count(q) == 1);
  assert(!queue_add(q, b));
  info("count = %d", queue_count(q));
  assert(queue_count(q) == 1);

  queue_destroy(q);
}

void test_more_than_size(void)
{
  queue_t q = queue_new(3);

  queue_add(q, NULL);
  queue_add(q, NULL);
  queue_add(q, NULL);

  info("count = %d", queue_count(q));
  assert(queue_count(q) == 3);

  queue_remove(q);
  info("count = %d", queue_count(q));
  assert(queue_count(q) == 2);

  queue_add(q, NULL);
  info("count = %d", queue_count(q));
  assert(queue_count(q) == 3);

  queue_remove(q);
  info("count = %d", queue_count(q));
  assert(queue_count(q) == 2);

  queue_remove(q);
  info("count = %d", queue_count(q));
  assert(queue_count(q) == 1);

  queue_remove(q);
  info("count = %d", queue_count(q));
  assert(queue_count(q) == 0);
}

void test_right_value(void)
{
  int a = 10;
  int b = 20;
  int c = 30;
  int *item;
  queue_t q = queue_new(3);

  queue_add(q, &a);
  queue_add(q, &b);
  queue_add(q, &c);

  item = queue_remove(q);
  info("item = %d", *item);
  assert(*item == 10);

  queue_add(q, &a);

  item = queue_remove(q);
  info("item = %d", *item);
  assert(*item == 20);

  item = queue_remove(q);
  info("item = %d", *item);
  assert(*item == 30);

  item = queue_remove(q);
  info("item = %d", *item);
  assert(*item == 10);

  info("count = %d", queue_count(q));
  assert(queue_count(q) == 0);
}

void run_test(const char *test_desc, void (*test_func) (void))
{
  info("Running %s", test_desc);
  test_func();
}

int main(int argc, char **argv)
{
  run_test("basic", &test_basic);
  run_test("queue full", &test_queue_full);
  run_test("add/remove more than size items", &test_more_than_size);
  run_test("get back the right element", &test_right_value);

  return 0;
}

#endif
