#ifndef _CLIENTS_H_
#define _CLIENTS_H_

#include <zookeeper.h>

typedef struct {
  void *data;
  int pos;
  const char *path;
  void (*watcher)(zhandle_t *, int, int, const char *);
  void (*reset_watcher_data)(void *);
} session_context;


void clients_run(int,
                 const char **,
                 void (*)(zhandle_t *, int, int, const char *),
                 void *(*)(void),
                 void (*)(void *));


#endif
