#ifndef _UTIL_H_
#define _UTIL_H_

#define _GNU_SOURCE

#include <pthread.h>


#define EXIT_BAD_PARAMS       1
#define EXIT_SYSTEM_CALL      2
#define EXIT_ZOOKEEPER_CALL   3


void * safe_alloc(size_t count);
char * safe_strdup(const char *str);
int positive_int(const char *str, const char *param_name);
void change_uid(const char *username);
void error(int rc, const char *msgfmt, ...);
void warn(const char *msgfmt, ...);
void info(const char *msgfmt, ...);
void set_thread_name(pthread_t thread, const char *name);

#endif
