#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>

#include <linux/memfd.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <sys/stat.h>
#include <math.h>

#include "queue_internal.h"

/** Convenience wrapper around memfd_create syscall, because apparently this is
  * so scary that glibc doesn't provide it...
  */
#if __GLIBC__ < 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 27)
static inline int memfd_create(const char *name, unsigned int flags) {
    return syscall(__NR_memfd_create, name, flags);
}
#endif

/** Convenience wrappers for erroring out
  */
static inline void queue_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "queue error: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    exit(0);
}
static inline void queue_error_errno(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "queue error: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, " (errno %d)\n", errno);
    va_end(args);
    exit(0);
}

// union {
//     message_t *m;
//     char 
// }

void queue_init(queue_t *q, char *name, size_t entry_size, size_t entries) {

    /* We're going to use a trick where we mmap two adjacent pages (in virtual memory) that point to the
     * same physical memory. This lets us optimize memory access, by virtue of the fact that we don't need
     * to even worry about wrapping our pointers around until we go through the entire buffer. Too bad this
     * isn't portable, because it's so fun.
     */
    
    size_t entries_pow2 = 1;
    
    while(entries_pow2 < entries)
        entries_pow2*=2;
    
    size_t full_entry_size = getpagesize()*((entry_size+sizeof(message_t)+getpagesize()-1)/getpagesize());
    size_t size = entries_pow2*full_entry_size;
    
    // printf("full size is %zu\n",size);
    // size_t power = 1;
    // while(power < size)
    //     power*=2;
    // printf("size for next power of 2 = %zu\n",power);
    // printf("entries for power of 2 size = %zu\n",power/full_entry_size);

    entry_size = full_entry_size - sizeof(message_t);
    
    printf("message size is %d\n", (int)sizeof(message_t));
    printf("page size is %d\n", (int)getpagesize());
    printf("full size is %d\n", (int)size);
    printf("entry size is %d\n", (int)entry_size);
    printf("full_entry size is %d\n", (int)full_entry_size);
    printf("entries_pow2 is %d\n", (int)entries_pow2);
    printf("shared_t size is %d\n", (int)sizeof(shared_t));
    strncpy(q->name, name, 16);
    printf("buffer name is %s\n", q->name);
    // Check that the requested size is a multiple of a page. If it isn't, we're in trouble.
    if(size % getpagesize() != 0) {
        queue_error("Requested size (%lu) is not a multiple of the page size (%d)", size, getpagesize());
    }
    
    // check if shared memory region already exists
    

    // Create an anonymous file backed by memory
    // if((q->fd = memfd_create("queue_region", 0)) == -1){
    // open a shared memory region
    if((q->fd = shm_open(name, O_RDWR|O_CREAT|O_EXCL, 0777)) == -1){
        queue_error_errno("Could not obtain anonymous file");
    }
    
    printf("full size is %d\n", (int)size);
    // Set buffer size
    if(ftruncate(q->fd, size + getpagesize()) != 0){
        queue_error_errno("Could not set size of anonymous file");
    }
    
    if((q->queue_info = mmap(NULL, getpagesize(), PROT_READ | PROT_WRITE, MAP_SHARED, q->fd, 0)) == MAP_FAILED){
        queue_error_errno("Could not map buffer into virtual memory 1");
    }
    
    // Ask mmap for a good address
    if((q->buffer = mmap(NULL, 2 * size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, getpagesize())) == MAP_FAILED){
        queue_error_errno("Could not allocate virtual memory");
    }
    
    // Mmap first region
    if(mmap(q->buffer, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, q->fd, getpagesize()) == MAP_FAILED){
        queue_error_errno("Could not map buffer into virtual memory 2");
    }
    
    // Mmap second region, with exact address
    if(mmap(q->buffer + size, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, q->fd, getpagesize()) == MAP_FAILED){
        queue_error_errno("Could not map buffer into virtual memory 3");
    }
    
    pthread_mutexattr_t att;
    pthread_mutexattr_init(&att);
    pthread_mutexattr_setpshared(&att, PTHREAD_PROCESS_SHARED);
    
    pthread_condattr_t cattr;
    pthread_condattr_init(&cattr);
    pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);
    
    // Initialize synchronization primitives
    if(mutex_init(&q->queue_info->write_mutex, &att) != 0){
        queue_error_errno("Could not initialize mutex");
    }
    if(mutex_init(&q->queue_info->read_mutex, &att) != 0){
        queue_error_errno("Could not initialize mutex");
    }
    
    if(cond_init(&q->queue_info->read_cond, &cattr) != 0){
        queue_error_errno("Could not initialize condition variable");
    }
    if(cond_init(&q->queue_info->write_cond, &cattr) != 0){
        queue_error_errno("Could not initialize condition variable");
    }
    
    // Initialize remaining members
    q->size = size;
    q->full_entry_size = full_entry_size;
    q->entries = entries_pow2;
    // q->queue_info->entry_size = entry_size;
    q->queue_info->full_entry_size = full_entry_size;
    q->queue_info->entries = entries_pow2;
    q->queue_info->entry_mask = entries_pow2-1;
    q->queue_info->size = size;
    q->queue_info->head = 0;
    q->queue_info->tail = 0;
    q->queue_info->head_seq = 0;
    q->queue_info->tail_seq = 0;
    q->queue_info->last_tail_seq = 0;
    q->queue_info->last_tail = 0;
    
    q->queue_info->readers = 0;
    
    pthread_condattr_destroy(&cattr);
    pthread_mutexattr_destroy(&att);
    
}

int queue_open(queue_t *q, char *name) {
    if((q->fd = shm_open(name, O_RDWR, 0777)) == -1){
        printf("Could not obtain anonymous file\n");
        return 1;
    }
    
    if((q->queue_info = mmap(NULL, getpagesize(), PROT_READ | PROT_WRITE, MAP_SHARED, q->fd, 0)) == MAP_FAILED){
        queue_error_errno("Could not map buffer into virtual memory 1");
    }
    
    q->entries = q->queue_info->entries;
    q->full_entry_size = q->queue_info->full_entry_size;
    q->size = (q->full_entry_size)*q->entries;
    printf("got size of buffer = %d\n",(int)q->size);
    mutex_lock(&q->queue_info->read_mutex);
    q->queue_info->readers++;
    mutex_unlock(&q->queue_info->read_mutex);
    
    // Ask mmap for a good address
    if((q->buffer = mmap(NULL, 2 * q->size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED){
        queue_error_errno("Could not allocate virtual memory");
    }
    
    // Mmap first region
    if(mmap(q->buffer, q->size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, q->fd, getpagesize()) == MAP_FAILED){
        queue_error_errno("Could not map buffer into virtual memory 2");
    }
    
    // Mmap second region, with exact address
    if(mmap(q->buffer + q->size, q->size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, q->fd, getpagesize()) == MAP_FAILED){
        queue_error_errno("Could not map buffer into virtual memory 3");
    }
    
    return 0;
}

void queue_close(queue_t *q) {
    
    mutex_lock(&q->queue_info->read_mutex);
    q->queue_info->readers--;
    mutex_unlock(&q->queue_info->read_mutex);
    
    if(munmap(q->buffer + q->size, q->size) != 0){
        queue_error_errno("Could not unmap buffer");
    }
    
    if(munmap(q->buffer, q->size) != 0){
        queue_error_errno("Could not unmap buffer");
    }
    
    if(munmap(q->queue_info, getpagesize()) != 0)
        queue_error_errno("Could not unmap shared_info");
    
    if(close(q->fd) != 0){
        queue_error_errno("Could not close anonymous file");
    }
    
}

size_t queue_destroy(queue_t *q) {

    struct timespec to;
    to.tv_sec = 1;
    to.tv_nsec = 0;
    
    mutex_lock(&q->queue_info->read_mutex);
    q->queue_info->entries = 0;
    while (q->queue_info->readers) {
        printf("not all readers disconnected.. waiting before closing\n");
#ifdef POSIXMUTEX
        clock_gettime(CLOCK_REALTIME, &to);
        to.tv_sec += 1;
#endif
        int retval = cond_timedwait(&q->queue_info->write_cond, &q->queue_info->read_mutex, &to);
        if(retval==ETIMEDOUT) {
            printf("timeout in queue_destroy\n");
        } else if (retval==-1) {
            perror("Error: ");
        }
    }
    mutex_unlock(&q->queue_info->read_mutex);
    
    if(munmap(q->buffer + q->size, q->size) != 0){
        queue_error_errno("Could not unmap buffer");
    }
    
    if(munmap(q->buffer, q->size) != 0){
        queue_error_errno("Could not unmap buffer");
    }
    
    if(mutex_destroy(&q->queue_info->write_mutex) != 0){
        queue_error_errno("Could not destroy mutex");
    }
    if(mutex_destroy(&q->queue_info->read_mutex) != 0){
        queue_error_errno("Could not destroy mutex");
    }
    
    if(cond_destroy(&q->queue_info->read_cond) != 0){
        queue_error_errno("Could not destroy condition variable");
    }
    
    if(cond_destroy(&q->queue_info->write_cond) != 0){
        queue_error_errno("Could not destroy condition variable");
    }
    
    if(munmap(q->queue_info, getpagesize()) != 0)
        queue_error_errno("Could not unmap shared_info");
    
    if(shm_unlink(q->name))
        printf("Unable to unlink /dev/shm%s\n","george");
    
    if(close(q->fd) != 0){
        queue_error_errno("Could not close anonymous file");
    }
    return 1;
}

void queue_pop(queue_t *q, uint8_t **buffer) {
    
    // message_t m;
    
    mutex_lock(&q->queue_info->write_mutex);
    
    if (q->queue_info->last_tail_seq < q->queue_info->tail_seq) {
        // printf("consumer hasn't read yet...\n");
        q->queue_info->last_tail = q->queue_info->tail+1;
        // if(q->queue_info->last_tail >= q->queue_info->entries) {
        //     q->queue_info->last_tail -= q->queue_info->entries;
        // }
    }
    
    // // signal the telemetry thread, the previous frame has either been used or missed
    // // mutex_lock(&q->queue_info->read_mutex);
    // cond_signal(&q->queue_info->write_cond);
    // mutex_unlock(&q->queue_info->read_mutex);
    
    // Check if buffer is full
    mutex_lock(&q->queue_info->read_mutex);
    if ( (q->queue_info->last_tail - q->queue_info->head) >= q->queue_info->entries) {

        // printf("buffer overan, overwriting...\n");
        // memcpy(&m, &q->buffer[q->queue_info->head*q->queue_info->full_entry_size], sizeof(message_t));
        q->queue_info->head += 1;
        q->queue_info->head_seq++;
        
        // if(q->queue_info->head >= q->queue_info->entries) {
        //     q->queue_info->head -= q->queue_info->entries;
        //     q->queue_info->tail -= q->queue_info->entries;
        //     q->queue_info->last_tail -= q->queue_info->entries;
        // }
        
        // When read buffer moves into 2nd memory region, we can reset to the 1st region
        
    }
    mutex_unlock(&q->queue_info->read_mutex);
    
    // Construct header
    // message_t m;
    // m.len = size;
    // m.seq = q->queue_info->tail_seq++;
    // m.miss = 0;
    
    // // Write message
    // memcpy(&q->buffer[q->queue_info->tail                    ], &m,     sizeof(message_t));
    // memcpy(&q->buffer[q->queue_info->tail + sizeof(message_t)], buffer, size             );
    // printf("q->queue_info->head = %zu\n",q->queue_info->head);
    // printf("q->queue_info->tail = %zu\n",q->queue_info->tail);
    // printf("q->queue_info->last_tail = %zu\n",q->queue_info->last_tail);
    // printf("q->queue_info->full_entry_size = %zu\n",q->queue_info->full_entry_size);
    *buffer = &(q->buffer[(q->queue_info->last_tail & q->queue_info->entry_mask)*q->queue_info->full_entry_size + sizeof(message_t)]);
    // Increment write index
    // q->queue_info->last_tail = q->queue_info->tail;
    // q->queue_info->tail  += size + sizeof(message_t);
    
    mutex_unlock(&q->queue_info->write_mutex);
    // cond_signal(&q->queue_info->read_cond);
}

void queue_push(queue_t *q, size_t size) {
    
    message_t m;
    
    mutex_lock(&q->queue_info->write_mutex);
    
    if (q->queue_info->last_tail_seq < q->queue_info->tail_seq) {
        // printf("slow consumer...\n");
        size_t index = (q->queue_info->tail & q->queue_info->entry_mask)*q->queue_info->full_entry_size;
        memcpy(&m, &q->buffer[index], sizeof(message_t));
        m.miss = 1;
        memcpy(&q->buffer[index], &m, sizeof(message_t));
        q->queue_info->tail += 1;
        // cond_signal(&q->queue_info->write_cond);
        // how to notify - set the miss flag which can be seen by the telem reader
    }
    
    // if (q->queue_info->last_tail_seq < q->queue_info->tail_seq) {
    //     // printf("slow consumer...\n");
    //     memcpy(&m, &q->buffer[q->queue_info->last_tail*q->queue_info->full_entry_size], sizeof(message_t));
    //     m.miss=1;
    //     memcpy(&q->buffer[q->queue_info->last_tail*q->queue_info->full_entry_size], &m, sizeof(message_t));
    //     q->queue_info->tail  += 1;
    //     // cond_signal(&q->queue_info->write_cond);
    //     // how to notify - set the miss flag which can be seen by the telem reader
    // }
    
    cond_signal(&q->queue_info->write_cond);
    // if (q->queue_info->last_tail_seq < q->queue_info->tail_seq) {
    //     // printf("slow consumer...\n");
    //     memcpy(&m, &q->buffer[q->queue_info->last_tail], sizeof(message_t));
    //     m.miss=1;
    //     memcpy(&q->buffer[q->queue_info->last_tail], &m, sizeof(message_t));
    //     q->queue_info->tail  += m.len + sizeof(message_t);
    //     // cond_signal(&q->queue_info->write_cond);
    //     // how to notify - set the miss flag which can be seen by the telem reader
    // }
    
    // signal the telemetry thread, the previous frame has either been used or missed
    // mutex_lock(&q->queue_info->read_mutex);
    // cond_signal(&q->queue_info->write_cond);
    // mutex_unlock(&q->queue_info->read_mutex);
    
    // // Check if buffer is full
    // if (q->queue_info->size - (q->queue_info->tail - q->queue_info->head) < size + sizeof(message_t)) {

    //     // printf("buffer overan, overwriting...\n");
    //     memcpy(&m, &q->buffer[q->queue_info->head], sizeof(message_t));
    //     q->queue_info->head += m.len + sizeof(message_t);
    //     q->queue_info->head_seq++;
        
    //     // When read buffer moves into 2nd memory region, we can reset to the 1st region
    //     if(q->queue_info->head >= q->queue_info->size) {
    //         q->queue_info->head -= q->queue_info->size;
    //         q->queue_info->tail -= q->queue_info->size;
    //     }
    // }
    
    // Construct header
    // message_t m;
    m.len = size;
    // m.seq = q->queue_info->tail_seq++;
    // m.miss = 0;
    m.seq = q->queue_info->tail_seq++;
    m.miss = 0;
    
    // Write message
    memcpy(&q->buffer[(q->queue_info->last_tail & q->queue_info->entry_mask)*q->queue_info->full_entry_size], &m,     sizeof(message_t));
    
    // // Write message
    // memcpy(&q->buffer[q->queue_info->tail                    ], &m,     sizeof(message_t));
    // memcpy(&q->buffer[q->queue_info->tail + sizeof(message_t)], buffer, size             );
    
    // Increment write index
    q->queue_info->last_tail = q->queue_info->tail;
    // q->queue_info->tail  += size + sizeof(message_t);
    
    cond_signal(&q->queue_info->read_cond);
    mutex_unlock(&q->queue_info->write_mutex);
    
}

void queue_put(queue_t *q, uint8_t *buffer, size_t size) {
    mutex_lock(&q->queue_info->write_mutex);
    
    message_t m;
    
    if (q->queue_info->last_tail_seq < q->queue_info->tail_seq) {
        printf("slow consumer...\n");
        size_t index = (q->queue_info->tail & q->queue_info->entry_mask)*q->queue_info->full_entry_size;
        memcpy(&m, &q->buffer[index], sizeof(message_t));
        m.miss=1;
        memcpy(&q->buffer[index], &m, sizeof(message_t));
        q->queue_info->tail  += 1;
        // how to notify
    }
    
    mutex_lock(&q->queue_info->read_mutex);
    cond_signal(&q->queue_info->write_cond);
    
    // Check if buffer is full
    if ((q->queue_info->tail - q->queue_info->head) >= q->queue_info->entries) {

        // printf("buffer overan, overwriting...\n");
        // memcpy(&m, &q->buffer[q->queue_info->head*q->queue_info->full_entry_size], sizeof(message_t));
        q->queue_info->head += 1;
        q->queue_info->head_seq++;
        
        // if(q->queue_info->head >= q->queue_info->entries) {
        //     q->queue_info->head -= q->queue_info->entries;
        //     q->queue_info->tail -= q->queue_info->entries;
        // }
        // When read buffer moves into 2nd memory region, we can reset to the 1st region
        
    }
    mutex_unlock(&q->queue_info->read_mutex);
    
    
    // Construct header
    // message_t m;
    m.len = size;
    m.seq = q->queue_info->tail_seq++;
    m.miss = 0;
    size_t index = (q->queue_info->tail & q->queue_info->entry_mask)*q->queue_info->full_entry_size;
    // Write message
    memcpy(&q->buffer[index                    ], &m,     sizeof(message_t));
    memcpy(&q->buffer[index + sizeof(message_t)], buffer, size             );
    
    // Increment write index
    // q->queue_info->last_tail = q->queue_info->tail;
    // q->queue_info->tail  += size + sizeof(message_t);
    
    cond_signal(&q->queue_info->read_cond);
    mutex_unlock(&q->queue_info->write_mutex);
}

size_t queue_get(queue_t *q, uint8_t **buffer) {
    /*
    This is for the real-time consumer
    */
    
    struct timespec to;
#ifdef POSIXMUTEX
    clock_gettime(CLOCK_REALTIME, &to);
#else
    to.tv_sec = 0;
    to.tv_nsec = 0;
#endif
    to.tv_sec += 2;
    message_t m;
    
    mutex_lock(&q->queue_info->write_mutex);

    // printf("cv->mid = %d\n",q->queue_info->read_cond.mid);
    // printf("mutex->id = %d\n",q->queue_info->write_mutex.id);
    // printf("cv->seq = %d\n",q->queue_info->read_cond.seq);
    // printf("cv->bcast = %d\n",q->queue_info->read_cond.bcast);
    // printf("cv->flags = %d\n",q->queue_info->read_cond.flags);
    
    // printf("starting while in get_last...\n");
    while (q->queue_info->last_tail_seq==q->queue_info->tail_seq) {
        // printf("starting wait in get_last...\n");
        int retval = cond_timedwait(&q->queue_info->read_cond, &q->queue_info->write_mutex, &to);
        // printf("done wait in get_last = %d...\n",retval);
        if ( retval == ETIMEDOUT ) {
            printf("timeout in get\n");
            mutex_unlock(&q->queue_info->write_mutex);
            return 0;
        } else if (retval == -1) {
            perror("Error: ");
        }
    }
    
    if (q->queue_info->tail_seq!=q->queue_info->last_tail_seq+1) {
        printf("missed buffer...\n");
        q->queue_info->last_tail_seq = q->queue_info->tail_seq;
        mutex_unlock(&q->queue_info->write_mutex);
        return 0;
    }
    
    // printf("waited in get_last...\n");
    size_t index = (q->queue_info->tail & q->queue_info->entry_mask)*q->queue_info->full_entry_size;
    memcpy(&m, &q->buffer[index], sizeof(message_t));
    // memcpy(buffer, &q->buffer[q->queue_info->last_tail + sizeof(message_t)], m.len);
    *buffer = &q->buffer[index + sizeof(message_t)];
    q->queue_info->last_tail_seq = q->queue_info->tail_seq;
    q->queue_info->tail  += 1;
    // cond_signal(&q->queue_info->write_cond);
    mutex_unlock(&q->queue_info->write_mutex);
    return m.len;
}

size_t queue_get_last(queue_t *q, uint8_t **buffer) {
    
    struct timespec to;
#ifdef POSIXMUTEX
    clock_gettime(CLOCK_REALTIME, &to);
#else
    to.tv_sec = 0;
    to.tv_nsec = 0;
#endif
    to.tv_sec += 2;
    // to.tv_sec = 2;
    message_t m;
    
    mutex_lock(&q->queue_info->read_mutex);
    
    // printf("cv->mid = %d\n",q->queue_info->write_cond.mid);
    // printf("mutex->id = %d\n",q->queue_info->read_mutex.id);
    // printf("cv->seq = %d\n",q->queue_info->write_cond.seq);
    // printf("cv->bcast = %d\n",q->queue_info->write_cond.bcast);
    // printf("cv->flags = %d\n",q->queue_info->write_cond.flags);

    // Wait for a message that we can successfully consume to reach the front of the queue
    // for(;;) {
        // Wait for a message to arrive
    while((q->queue_info->tail - q->queue_info->head) == 0){
        int retval = cond_timedwait(&q->queue_info->write_cond, &q->queue_info->read_mutex, &to);
        if(retval==ETIMEDOUT) {
            printf("timeout in get\n");
            mutex_unlock(&q->queue_info->read_mutex);
            return 0;
        }
    }
        
        // // Read message header
        // memcpy(&m, &q->buffer[q->queue_info->head], sizeof(message_t));
        
        // Message too long, wait for someone else to consume it
        // if(m.len > max){
        //     printf("buffer received too long... waiting for a shorter one...\n");
        //     while(q->queue_info->head_seq == m.seq) {
        //         if(cond_timedwait(&q->queue_info->read_cond, &q->queue_info->read_mutex, &to)==ETIMEDOUT) {
        //             printf("timeout in get part 2\n");
        //             mutex_unlock(&q->queue_info->read_mutex);
        //             return 0;
        //         }
        //     }
        //     continue;
        // }
        
        // We successfully consumed the header of a suitable message, so proceed
    //     break;
    // } 
    // Read message header
    size_t index = (q->queue_info->head & q->queue_info->entry_mask)*q->queue_info->full_entry_size;
    
    memcpy(&m, &q->buffer[index], sizeof(message_t));
    if (m.miss == 1) {
        printf("Frame %zu was missed...\n",m.seq);
    }
    // Read message body
    // memcpy(buffer, &q->buffer[q->queue_info->head + sizeof(message_t)], m.len);
    *buffer = &q->buffer[index + sizeof(message_t)];
    
    // Consume the message by incrementing the read pointer
    q->queue_info->head += 1;
    q->queue_info->head_seq++;
    
    // When read buffer moves into 2nd memory region, we can reset to the 1st region
    // if(q->queue_info->head >= q->queue_info->entries) {
    //     q->queue_info->head -= q->queue_info->entries;
    //     q->queue_info->tail -= q->queue_info->entries;
    // }
    
    // cond_signal(&q->queue_info->write_cond);
    mutex_unlock(&q->queue_info->read_mutex);
    
    return m.len;
}
