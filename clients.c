/*
 * TODO:
 *      graceful connect retries
 */

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#include <assert.h>
#include <fcntl.h>
#include <getopt.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <zookeeper.h>

#include "clients.h"
#include "queue.h"
#include "util.h"


#ifndef EPOLLRDHUP
#define EPOLLRDHUP 0x2000
#endif


#define DEFAULT_USERNAME_PREFIX       "zk-client"
#define DEFAULT_PATH        "/"


typedef struct {
  int events;
  int queued;
  pthread_mutex_t lock;
  zhandle_t *zh;
  const char *server;
  int session_timeout;
} connection;

typedef struct {
  char *username_prefix;
  char *path;
  const char *servername;
  int max_events;   /* max events for epoll_wait */
  int num_clients;  /* clients per child process */
  int num_procs;   /* # of procs which'll spawn ZK clients */
  int num_workers;  /* # of threads to call zookeeper_process from */
  int wait_time;   /* wait time for epoll_wait */
  int zk_session_timeout;
  int switch_uid;
  int sleep_after_clients; /* call sleep(N) after this # of clnts */
  int sleep_inbetween_clients; /* N for the above sleep(N) */
  void (*watcher)(zhandle_t *, int, int, const char *);
  void *(*new_watcher_data)(void);
  void (*reset_watcher_data)(void *);
} run_params;

static int g_epfd;
static connection *g_zhs; /* state & meta-state for all zk clients */

static void help(void);
static void parse_argv(int argc, const char **argv, run_params *params);
static void init_params(run_params *params);
static void watcher(zhandle_t *zzh, int type, int state, const char *path, void *context);
static void start_child_proc(int child_num, run_params *params);
static void *create_clients(void *data);
static void *poll_clients(void *data);
static void *check_interests(void *data);
static void *zk_process_worker(void *data);
static void do_check_interests(connection *zkc);
static void create_client(connection *conn, void *context);


void clients_run(int argc,
                 const char **argv,
                 void (*my_watcher)(zhandle_t *, int, int, const char *),
                 void *(*new_watcher_data)(void),
                 void (*reset_watcher_data)(void *))
{
  int i = 0;
  pid_t pid;
  run_params params;

  init_params(&params);
  parse_argv(argc, argv, &params);
  params.watcher = my_watcher;
  params.new_watcher_data = new_watcher_data;
  params.reset_watcher_data = reset_watcher_data;

  zoo_set_debug_level(ZOO_LOG_LEVEL_DEBUG);

  prctl(PR_SET_NAME, "parent", 0, 0, 0);

  for (i=0; i < params.num_procs; i++) {
    pid = fork();
    if (pid == -1)
      error(EXIT_SYSTEM_CALL, "Ugh, couldn't fork");

    if (!pid) {
      start_child_proc(i, &params);
    }
  }

  if (pid) { /* parent */
    /* TODO: wait() on children */
    while (1)
      sleep(100);
  }
}

/* set defaults */
static void init_params(run_params *params)
{
  params->username_prefix = NULL;
  params->path = NULL;
  params->servername = NULL;
  params->max_events = 100;
  params->num_clients = 500;
  params->num_procs = 20;
  params->num_workers = 1;
  params->wait_time = 50;
  params->zk_session_timeout = 10000;
  params->switch_uid = 0;
  params->sleep_after_clients = 0;
  params->sleep_inbetween_clients = 5;
}

static void start_child_proc(int child_num, run_params *params)
{
  char tname[20];
  int saved, j;
  int num_workers = params->num_workers;
  int num_clients = params->num_clients;
  pthread_t tid_interests, tid_poller, tid_create_clients;
  pthread_t *tids_workers;
  queue_t queue;

  tids_workers = (pthread_t *)safe_alloc(sizeof(pthread_t) * num_workers);

  snprintf(tname, 20, "child[%d]", child_num);
  prctl(PR_SET_NAME, tname, 0, 0, 0);

  queue = queue_new(num_clients);

  /* for threads needing the params */
  queue_set_user_data(queue, (void *)params);

  if (params->switch_uid) {
    char username[64];
    sprintf(username, "%s%d", params->username_prefix, child_num);
    change_uid(username);
  }

  g_zhs = (connection *)safe_alloc(sizeof(connection) * num_clients);

  g_epfd = epoll_create(1);
  if (g_epfd == -1) {
    saved = errno;
    error(EXIT_SYSTEM_CALL,
          "Failed to create an epoll instance: %s",
          strerror(saved));
  }

  /* prepare locks */
  for (j=0; j < num_clients; j++) {
    if (pthread_mutex_init(&g_zhs[j].lock, 0)) {
      error(EXIT_SYSTEM_CALL, "Failed to init mutex");
    }
  }

  /* start threads */
  pthread_create(&tid_create_clients, NULL, &create_clients, params);
  set_thread_name(tid_create_clients, "creator");

  pthread_create(&tid_interests, NULL, &check_interests, params);
  set_thread_name(tid_interests, "interests");

  pthread_create(&tid_poller, NULL, &poll_clients, queue);
  set_thread_name(tid_poller, "poller");

  for (j=0; j < num_workers; j++) {
    char thread_name[128];

    snprintf(thread_name, 128, "work[%d]", j);
    pthread_create(&tids_workers[j], NULL, &zk_process_worker, queue);
    set_thread_name(tids_workers[j], thread_name);
  }

  /* TODO: monitor each thread's health */
  while (1)
    sleep(100);
}

static void *zk_process_worker(void *data)
{
  connection *zkc;
  queue_t queue = (queue_t)data;

  while (1) {
    zkc = (connection *)queue_remove(queue);

    /* Note:
     *
     * watchers are called from here, so no need for locking from there
     */
    pthread_mutex_lock(&zkc->lock);
    zkc->queued = 0;
    zookeeper_process(zkc->zh, zkc->events);
    pthread_mutex_unlock(&zkc->lock);
  }

  return NULL;
}

static void *check_interests(void *data)
{
  int j;
  struct timespec req = { 0, 10 * 1000 * 1000 } ; /* 10ms */
  run_params *params = (run_params *)data;
  int num_clients = params->num_clients;

  while (1) {
    /* Lets see what new interests we've got (i.e.: new Pings, etc) */
    for (j=0; j < num_clients; j++) {
      do_check_interests(&g_zhs[j]);
    }

    nanosleep(&req, NULL);
  }

  return NULL;
}

static void do_check_interests(connection *zkc)
{
  int fd, rc, interest, saved, client_ready;
  struct epoll_event ev;
  struct timeval tv;

  fd = -1;
  client_ready = 1;

  /* TODO: if queued to be processed, should we skip? */
  pthread_mutex_lock(&zkc->lock);
  if (zkc->zh) {
    rc = zookeeper_interest(zkc->zh, &fd, &interest, &tv);
  } else {
    client_ready = 0;
  }
  pthread_mutex_unlock(&zkc->lock);

  if (!client_ready)
    return;

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

static void * create_clients(void *data)
{
  int j, after, inbetween;
  run_params *params = (run_params *)data;

  after = params->sleep_after_clients;
  inbetween = params->sleep_inbetween_clients;

  for (j=0; j < params->num_clients; j++) {
    session_context *context = safe_alloc(sizeof(session_context));

    context->pos = j; /* a pointer to connection * would be better */
    context->path = params->path;
    context->watcher = params->watcher;
    context->data = params->new_watcher_data();
    context->reset_watcher_data = params->reset_watcher_data;

    pthread_mutex_lock(&g_zhs[j].lock);
    g_zhs[j].server = params->servername;
    g_zhs[j].session_timeout = params->zk_session_timeout;
    create_client(&g_zhs[j], context);
    pthread_mutex_unlock(&g_zhs[j].lock);

    if (after > 0 && j > 0 && j % after == 0) {
      info("Sleeping for %d secs after having created %d clients",
           inbetween,
           j);
      sleep(inbetween);
    }
  }

  info("Done creating clients...");

  return NULL;
}

static void *poll_clients(void *data)
{
  int ready, j, saved;
  int events;
  struct epoll_event *evlist;
  queue_t queue = (queue_t)data;
  connection *conn;
  run_params *params = (run_params *)queue_get_user_data(queue);
  int max_events = params->max_events;
  int wait_time = params->wait_time;

  evlist = (struct epoll_event *)safe_alloc(
      sizeof(struct epoll_event) * max_events);

  while (1) {
    ready = epoll_wait(g_epfd, evlist, max_events, wait_time);
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

        conn = (connection *)evlist[j].data.ptr;

        pthread_mutex_lock(&conn->lock);

        if (!conn->queued) {
          conn->events = events;
          conn->queued = 1;
          queue_add(queue, (void *)conn);
        }

        pthread_mutex_unlock(&conn->lock);

      } else if (evlist[j].events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)) {
        /* Invalid FDs will be removed when zookeeper_interest() indicates
         * they are not valid anymore */
      } else {
        warn("Unknown events: %d\n", evlist[j].events);
      }
    }
  }

  return NULL;
}

static void create_client(connection *conn, void *context)
{
  int fd, rc, interest, saved;
  struct epoll_event ev;
  struct timeval tv;
  zhandle_t *zh;

  /* try until we succeed */
  while (1) {
    zh = zookeeper_init(conn->server,
                        watcher,
                        conn->session_timeout,
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
  ev.data.ptr = conn;

  if (epoll_ctl(g_epfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
    saved = errno;
    error(EXIT_SYSTEM_CALL, "epoll_ctl_add failed with: %s", strerror(saved));
  }

  conn->zh = zh;
}

/* no locks are taken here, those happen from wherever zookeeper_process
 * is called. */
static void watcher(zhandle_t *zzh, int type, int state, const char *path, void *ctxt)
{
  session_context *context = (session_context *)zoo_get_context(zzh);

  if (state == ZOO_EXPIRED_SESSION_STATE) {
    /* Cleanup the expired session */
    zookeeper_close(zzh);

    /* create a new session */
    context->reset_watcher_data(context->data);
    create_client(&g_zhs[context->pos], context);
  } else {
    /* dispatch the event to the other watcher */
    context->watcher(zzh, type, state, path);
  }
}

static void parse_argv(int argc, const char **argv, run_params *params)
{
  const char *sopts = "+he:c:p:w:s:u:z:N:n:W:";
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
    { "num-workers",          required_argument, NULL, 'W' },
    {}
  };
  int c;

  assert(argc >= 0);
  assert(argv);

  while ((c = getopt_long(argc, (char **)argv, sopts, options, NULL)) >= 0) {
    switch (c) {
    case 'h':
      help();
      exit(0);
    case 'e':
      params->max_events = positive_int(optarg, "max events");
      break;
    case 'c':
      params->num_clients = positive_int(optarg, "num clients");
      break;
    case 'p':
      params->num_procs = positive_int(optarg, "num procs");
      break;
    case 'w':
      params->wait_time = positive_int(optarg, "wait time");
      break;
    case 's':
      params->zk_session_timeout = positive_int(optarg, "zk session timeout");
      break;
    case 'u':
      params->switch_uid = 1;
      break;
    case 'z':
      params->path = safe_strdup(optarg);
      break;
    case 'N':
      params->sleep_after_clients = positive_int(optarg, "sleep after clients");
      break;
    case 'n':
      params->sleep_inbetween_clients =
        positive_int(optarg, "sleep time between clients");
      break;
    case 'W':
      params->num_workers =
        positive_int(optarg, "number of workers for zookeeper_process");
      break;
    case '?':
      help();
      exit(1);
    default:
      error(EXIT_BAD_PARAMS, "Bad option %c\n", (char)c);
    }
  }


  if (argc <= optind)
    error(EXIT_BAD_PARAMS, "Give me the hostname");

  params->servername = argv[optind];
  if (!params->path)
    params->path = DEFAULT_PATH;
  if (!params->username_prefix)
    params->username_prefix = DEFAULT_USERNAME_PREFIX;

  info("Running with:");
  info("server = %s", params->servername);
  info("username_prefix = %s", params->username_prefix);
  info("server_set_path = %s", params->path);
  info("max_events = %d", params->max_events);
  info("num_clients = %d", params->num_clients);
  info("num_procs = %d", params->num_procs);
  info("wait_time = %d", params->wait_time);
  info("zk_session_timeout = %d", params->zk_session_timeout);
  info("sleep_after_clients = %d", params->sleep_after_clients);
  info("sleep_inbetween_clients = %d", params->sleep_inbetween_clients);
  info("num_workers = %d", params->num_workers);
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
         "  --num-workers          -W        # of workers to call zookeeper_process() from\n"
         "  --watched-paths,       -z        Watched path\n",
         program_invocation_short_name);
}

