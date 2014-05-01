/*
 * Local Variables:
 * compile-command: "gcc -Wall -I/tmp/zookeeper-libs/include/zookeeper -L/tmp/zookeeper-libs/lib -Wl,-rpath=/tmp/zookeeper-libs/lib -lpthread -lzookeeper_st zk-follow-server-set.c -o zk-follow-server-set "
 * End:
 *
 *
 * TODO:
 *      locking around context & zhandle_t(s)
 *      graceful connect retries
 *      call zookeeper_process from its own thread
 */

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#include <assert.h>
#include <fcntl.h>
#include <getopt.h>
#include <errno.h>
#include <pthread.h>
#include <pwd.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <zookeeper.h>


#ifndef EPOLLRDHUP
#define EPOLLRDHUP 0x2000
#endif

static char *g_username_prefix = NULL;
static char *g_serverset_path = NULL;
static const char *g_servername = NULL;
static int g_max_events = 100;
static int g_num_clients = 500;
static int g_num_procs = 20;
static int g_wait_time = 50;
static int g_zk_session_timeout = 10000;
static int g_switch_uid = 0;
static int g_epfd;
static zhandle_t **g_zhs;


#define EXIT_BAD_PARAMS       1
#define EXIT_SYSTEM_CALL      2
#define EXIT_ZOOKEEPER_CALL   3

#define DEFAULT_USERNAME_PREFIX       "zk-client"
#define DEFAULT_SERVERSET_PATH        "/twitter/service/gizmoduck/prod/gizmoduck"

typedef struct {
  char following;
  int pos;
} zh_context;

static void help(void);
static void parse_argv(int argc, char **argv);
static int positive_int(const char *str, const char *param_name);
static void watcher(zhandle_t *zzh, int type, int state, const char *path, void *context);
static void start_clients(void);
static void poll_clients(void);
static void *check_interests(void *data);
static void do_check_interests(void);
static zhandle_t *create_client(int pos);
static void change_uid(int num);
static void error(int rc, const char *msgfmt, ...);
static void warn(const char *msgfmt, ...);


int main(int argc, char **argv)
{
  int i = 0;
  pid_t pid;

  parse_argv(argc, argv);

  if (argc <= optind)
    error(EXIT_BAD_PARAMS, "Give me the hostname");

  g_servername = argv[optind];
  g_serverset_path = g_serverset_path ? g_serverset_path : DEFAULT_SERVERSET_PATH;
  g_username_prefix = g_username_prefix ? g_username_prefix : DEFAULT_USERNAME_PREFIX;

  printf("Running with:\n");
  printf("server = %s\n", g_servername);
  printf("username_prefix = %s\n", g_username_prefix);
  printf("server_set_path = %s\n", g_serverset_path);
  printf("max_events = %d\n", g_max_events);
  printf("num_clients = %d\n", g_num_clients);
  printf("num_procs = %d\n", g_num_procs);
  printf("wait_time = %d\n", g_wait_time);
  printf("zk_session_timeout = %d\n", g_zk_session_timeout);

  zoo_set_debug_level(ZOO_LOG_LEVEL_DEBUG);

  for (i=0; i < g_num_procs; i++) {
    pid = fork();
    if (pid == -1)
      error(EXIT_SYSTEM_CALL, "Ugh, couldn't fork");

    if (!pid) {
      pthread_t tid;

      if (g_switch_uid)
        change_uid(i);

      start_clients();
      pthread_create(&tid, NULL, &check_interests, NULL);
      poll_clients();
    }
  }

  while (1)
    sleep(100);

  return 0;
}

static void change_uid(int num)
{
  char name[64];
  struct passwd *passwd;

  sprintf(name,
          "%s%d",
          g_username_prefix,
          num);
  passwd = getpwnam(name);
  if (!passwd)
    error(EXIT_SYSTEM_CALL, "Couldn't get the UID for %s", name);
  setuid(passwd->pw_uid);
}

static void * safe_alloc(size_t count)
{
  void *ptr = malloc(count);
  if (!ptr)
    error(EXIT_SYSTEM_CALL, "Failed to allocated memory");
  memset(ptr, 0, count);
  return ptr;
}

static char * safe_strdup(const char *str)
{
  char *s = strdup(str);
  if (!s)
    error(EXIT_SYSTEM_CALL, "Failed to allocated memory");
  return s;
}

static void *check_interests(void *data)
{
  struct timespec req = { 0, 10 * 1000 * 1000 } ; /* 10ms */

  while (1) {
    do_check_interests();
    nanosleep(&req, NULL);
  }

  return NULL;
}

static void do_check_interests(void)
{
  int j, fd, rc, interest, saved;
  struct epoll_event ev;
  struct timeval tv;

  /* Lets see what new interests we've got (i.e.: new Pings, etc) */
  for (j=0; j < g_num_clients; j++) {
    fd = -1;
    rc = zookeeper_interest(g_zhs[j], &fd, &interest, &tv);
    if (rc || fd == -1) {
      if (fd != -1 && (rc == ZINVALIDSTATE || rc == ZCONNECTIONLOSS))
        /* Note that ev must be !NULL for kernels < 2.6.9 */
        epoll_ctl(g_epfd, EPOLL_CTL_DEL, fd, &ev);
      continue;
    }

    ev.data.ptr = g_zhs[j];
    ev.events = 0;
    if (interest & ZOOKEEPER_READ)
      ev.events |= EPOLLIN;

    if (interest & ZOOKEEPER_WRITE)
      ev.events |= EPOLLOUT;

    if (epoll_ctl(g_epfd, EPOLL_CTL_MOD, fd, &ev) == -1) {
      saved = errno;
      if (saved != ENOENT)
        error(EXIT_SYSTEM_CALL,
              "epoll_ctl_mod failed with: %s ",
              strerror(saved));

      /* New FD, lets add it */
      if (epoll_ctl(g_epfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        saved = errno;
        error(EXIT_SYSTEM_CALL,
              "epoll_ctl_add failed with: %s",
              strerror(saved));
      }
    }
  }
}

static void start_clients(void)
{
  int j, saved;

  g_zhs = (zhandle_t **)safe_alloc(sizeof(zhandle_t *) * g_num_clients);

  g_epfd = epoll_create(1);
  if (g_epfd == -1) {
    saved = errno;
    error(EXIT_SYSTEM_CALL, "Failed to create an epoll instance: %s", strerror(saved));
  }

  for (j=0; j < g_num_clients; j++) {
    g_zhs[j] = create_client(j);
  }
}

static void poll_clients(void)
{
  int ready, j, saved;
  int events;
  struct epoll_event *evlist;

  evlist = (struct epoll_event *)safe_alloc(sizeof(struct epoll_event) * g_max_events);

  while (1) {
    ready = epoll_wait(g_epfd, evlist, g_max_events, g_wait_time);
    if (ready == -1) {
      if (errno == EINTR)
        continue;
      else {
        saved = errno;
        error(EXIT_SYSTEM_CALL, "epoll_wait failed with: %s", strerror(saved));
      }
    }

    /* Go over file descriptors that are ready */
    for (j=0; j < ready; j++) {
      events = 0;
      if (evlist[j].events & (EPOLLIN|EPOLLOUT)) {
        if (evlist[j].events & EPOLLIN)
          events |= ZOOKEEPER_READ;
        if (evlist[j].events & EPOLLOUT)
          events |= ZOOKEEPER_WRITE;
        zookeeper_process((zhandle_t *)evlist[j].data.ptr, events);
      } else if (evlist[j].events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)) {
        /* Invalid FDs will be removed when zookeeper_interest() indicates
         * they are not valid anymore */
      } else {
        warn("Unknown events: %d\n", evlist[j].events);
      }
    }
  }
}

static zhandle_t *create_client(int pos)
{
  int fd, rc, interest, saved;
  struct epoll_event ev;
  struct timeval tv;
  zhandle_t *zh;
  zh_context *context;

  context = safe_alloc(sizeof(zh_context));

  context->pos = pos;

  /* try until we succeed */
  while (1) {
    zh = zookeeper_init(g_servername,
                        watcher,
                        g_zk_session_timeout,
                        0,
                        context,
                        ZOO_READONLY);
    fd = -1;
    rc = zookeeper_interest(zh, &fd, &interest, &tv);
    if (rc == ZOK)
      break;

    if (rc == ZCONNECTIONLOSS) {
      /* busy server perhaps? lets try again */
      zookeeper_close(zh);
      continue;
    }

    error(EXIT_ZOOKEEPER_CALL, "zookeeper_interest failed with rc=%d\n", rc);
  }

  ev.events = 0;
  if (interest & ZOOKEEPER_READ)
    ev.events |= EPOLLIN;
  if (interest & ZOOKEEPER_WRITE)
    ev.events |= EPOLLOUT;
  ev.data.ptr = zh;

  if (epoll_ctl(g_epfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
    saved = errno;
    error(EXIT_SYSTEM_CALL, "epoll_ctl_add failed with: %s", strerror(saved));
  }

  return zh;
}

static void error(int rc, const char *msgfmt, ...)
{
  va_list ap;

  va_start(ap, msgfmt);
  printf(msgfmt, ap);
  va_end(ap);

  printf("\n");

  exit(rc);
}

static void warn(const char *msgfmt, ...)
{
  va_list ap;

  va_start(ap, msgfmt);
  printf(msgfmt, ap);
  va_end(ap);

  printf("\n");
}

static void strings_completion(int rc,
        const struct String_vector *strings,
        const void *data) {
  if (strings)
    printf("Got %d children\n", strings->count);
}

static int is_connected(zhandle_t *zh)
{
  int state = zoo_state(zh);
  return state == ZOO_CONNECTED_STATE || state == ZOO_CONNECTED_RO_STATE;
}

static void watcher(zhandle_t *zzh, int type, int state, const char *path, void *ctxt)
{
  int rc;
  zh_context *context;

  printf("type = %d\n", type);

  if (type != ZOO_SESSION_EVENT) {
    printf("%d %d %s\n", type, state, path);
    rc = zoo_aget_children(zzh,
                           g_serverset_path,
                           1,
                           strings_completion,
                           NULL);
    if (rc)
      warn("Failed to list path");

    return;
  }

  /* TODO: handle *all* state transitions */
  if (is_connected(zzh)) {
    context = (zh_context *)zoo_get_context(zzh);
    if (!context->following) {
      rc = zoo_aget_children(zzh,
                             g_serverset_path,
                             1,
                             strings_completion,
                             NULL);
      if (rc)
        warn("Failed to list path");
      else
        context->following = 1;
    }
  } else if (state == ZOO_EXPIRED_SESSION_STATE) {
    int pos;

    /* Cleanup the expired session */
    zookeeper_close(zzh);
    context = (zh_context *)zoo_get_context(zzh);
    pos = context->pos;
    free(context);

    /* We never give up: create a new session */
    g_zhs[pos] = create_client(pos);
  }
}

static void parse_argv(int argc, char **argv)
{
  static const struct option options[] = {
    { "help",               no_argument,       NULL, 'h' },
    { "max_events",         required_argument, NULL, 'e' },
    { "num_clients",        required_argument, NULL, 'c' },
    { "num_procs",          required_argument, NULL, 'p' },
    { "wait_time",          required_argument, NULL, 'w' },
    { "session_timeout",    required_argument, NULL, 's' },
    { "switch_uid",         no_argument,       NULL, 'u' },
    {}
  };

  int c;

  assert(argc >= 0);
  assert(argv);

  while ((c = getopt_long(argc, argv, "+he:c:p:w:s:u:z:", options, NULL)) >= 0) {
    switch (c) {
    case 'h':
      help();
      exit(0);
    case 'e':
      g_max_events = positive_int(optarg, "max events");
      break;
    case 'c':
      g_num_clients = positive_int(optarg, "num clients");
      break;
    case 'p':
      g_num_procs = positive_int(optarg, "num procs");
      break;
    case 'w':
      g_wait_time = positive_int(optarg, "wait time");
      break;
    case 's':
      g_zk_session_timeout = positive_int(optarg, "zk session timeout");
      break;
    case 'u':
      g_switch_uid = 1;
      break;
    case 'z':
      g_serverset_path = safe_strdup(optarg);
      break;
    case '?':
      help();
      exit(1);
    default:
      error(EXIT_BAD_PARAMS, "Bad option %c\n", (char)c);
    }
  }
}

static int positive_int(const char *str, const char *param_name)
{
  int ret = atoi(str);

  if (ret < 0)
    error(EXIT_BAD_PARAMS, "Bad param for %s: %d", param_name, ret);

  return ret;
}

static void help(void)
{
  printf("%s [OPTIONS...] {ZK_SERVER}\n\n"
         "Create and maintain a given number of ZK clients.\n\n"
         "  -h --help               Show this help\n"
         "  -e                      Set the max number of events\n"
         "  -c                      Set the number of clients\n"
         "  -p                      Set the number of processes\n"
         "  -w                      Set the wait time for epoll_wait()\n"
         "  -s                      Set the session timeout for ZK clients\n"
         "  -u                      Switch UID after forking\n"
         "  -z                      ServerSet's path\n",
         program_invocation_short_name);
}
