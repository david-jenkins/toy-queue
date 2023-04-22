#include <stdio.h>
#include <unistd.h>
#include <signal.h>

#include "queue.h"

#define BUFFER_SIZE (getpagesize())
#define NUM_THREADS (1)
#define MESSAGES_PER_THREAD (getpagesize() * 2)

#define ENTRY_SIZE 1600*1096
#define NSEC_PER_SEC 1000000000

int go = 1;

void sigint_handler(int sig_num)
{
    /* Reset handler to catch SIGINT next time.
       Refer http://en.cppreference.com/w/c/program/signal */
    printf("\n User provided signal handler for Ctrl+C \n");
    go = 0;
    /* Do a graceful cleanup of the program like: free memory/resources/etc and exit */

}

void *consumer_loop(void *arg) {
    queue_t *q = (queue_t *) arg;
    size_t count = 0;
    // size_t i;
    // for(i = 0; i < MESSAGES_PER_THREAD; i++){
    char *buffer;
    buffer = malloc(ENTRY_SIZE);
    struct timespec *to;
    double d0;
    double d1;
    to = (struct timespec *)buffer;
    struct timespec t1;
    while(go) {
        #ifdef TELEM
        queue_get(q, (uint8_t *) buffer, ENTRY_SIZE);
        clock_gettime(CLOCK_REALTIME, &t1);
        // printf("got %d\n",to->tv_sec);
        // printf("got %d\n",to->tv_nsec);
        printf("%lld.%.9ld\n", (long long)to->tv_sec, to->tv_nsec);
        d0 = ((double)(to->tv_sec) + ((double)(to->tv_nsec) / NSEC_PER_SEC));
        d1 = ((double)(t1.tv_sec) + ((double)(t1.tv_nsec) / NSEC_PER_SEC));
        printf("%e\n",d1-d0);
        // usleep(80000);
        #else
        queue_get_last(q, (uint8_t *) buffer, ENTRY_SIZE);
        clock_gettime(CLOCK_REALTIME, &t1);
        // printf("got %d\n",to->tv_sec);
        // printf("got %d\n",to->tv_nsec);
        printf("%lld.%.9ld\n", (long long)to->tv_sec, to->tv_nsec);
        d0 = ((double)(to->tv_sec) + ((double)(to->tv_nsec) / NSEC_PER_SEC));
        d1 = ((double)(t1.tv_sec) + ((double)(t1.tv_nsec) / NSEC_PER_SEC));
        printf("%e\n",d1-d0);
        // usleep(40000);
        #endif
        
        count++;
        
    }
    return (void *) count;
}

void *publisher_loop(void *arg) {
    queue_t *q = (queue_t *) arg;
    size_t i = 47;
    for(i = 0; i < NUM_THREADS * MESSAGES_PER_THREAD; i++){
        queue_put(q, (uint8_t *) &i, sizeof(size_t));
    }
    return (void *) i;
}

int main(int argc, char *argv[]){

    signal(SIGINT, sigint_handler);

    queue_t q;
    // queue_init(&q, BUFFER_SIZE);
    queue_open(&q, "george");

    // pthread_t publisher;
    pthread_t consumers[NUM_THREADS];

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    
    // pthread_create(&publisher, &attr, &publisher_loop, (void *) &q);
    
    intptr_t i;
    for(i = 0; i < NUM_THREADS; i++){
        pthread_create(&consumers[i], &attr, &consumer_loop, (void *) &q);
    }
    
    // intptr_t sent;
    // pthread_join(publisher, (void **) &sent);
    // printf("publisher sent %ld messages\n", sent);
    
    intptr_t recd[NUM_THREADS];
    for(i = 0; i < NUM_THREADS; i++){
        pthread_join(consumers[i], (void **) &recd[i]);
        printf("consumer %ld received %ld messages\n", i, recd[i]);
    }
    
    pthread_attr_destroy(&attr);
    
    queue_close(&q);
    
    printf("FINISHED\n");
    
    return 0;
}
