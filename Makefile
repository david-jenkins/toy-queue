CCFLAGS=-ggdb -O3 -Wall -Wpedantic -Werror -std=gnu11 -finline-functions -fsanitize=address -D_GNU_SOURCE -lrt -lm -lpthread
LDFLAGS=-lasan

all: test client client2

test: test.c queue.o mutex.o | queue.h mutex.h
	$(CC) $(CCFLAGS) -o $@ $^ $(LDFLAGS)

client: test.c queue.o mutex.o | queue.h mutex.h
	$(CC) $(CCFLAGS) -DCLIENT -o $@ $^ $(LDFLAGS)

client2: test.c queue.o mutex.o | queue.h mutex.h
	$(CC) $(CCFLAGS) -DCLIENT -DTELEM -o $@ $^ $(LDFLAGS)
	
mutex.o: mutex.c | mutex.h 
	$(CC) $(CCFLAGS) -lpthread -fPIC -o $@ -c $^

queue.o: queue.c | queue.h queue_internal.h
	$(CC) $(CCFLAGS) -o $@ -c $^

clean:
	rm -f queue.o mutex.o client2 client test
