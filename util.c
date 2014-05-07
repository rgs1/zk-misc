#include "util.h"

#include <pwd.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>


static void do_log(const char *level, const char *msgfmt, va_list ap);


void * safe_alloc(size_t count)
{
  void *ptr = malloc(count);
  if (!ptr)
    error(EXIT_SYSTEM_CALL, "Failed to allocated memory");
  memset(ptr, 0, count);
  return ptr;
}

char * safe_strdup(const char *str)
{
  char *s = strdup(str);
  if (!s)
    error(EXIT_SYSTEM_CALL, "Failed to allocated memory");
  return s;
}

int positive_int(const char *str, const char *param_name)
{
  int ret = atoi(str);

  if (ret < 0)
    error(EXIT_BAD_PARAMS, "Bad param for %s: %d", param_name, ret);

  return ret;
}

void change_uid(const char *username)
{
  struct passwd *passwd;

  passwd = getpwnam(username);
  if (!passwd)
    error(EXIT_SYSTEM_CALL, "Couldn't get the UID for %s", username);
  setuid(passwd->pw_uid);
}

void error(int rc, const char *msgfmt, ...)
{
  va_list ap;

  va_start(ap, msgfmt);
  do_log("ERROR", msgfmt, ap);
  va_end(ap);

  exit(rc);
}

void warn(const char *msgfmt, ...)
{
  va_list ap;

  va_start(ap, msgfmt);
  do_log("WARN", msgfmt, ap);
  va_end(ap);
}

void info(const char *msgfmt, ...)
{
  va_list ap;

  va_start(ap, msgfmt);
  do_log("INFO", msgfmt, ap);
  va_end(ap);
}

/* an init function to provide the caller's PID would be nice */
static void do_log(const char *level, const char *msgfmt, va_list ap)
{
  char buf[1024];
  time_t t = time(NULL);
  struct tm *p = localtime(&t);

  strftime(buf, 1024, "%B %d %Y %H:%M:%S", p);

  printf("[%s][PID %d][%s] ", level, getpid(), buf);
  vprintf(msgfmt, ap);
  printf("\n");
}

void set_thread_name(pthread_t thread, const char *name)
{
#if __GLIBC__ >= 2 && __GLIBC_MINOR__ >= 12
  pthread_setname_np(thread, name);
#endif
}
