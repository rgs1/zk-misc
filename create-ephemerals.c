/*
 * calls create(path, ephemeral: true, seq: true) for each client
 */

#include "clients.h"
#include "util.h"


typedef struct {
  char created;
} watcher_data;

static void *new_watcher_data(void)
{
  watcher_data *wdata =
    (watcher_data *)safe_alloc(sizeof(watcher_data));
  return wdata;
}

static void reset_watcher_data(void *data)
{
  watcher_data *wdata = (watcher_data *)data;
  wdata->created = 0;
}

static void create_cb(int rc, const char *path, const void *data)
{
  if (!rc)
    info("Created %s", path);
  else
    warn("Create failed with rc=%d", rc);
}

/* session expiration is handled for us (and a new session created) */
void my_watcher(zhandle_t *zzh, int type, int state, const char *path)
{
  session_context *context;
  watcher_data *wdata;
  int rc;

  if (type != ZOO_SESSION_EVENT)
    return;

  if (state == ZOO_CONNECTED_STATE) {
    context = (session_context *)zoo_get_context(zzh);
    wdata = (watcher_data *)context->data;

    if (!wdata->created) {
      int flags = ZOO_EPHEMERAL|ZOO_SEQUENCE;
      rc = zoo_acreate(zzh,
                       context->path,
                       "test",
                       4,
                       &ZOO_OPEN_ACL_UNSAFE,
                       flags,
                       create_cb,
                       NULL);
      if (rc)
        warn("Failed to create path");
      else
        wdata->created = 1;
    }
  }
}

int main(int argc, const char **argv)
{
  clients_run(argc,
              argv,
              &my_watcher,
              &new_watcher_data,
              &reset_watcher_data);
  return 0;
}
