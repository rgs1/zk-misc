CC = gcc
CFLAGS = -Wall
ZK_CFLAGS = -I/tmp/zookeeper-libs/include/zookeeper
ZK_LDFLAGS = -L/tmp/zookeeper-libs/lib -Wl,-rpath=/tmp/zookeeper-libs/lib -lpthread -lzookeeper_st

SOURCES = zk-watchers.c zk-epoll.c readonly.c queue.c
OBJECTS = $(SOURCES:.c=.o)
EXECUTABLES = $(SOURCES:.c=) queue-test

zk-watchers: zk-watchers.o queue.o
	$(CC) $(ZK_LDFLAGS) $^ -o $@

zk-watchers.o: zk-watchers.c
	$(CC) $(CFLAGS) $(ZK_CFLAGS) -c $< -o $@

zk-epoll: zk-epoll.c
	$(CC) $(CFLAGS) $(ZK_CFLAGS) $(ZK_LDFLAGS) $< -o $@

readonly: readonly.c
	$(CC) $(CFLAGS) $(ZK_CFLAGS) $(ZK_LDFLAGS) $< -o $@

queue.o: queue.c queue.h
	$(CC) $(CFLAGS) -c $< -o $@

queue-test: queue.c queue.h
	$(CC) $(CFLAGS) -DRUN_TESTS -lpthread $< -o $@

clean:
	rm -rf $(OBJECTS) $(EXECUTABLES)
