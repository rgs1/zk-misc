CC = gcc
CFLAGS = -Wall
ZK_CFLAGS = -I/tmp/zookeeper-libs/include/zookeeper
ZK_LDFLAGS = -L/tmp/zookeeper-libs/lib -Wl,-rpath=/tmp/zookeeper-libs/lib -lpthread -lzookeeper_st

SOURCES = clients.c queue.c dict.c list.c util.c get-children-with-watch.c create-ephemerals.c
OBJECTS = $(SOURCES:.c=.o) queue-test.o
EXECUTABLES = $(SOURCES:.c=) queue-test dict-test list-test

clients.o: clients.c clients.h
	$(CC) $(CFLAGS) $(ZK_CFLAGS) -c $< -o $@

queue.o: queue.c queue.h
	$(CC) $(CFLAGS) -c $< -o $@

dict.o: dict.c dict.h
	$(CC) $(CFLAGS) -c $< -o $@

list.o: list.c list.h
	$(CC) $(CFLAGS) -c $< -o $@

util.o: util.c util.h
	$(CC) $(CFLAGS) -c $< -o $@

queue-test.o: queue.c queue.h
	$(CC) $(CFLAGS) -DRUN_TESTS -c $< -o $@

queue-test: queue-test.o util.o
	$(CC) $(CFLAGS) -DRUN_TESTS -lpthread $^ -o $@

dict-test.o: dict.c dict.h
	$(CC) $(CFLAGS) -DRUN_TESTS -c $< -o $@

dict-test: dict-test.o util.o
	$(CC) $(CFLAGS) -DRUN_TESTS -lpthread $^ -o $@

list-test.o: list.c list.h
	$(CC) $(CFLAGS) -DRUN_TESTS -c $< -o $@

list-test: list-test.o util.o
	$(CC) $(CFLAGS) -DRUN_TESTS -lpthread $^ -o $@

get-children-with-watch.o: get-children-with-watch.c
	$(CC) $(CFLAGS) $(ZK_CFLAGS) -c $< -o $@

get-children-with-watch: clients.o queue.o util.o get-children-with-watch.o
	$(CC) $(ZK_LDFLAGS) $^ -o $@

create-ephemerals.o: create-ephemerals.c
	$(CC) $(CFLAGS) $(ZK_CFLAGS) -c $< -o $@

create-ephemerals: clients.o queue.o util.o create-ephemerals.o
	$(CC) $(CFLAGS) $(ZK_CFLAGS) $(ZK_LDFLAGS) $^ -o $@

clean:
	rm -rf $(OBJECTS) $(EXECUTABLES)
