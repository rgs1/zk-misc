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

#ifdef RUN_TESTS
#include <assert.h>
#endif

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>


#define EXIT_BAD_PARAMS       1
#define EXIT_SYSTEM_CALL      2


typedef struct {
  void **ptrs;
  int head;
  int tail;
  int count;
  int size;
  pthread_mutex_t lock;
  pthread_cond_t cond;
} queue;

/* helpers */
static void error(int rc, const char *msgfmt, ...);
static void do_log(const char *level, const char *msgfmt, va_list ap);
static void info(const char *msgfmt, ...);
static void warn(const char *msgfmt, ...);

queue * queue_new(int size);
void queue_destroy(queue *q);
void queue_init(queue *q);
int queue_add(queue *q, void *item);
void * queue_remove(queue *q);
int queue_empty(queue *q);
int queue_count(queue *q);


static void error(int rc, const char *msgfmt, ...)
{
  va_list ap;

  va_start(ap, msgfmt);
  do_log("ERROR", msgfmt, ap);
  va_end(ap);

  exit(rc);
}

static void info(const char *msgfmt, ...)
{
  va_list ap;

  va_start(ap, msgfmt);
  do_log("INFO", msgfmt, ap);
  va_end(ap);
}

static void warn(const char *msgfmt, ...)
{
  va_list ap;

  va_start(ap, msgfmt);
  do_log("WARN", msgfmt, ap);
  va_end(ap);
}

static void do_log(const char *level, const char *msgfmt, va_list ap)
{
  char buf[1024];
  time_t t = time(NULL);
  struct tm *p = localtime(&t);

  strftime(buf, 1024, "%B %d %Y %H:%M:%S", p);

  printf("[%s][PID %ld][%s] ", level, syscall(SYS_gettid), buf);
  vprintf(msgfmt, ap);
  printf("\n");
}

static void * safe_alloc(size_t count)
{
  void *ptr = malloc(count);
  if (!ptr)
    error(EXIT_SYSTEM_CALL, "Failed to allocated memory");
  memset(ptr, 0, count);
  return ptr;
}

void queue_init(queue *q)
{
  if (pthread_mutex_init(&q->lock, 0)) {
    error(EXIT_SYSTEM_CALL, "Failed to allocated memory");
  }
}

queue * queue_new(int size)
{
  queue *q = safe_alloc(sizeof(queue));
  q->ptrs = safe_alloc(sizeof(void *) * size);
  q->size = size;
  queue_init(q);
  return q;
}

void queue_destroy(queue *q)
{
  assert(q);
  assert(q->ptrs);
  free(q->ptrs);
  free(q);
}

int queue_add(queue *q, void *item)
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
int queue_empty(queue *q)
{
  return q->count == 0;
}

/* blocks until there's an element to remove */
void *queue_remove(queue *q)
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

int queue_count(queue *q)
{
  return q->count;
}


/*
 * tests
 */

#ifdef RUN_TESTS

void *producer(void *data)
{
  char *a = "hello";
  char *b = "goodbye";
  queue *q = (queue *)data;

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
  queue *q = (queue *)data;

  info("removed item = %s", (char *)queue_remove(q));
  sleep(4);
  info("removed item = %s", (char *)queue_remove(q));

  info("count = %d", queue_count(q));

  return NULL;
}

void test_basic(void)
{
  queue *q = queue_new(2);
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
  queue *q = queue_new(1);

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
  queue *q = queue_new(3);

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
  queue *q = queue_new(3);

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
