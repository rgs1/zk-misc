CC = gcc
CFLAGS = -Wall
ZK_CFLAGS = -I/tmp/zookeeper-libs/include/zookeeper
ZK_LDFLAGS = -L/tmp/zookeeper-libs/lib -Wl,-rpath=/tmp/zookeeper-libs/lib -lpthread -lzookeeper_st

SOURCES = zk-follow-server-set.c zk-epoll.c readonly.c queue.c
OBJECTS = $(SOURCES:.c=.o)
EXECUTABLES = $(SOURCES:.c=)

zk-follow-server-set: zk-follow-server-set.o
	$(CC) $(ZK_LDFLAGS) $< -o $@

zk-follow-server-set.o: zk-follow-server-set.c
	$(CC) $(CFLAGS) $(ZK_CFLAGS) -c $< -o $@

zk-epoll: zk-epoll.c
	$(CC) $(CFLAGS) $(ZK_CFLAGS) $(ZK_LDFLAGS) $< -o $@

readonly: readonly.c
	$(CC) $(CFLAGS) $(ZK_CFLAGS) $(ZK_LDFLAGS) $< -o $@

queue.o: queue.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJECTS) $(EXECUTABLES)
