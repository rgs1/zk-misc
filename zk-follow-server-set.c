/*
 * TODO:
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


typedef struct {
  pthread_mutex_t lock;
  zhandle_t *zh;
} zk_conn;


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
static pid_t g_pid;
static int g_sleep_after_clients = 0;
static int g_sleep_inbetween_clients = 5;
static zk_conn *g_zhs;


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
static void do_check_interests(zk_conn *zkc);
static zhandle_t *create_client(zk_conn *zkc, int pos);
static void change_uid(int num);
static void error(int rc, const char *msgfmt, ...);
static void warn(const char *msgfmt, ...);
static void info(const char *msgfmt, ...);
static void do_log(const char *level, const char *msgfmt, va_list ap);


int main(int argc, char **argv)
{
  int i = 0;
  pid_t pid;

  parse_argv(argc, argv);

  if (argc <= optind)
    error(EXIT_BAD_PARAMS, "Give me the hostname");

  g_pid = getpid();

  g_servername = argv[optind];
  g_serverset_path = g_serverset_path ? g_serverset_path : DEFAULT_SERVERSET_PATH;
  g_username_prefix = g_username_prefix ? g_username_prefix : DEFAULT_USERNAME_PREFIX;

  info("Running with:");
  info("server = %s", g_servername);
  info("username_prefix = %s", g_username_prefix);
  info("server_set_path = %s", g_serverset_path);
  info("max_events = %d", g_max_events);
  info("num_clients = %d", g_num_clients);
  info("num_procs = %d", g_num_procs);
  info("wait_time = %d", g_wait_time);
  info("zk_session_timeout = %d", g_zk_session_timeout);
  info("sleep_after_clients = %d", g_sleep_after_clients);
  info("sleep_inbetween_clients = %d", g_sleep_inbetween_clients);

  zoo_set_debug_level(ZOO_LOG_LEVEL_DEBUG);

  for (i=0; i < g_num_procs; i++) {
    pid = fork();
    if (pid == -1)
      error(EXIT_SYSTEM_CALL, "Ugh, couldn't fork");

    if (!pid) {
      pthread_t tid;

      g_pid = getpid();

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
  int j;
  struct timespec req = { 0, 10 * 1000 * 1000 } ; /* 10ms */

  while (1) {
    /* Lets see what new interests we've got (i.e.: new Pings, etc) */
    for (j=0; j < g_num_clients; j++) {
      do_check_interests(&g_zhs[j]);
    }

    nanosleep(&req, NULL);
  }

  return NULL;
}

static void do_check_interests(zk_conn *zkc)
{
  int fd, rc, interest, saved;
  struct epoll_event ev;
  struct timeval tv;

  fd = -1;

  pthread_mutex_lock(&zkc->lock);
  rc = zookeeper_interest(zkc->zh, &fd, &interest, &tv);
  pthread_mutex_unlock(&zkc->lock);

  if (rc || fd == -1) {
    if (fd != -1 && (rc == ZINVALIDSTATE || rc == ZCONNECTIONLOSS))
      /* Note that ev must be !NULL for kernels < 2.6.9 */
      epoll_ctl(g_epfd, EPOLL_CTL_DEL, fd, &ev);
    return;
  }

  ev.data.ptr = zkc;
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

static void start_clients(void)
{
  int j, saved;
  int after = g_sleep_after_clients;
  int inbetween = g_sleep_inbetween_clients;

  g_zhs = (zk_conn *)safe_alloc(sizeof(zk_conn) * g_num_clients);

  g_epfd = epoll_create(1);
  if (g_epfd == -1) {
    saved = errno;
    error(EXIT_SYSTEM_CALL, "Failed to create an epoll instance: %s", strerror(saved));
  }

  for (j=0; j < g_num_clients; j++) {
    if (pthread_mutex_init(&g_zhs[j].lock, 0))
      error(EXIT_SYSTEM_CALL, "Failed to init mutex");
    /* no need to lock here, only one thread at this point */
    g_zhs[j].zh = create_client(&g_zhs[j], j);

    if (after > 0 && j > 0 && j % after == 0) {
      warn("Sleeping for %d secs after having created %d clients", inbetween, j);
      sleep(inbetween);
    }
  }
}

static void poll_clients(void)
{
  int ready, j, saved;
  int events;
  struct epoll_event *evlist;
  zk_conn *zkc;

  evlist = (struct epoll_event *)safe_alloc(sizeof(struct epoll_event) * g_max_events);

  while (1) {
    ready = epoll_wait(g_epfd, evlist, g_max_events, g_wait_time);
    if (ready == -1) {
      if (errno == EINTR)
        continue;

      saved = errno;
      error(EXIT_SYSTEM_CALL, "epoll_wait failed with: %s", strerror(saved));
    }

    /* Go over file descriptors that are ready */
    for (j=0; j < ready; j++) {
      events = 0;
      if (evlist[j].events & (EPOLLIN|EPOLLOUT)) {
        if (evlist[j].events & EPOLLIN)
          events |= ZOOKEEPER_READ;
        if (evlist[j].events & EPOLLOUT)
          events |= ZOOKEEPER_WRITE;

        /* watcher is called from here, so no need to take a lock from there */
        zkc = (zk_conn *)evlist[j].data.ptr;

        /* add zkc to work queue */
        pthread_mutex_lock(&zkc->lock);
        zookeeper_process(zkc->zh, events);
        pthread_mutex_unlock(&zkc->lock);

      } else if (evlist[j].events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)) {
        /* Invalid FDs will be removed when zookeeper_interest() indicates
         * they are not valid anymore */
      } else {
        warn("Unknown events: %d\n", evlist[j].events);
      }
    }
  }
}

static zhandle_t *create_client(zk_conn *zkc, int pos)
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
  ev.data.ptr = zkc;

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
  do_log("ERROR", msgfmt, ap);
  va_end(ap);

  exit(rc);
}

static void warn(const char *msgfmt, ...)
{
  va_list ap;

  va_start(ap, msgfmt);
  do_log("WARN", msgfmt, ap);
  va_end(ap);
}

static void info(const char *msgfmt, ...)
{
  va_list ap;

  va_start(ap, msgfmt);
  do_log("INFO", msgfmt, ap);
  va_end(ap);
}

static void do_log(const char *level, const char *msgfmt, va_list ap)
{
  char buf[1024];
  time_t t = time(NULL);
  struct tm *p = localtime(&t);

  strftime(buf, 1024, "%B %d %Y %H:%M:%S", p);

  printf("[%s][PID %d][%s] ", level, g_pid, buf);
  vprintf(msgfmt, ap);
  printf("\n");
}

static void strings_completion(int rc,
        const struct String_vector *strings,
        const void *data) {
  if (strings)
    info("Got %d children", strings->count);
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

  if (type != ZOO_SESSION_EVENT) {
    info("%d %d %s", type, state, path);
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
    g_zhs[pos].zh = create_client(&g_zhs[pos], pos);
  }
}

static void parse_argv(int argc, char **argv)
{
  static const struct option options[] = {
    { "help",                 no_argument,       NULL, 'h' },
    { "max-events",           required_argument, NULL, 'e' },
    { "num-clients",          required_argument, NULL, 'c' },
    { "num-procs",            required_argument, NULL, 'p' },
    { "wait-time",            required_argument, NULL, 'w' },
    { "session-timeout",      required_argument, NULL, 's' },
    { "switch-uid",           no_argument,       NULL, 'u' },
    { "sleep-after-clients",  required_argument, NULL, 'N' },
    { "sleep-in-between",     required_argument, NULL, 'n' },
    { "watched-paths",        required_argument, NULL, 'z' },
    {}
  };

  int c;

  assert(argc >= 0);
  assert(argv);

  while ((c = getopt_long(argc, argv, "+he:c:p:w:s:u:z:N:n:", options, NULL)) >= 0) {
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
    case 'N':
      g_sleep_after_clients = positive_int(optarg, "sleep after clients");
      break;
    case 'n':
      g_sleep_inbetween_clients = positive_int(optarg, "sleep time between clients");
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
         "  --help,                -h        Show this help\n"
         "  --max-events,          -e        Set the max number of events\n"
         "  --num-clients,         -c        Set the number of clients\n"
         "  --num-procs,           -p        Set the number of processes\n"
         "  --wait-time,           -w        Set the wait time for epoll_wait()\n"
         "  --session-timeout,     -s        Set the session timeout for ZK clients\n"
         "  --switch-uid,          -u        Switch UID after forking\n"
         "  --sleep-after-clients  -N        Sleep after starting N clients\n"
         "  --sleep-in-between     -n        Seconds to sleep inbetween N started clients\n"
         "  --watched-paths,       -z        Watched path\n",
         program_invocation_short_name);
}
