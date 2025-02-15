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

void queue_init(queue_t *q, char *name, size_t s) {

    /* We're going to use a trick where we mmap two adjacent pages (in virtual memory) that point to the
     * same physical memory. This lets us optimize memory access, by virtue of the fact that we don't need
     * to even worry about wrapping our pointers around until we go through the entire buffer. Too bad this
     * isn't portable, because it's so fun.
     */ 
    printf("page size is %d\n", (int)getpagesize());
    printf("shared_t size is %d\n", (int)sizeof(shared_t));
    // Check that the requested size is a multiple of a page. If it isn't, we're in trouble.
    if(s % getpagesize() != 0) {
        queue_error("Requested size (%lu) is not a multiple of the page size (%d)", s, getpagesize());
    }

    // Create an anonymous file backed by memory
    // if((q->fd = memfd_create("queue_region", 0)) == -1){
    if((q->fd = shm_open(name, O_RDWR|O_CREAT, 0777)) == -1){
        queue_error_errno("Could not obtain anonymous file");
    }
    
    // Set buffer size
    if(ftruncate(q->fd, s + getpagesize()) != 0){
        queue_error_errno("Could not set size of anonymous file");
    }
    
    if((q->queue_info = mmap(NULL, getpagesize(), PROT_READ | PROT_WRITE, MAP_SHARED, q->fd, 0)) == MAP_FAILED){
        queue_error_errno("Could not map buffer into virtual memory 1");
    }
    
    // Ask mmap for a good address
    if((q->buffer = mmap(NULL, 2 * s, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED){
        queue_error_errno("Could not allocate virtual memory");
    }
    
    // Mmap first region
    if(mmap(q->buffer, s, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, q->fd, getpagesize()) == MAP_FAILED){
        queue_error_errno("Could not map buffer into virtual memory 2");
    }
    
    // Mmap second region, with exact address
    if(mmap(q->buffer + s, s, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, q->fd, getpagesize()) == MAP_FAILED){
        queue_error_errno("Could not map buffer into virtual memory 3");
    }
    
    pthread_mutexattr_t att;
    pthread_mutexattr_init(&att);
    pthread_mutexattr_setpshared(&att, PTHREAD_PROCESS_SHARED);
    
    pthread_condattr_t cattr;
    pthread_condattr_init(&cattr);
    pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);
    
    // Initialize synchronization primitives
    if(mutex_init(&q->queue_info->lock, &att) != 0){
        queue_error_errno("Could not initialize mutex");
    }
    if(cond_init(&q->queue_info->readable, &cattr) != 0){
        queue_error_errno("Could not initialize condition variable");
    }
    if(cond_init(&q->queue_info->writeable, &cattr) != 0){
        queue_error_errno("Could not initialize condition variable");
    }
    
    // Initialize remaining members
    q->size = s;
    q->queue_info->size = s;
    q->queue_info->head = 0;
    q->queue_info->tail = 0;
    q->queue_info->head_seq = 0;
    q->queue_info->tail_seq = 0;
    
    pthread_condattr_destroy(&cattr);
    pthread_mutexattr_destroy(&att);
    
}

void queue_open(queue_t *q, char *name) {
    if((q->fd = shm_open(name, O_RDWR, 0777)) == -1){
        queue_error_errno("Could not obtain anonymous file");
    }
    
    if((q->queue_info = mmap(NULL, getpagesize(), PROT_READ | PROT_WRITE, MAP_SHARED, q->fd, 0)) == MAP_FAILED){
        queue_error_errno("Could not map buffer into virtual memory 1");
    }
    
    printf("got size of buffer = %d\n",(int)q->queue_info->size);
    q->size = q->queue_info->size;
    
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

}

void queue_close(queue_t *q) {
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

void queue_destroy(queue_t *q) {
    if(munmap(q->buffer + q->size, q->size) != 0){
        queue_error_errno("Could not unmap buffer");
    }
    
    if(munmap(q->buffer, q->size) != 0){
        queue_error_errno("Could not unmap buffer");
    }
    
    if(mutex_destroy(&q->queue_info->lock) != 0){
        queue_error_errno("Could not destroy mutex");
    }
    
    if(cond_destroy(&q->queue_info->readable) != 0){
        queue_error_errno("Could not destroy condition variable");
    }
    
    if(cond_destroy(&q->queue_info->writeable) != 0){
        queue_error_errno("Could not destroy condition variable");
    }
    
    if(munmap(q->queue_info, getpagesize()) != 0)
        queue_error_errno("Could not unmap shared_info");
    
    if(shm_unlink("george"))
        printf("Unable to unlink /dev/shm%s\n","george");
    
    if(close(q->fd) != 0){
        queue_error_errno("Could not close anonymous file");
    }

}

void queue_put(queue_t *q, uint8_t *buffer, size_t size) {
    mutex_lock(&q->queue_info->lock);
    
    // Wait for space to become available
    while(q->queue_info->size - (q->queue_info->tail - q->queue_info->head) < size + sizeof(message_t)) {
        cond_wait(&q->queue_info->writeable, &q->queue_info->lock);
    }
    
    // Construct header
    message_t m;
    m.len = size;
    m.seq = q->queue_info->tail_seq++;
    
    // Write message
    memcpy(&q->buffer[q->queue_info->tail                    ], &m,     sizeof(message_t));
    memcpy(&q->buffer[q->queue_info->tail + sizeof(message_t)], buffer, size             );
    
    // Increment write index
    q->queue_info->tail  += size + sizeof(message_t);
    
    cond_signal(&q->queue_info->readable);
    mutex_unlock(&q->queue_info->lock);
}

size_t queue_get(queue_t *q, uint8_t *buffer, size_t max) {
    mutex_lock(&q->queue_info->lock);
    
    // Wait for a message that we can successfully consume to reach the front of the queue
    message_t m;
    for(;;) {
    
        // Wait for a message to arrive
        while((q->queue_info->tail - q->queue_info->head) == 0){
            cond_wait(&q->queue_info->readable, &q->queue_info->lock);
        }
        
        // Read message header
        memcpy(&m, &q->buffer[q->queue_info->head], sizeof(message_t));
        
        // Message too long, wait for someone else to consume it
        if(m.len > max){
            while(q->queue_info->head_seq == m.seq) {
                cond_wait(&q->queue_info->writeable, &q->queue_info->lock);
            }
            continue;
        }
        
        // We successfully consumed the header of a suitable message, so proceed
        break;
    } 
    
    // Read message body
    memcpy(buffer, &q->buffer[q->queue_info->head + sizeof(message_t)], m.len);
    
    // Consume the message by incrementing the read pointer
    q->queue_info->head += m.len + sizeof(message_t);
    q->queue_info->head_seq++;
    
    // When read buffer moves into 2nd memory region, we can reset to the 1st region
    if(q->queue_info->head >= q->queue_info->size) {
        q->queue_info->head -= q->queue_info->size;
        q->queue_info->tail -= q->queue_info->size;
    }
    
    cond_signal(&q->queue_info->writeable);
    mutex_unlock(&q->queue_info->lock);
    
    return m.len;
}
