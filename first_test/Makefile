CCFLAGS=-Wall -Wpedantic -Werror -std=gnu11 -finline-functions -fsanitize=address -D_GNU_SOURCE -lrt -lpthread
LDFLAGS= -lasan

all: test client

test: test.c queue.o mutex.o | queue.h mutex.h
	$(CC) $(CCFLAGS) -o $@ $^ $(LDFLAGS)

client: client.c queue.o mutex.o | queue.h mutex.h
	$(CC) $(CCFLAGS) -o $@ $^ $(LDFLAGS)
	
mutex.o: mutex.c | mutex.h 
	$(CC) $(CCFLAGS) -lpthread -fPIC -o $@ -c $^

queue.o: queue.c | queue.h queue_internal.h
	$(CC) $(CCFLAGS) -o $@ -c $^

clean:
	rm -f queue.o test
