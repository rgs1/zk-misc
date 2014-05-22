#ifndef _UTIL_H_
#define _UTIL_H_

#define _GNU_SOURCE

#include <pthread.h>


#define EXIT_BAD_PARAMS       1
#define EXIT_SYSTEM_CALL      2
#define EXIT_ZOOKEEPER_CALL   3


void * safe_alloc(size_t count);
void * safe_realloc(void *mem, size_t old_size, size_t new_size);
char * safe_strdup(const char *str);
int positive_int(const char *str, const char *param_name);
void change_uid(const char *username);
void error(int rc, const char *msgfmt, ...);
void warn(const char *msgfmt, ...);
void info(const char *msgfmt, ...);
void set_thread_name(pthread_t thread, const char *name);
void run_test(const char *test_desc, void (*test_func) (void));


#define INIT_LOCK(x) \
        if (pthread_mutex_init(&x->lock, 0)) { error(EXIT_SYSTEM_CALL, "Failed to init mutex"); }
#define LOCK(x)  pthread_mutex_lock(&x->lock)
#define UNLOCK(x)  pthread_mutex_unlock(&x->lock)


#endif
