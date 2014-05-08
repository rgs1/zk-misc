Small collection of programs to test ZooKeeper's scalability


get-children-with-watch
===========

This prog spawns N clients, each of which will call:

```
getChildren(path, watch: true)
```

for the given path, setting a watch as well. Every time the watch
fires, the call will be reissued (thus, resetting the watch). Depending
on how you'd like to run this, you can distribute the creation of clients
among a given set of processes. For example, to create 10k clients with
1k clients per process you can do:

```
$ ./get-children-with-watch --num-clients 1000 --num-procs 10 --watched-paths / localhost:2181
```

By default, each (child) process will start 4 (additional) threads:

* a client creator thread that will exit once it's done creating clients
* a __poller__ thread (which calls epoll() to check on the sockets)
* an __interests__ thread (which calls zookeeper_interests() on handlers to
  check for pings and such)
* a __worker__ thread which will call zookeeper_process() when the epoll() thread
  indicates (through a queue) that there is work to be done

If you want more __worker__ threads (i.e.: because of slow network), you can try:

```
$ ./get-children-with-watch --num-clients 1000 --num-procs 10 --num-workers 5 --watched-paths / localhost:2181
```

To check the full set of available pararmeters use (surprise surprise):

```
$ ./get-children-with-watch --help
```
