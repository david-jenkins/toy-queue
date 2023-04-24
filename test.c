#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <math.h>

#include "queue.h"

#define BUFFER_SIZE (10000*getpagesize())
#define NUM_THREADS (1)
#define MESSAGES_PER_THREAD (getpagesize() * 2)

int go = 1;

// #define ENTRY_SIZE 4096
#define ENTRIES 50
#define ENTRY_SIZE (1600*1096)
// #define ENTRY_SIZE (264*242*2)
// #define ENTRY_SIZE (4096)
// #define ENTRY_SIZE (264*242*2)
// #define ENTRY_SIZE (4096)
#define NSEC_PER_SEC 1000000000

/*
works with large buffers....
now need to make sure it doesn't hang when the buffer is full...
it should just overwrite things....

also the client should have a timeout incase nothing gets put there...

and then work out how to do the two heads/tails thing....

*/

void sigint_handler(int sig_num)
{
    /* Reset handler to catch SIGINT next time.
       Refer http://en.cppreference.com/w/c/program/signal */
    printf("\n User provided signal handler for Ctrl+C \n");
    go = 0;
    /* Do a graceful cleanup of the program like: free memory/resources/etc and exit */

}



#define SIZES (sizeof(size_t)+sizeof(struct timespec))
#ifdef TELEM
#define SAMPLES 100
#else
#define SAMPLES 100
#endif

void *consumer_loop(void *arg) {
    queue_t *q = (queue_t *) arg;
    int i = 0;
    size_t count0 = 0;
    size_t count1 = 0;
    char *buffer;// = 0;// = malloc(ENTRY_SIZE);
    double d0;
    double d1;
    struct timespec t0;
    struct timespec t1;
    char *time1 = malloc(SAMPLES*SIZES);
    char *time2 = malloc(SAMPLES*SIZES);
    size_t bytes_read;
    while(go) {
    // for (i=0; i<SAMPLES; i++) {
        // printf("getting data....\n");
        #ifdef TELEM
        bytes_read = queue_get_last(q, (uint8_t **) &buffer);
        #else
        bytes_read = queue_get(q, (uint8_t **) &buffer);
        // usleep(11000); //  doing work
        #endif
        clock_gettime(CLOCK_REALTIME, &t1);
        if (bytes_read>=SIZES) {
            memcpy(&time1[i*SIZES], buffer, SIZES);
        } else {
            printf("not enough data...\n");
            if (q->queue_info->entries == 0)
                break;
        }
        memcpy(&time2[i*SIZES], &count0, sizeof(size_t));
        memcpy(&time2[i*SIZES+sizeof(size_t)], &t1, sizeof(struct timespec));
        count0++;
        if (go==0){
            break;
        }
        i = (i+1)%SAMPLES;
    }
    double mean = 0;
    double data[SAMPLES-2];
    double stdDev = 0;
    for (i=2; i<SAMPLES; i++) {
        memcpy(&count0, &time1[i*SIZES], sizeof(size_t));
        memcpy(&t0, &time1[i*SIZES+sizeof(size_t)], sizeof(struct timespec));
        memcpy(&count1, &time2[i*SIZES], sizeof(size_t));
        memcpy(&t1, &time2[i*SIZES+sizeof(size_t)], sizeof(struct timespec));
        // printf("got %d\n",to->tv_sec);
        // printf("got %d\n",to->tv_nsec);
        printf("%lld.%.9ld\n", (long long)t0.tv_sec, t0.tv_nsec);
        printf("%lld.%.9ld\n", (long long)t1.tv_sec, t1.tv_nsec);
        d0 = ((double)(t0.tv_sec) + ((double)(t0.tv_nsec) / NSEC_PER_SEC));
        d1 = ((double)(t1.tv_sec) + ((double)(t1.tv_nsec) / NSEC_PER_SEC));
        printf("for count0=%zu, count1=%zu\n",count0, count1);
        printf("%e\n\n",d1-d0);
        mean+=(d1-d0);
        data[i-2] = d1-d0;
        // usleep(80000);
    }
    mean = mean/((double)(SAMPLES-2));
    for (i = 0; i < SAMPLES-2; i++) {
        stdDev += pow(data[i] - mean, 2);
    }
    stdDev = sqrt(stdDev / (SAMPLES-10));
    printf("mean time = %e += %e (%e Hz)\n", mean, stdDev/sqrt((double)(SAMPLES-2)), 1.0/mean);
    
    return (void *) count1;
}

void *publisher_loop(void *arg) {
    queue_t *q = (queue_t *) arg;
    char *buffer;// = 0;// = malloc(ENTRY_SIZE);
    // char *buffer = malloc(ENTRY_SIZE);
    size_t count = 0;
    struct timespec to;
    while(go) {
        queue_pop(q, (uint8_t **) &buffer);
        // printf("popping a buf\n");
        usleep(250); //  doing work
        clock_gettime(CLOCK_REALTIME, &to);
        memcpy(&(buffer[0]), &count, sizeof(size_t));
        memcpy(&(buffer[sizeof(size_t)]), &to, sizeof(struct timespec));
        // printf("pushing the buf\n");
        count++;
        queue_push(q, SIZES);
        // queue_put(q, (uint8_t *) buffer, SIZES);
    }
    // free(buffer);
    return (void *) count;
}

int main(int argc, char *argv[]){
    
    signal(SIGINT, sigint_handler);
    int num;
    if( argc == 2 ) {
        char *a = argv[1];
        num = atoi(a);
    } else {
        printf("One argument expected.\n");
        return 0;
    }
    
    if (num==0) {
        printf("doing first\n");
    } else if (num<0) {
        printf("doing last\n");
    } else {
        printf("doing %dth\n",num);
    }
    
    char shm_name [10];

    snprintf ( shm_name, 10, "george%03d", num );
    
    printf("using a queue called %s\n",shm_name);

    queue_t q;
#ifdef CLIENT

    if (queue_open(&q, shm_name)==1) {
        printf("Can't open shared memory region...\n");
        return 1;
    }
    
    pthread_t consumers[NUM_THREADS];

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    
    intptr_t i;
    for(i = 0; i < NUM_THREADS; i++){
        pthread_create(&consumers[i], &attr, &consumer_loop, (void *) &q);
    }
    
    intptr_t recd[NUM_THREADS];
    for(i = 0; i < NUM_THREADS; i++){
        pthread_join(consumers[i], (void **) &recd[i]);
        printf("consumer %ld received %ld messages\n", i, recd[i]);
    }
    
    pthread_attr_destroy(&attr);
    
    queue_close(&q);
#else
    queue_init(&q, shm_name, ENTRY_SIZE, ENTRIES);
    usleep(500000);
    pthread_t publisher;
    // pthread_t consumers[NUM_THREADS];

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    
    // sleep(1);
    pthread_create(&publisher, &attr, &publisher_loop, (void *) &q);
    
    intptr_t sent;
    pthread_join(publisher, (void **) &sent);
    printf("publisher sent %ld messages\n", sent);
    
    pthread_attr_destroy(&attr);
    
    queue_destroy(&q);
#endif
    
    printf("PROGRAME DONE\n");
    
    return 0;
}
