/*
 * calls getChildren(path, watch: true) repeatedly
 */

#include "clients.h"
#include "util.h"


typedef struct {
  char following;
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
  wdata->following = 0;
}

static int is_connected(zhandle_t *zh)
{
  int state = zoo_state(zh);
  return state == ZOO_CONNECTED_STATE || state == ZOO_CONNECTED_RO_STATE;
}

/* i guess i could move console IO to another thread... */
static void strings_completion(int rc,
        const struct String_vector *strings,
        const void *data) {
  if (strings)
      info("Got %d children", strings->count);
}

/* session expiration is handled for us (and a new session created) */
void my_watcher(zhandle_t *zzh, int type, int state, const char *path)
{
  session_context *context = (session_context *)zoo_get_context(zzh);
  int rc;

  if (type != ZOO_SESSION_EVENT) {
    info("%d %d %s", type, state, path);
    rc = zoo_aget_children(zzh,
                           context->path,
                           1,
                           strings_completion,
                           NULL);
    if (rc)
      warn("Failed to list path");

  } else {
    if (is_connected(zzh)) {
      watcher_data *wdata = (watcher_data *)context->data;

      if (!wdata->following) {
        rc = zoo_aget_children(zzh,
                               context->path,
                               1,
                               strings_completion,
                               NULL);
        if (rc)
          warn("Failed to list path");
        else
          wdata->following = 1;
      }
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
