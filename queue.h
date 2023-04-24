#ifndef queue_h_
#define queue_h_

#include <stdint.h>
// #include <pthread.h>
#include "mutex.h"

typedef struct __shared_info__ {
  
  // size
  // size_t entry_size;
  size_t full_entry_size;
  size_t entries;
  size_t entry_mask;
  size_t size;
  
  // read / write indices
  size_t head;
  size_t tail;
  
  // sequence number of next consumable message
  size_t head_seq;
  
  // sequence number of last written message
  size_t tail_seq;
  
  size_t last_tail_seq;
  size_t last_tail;
  
  // synchronization primitives
  cv_t read_cond;
  cv_t write_cond;
  mutex_t read_mutex;
  mutex_t write_mutex;
  
  size_t readers;
} shared_t;

/** Blocking queue data structure
 */
typedef struct __queue_t__ {
    // backing buffer and size
    char name[16];
    uint8_t *buffer;
    size_t size;
    size_t full_entry_size;
    size_t entries;
    
    // backing buffer's memfd descriptor
    int fd;
    
    shared_t *queue_info;
    
} queue_t;

/** Initialize a blocking queue *q* of size *s*
 */
void queue_init(queue_t *q, char *name, size_t entry_size, size_t entries);

int queue_open(queue_t *q, char *name);

void queue_close(queue_t *q);

/** Destroy the blocking queue *q*
 */
size_t queue_destroy(queue_t *q);

/** Insert into queue *q* a message of *size* bytes from *buffer*
  *
  * Blocks until sufficient space is available in the queue.
  */
void queue_pop(queue_t *q, uint8_t **buffer);
void queue_push(queue_t *q, size_t size);

void queue_put(queue_t *q, uint8_t *buffer, size_t size);

/** Retrieves a message of at most *max* bytes from queue *q* and writes
  * it to *buffer*.
  *
  * Blocks until a message of no more than *max* bytes is available.
  *
  * Returns the number of bytes in the written message.
  */
size_t queue_get_last(queue_t *q, uint8_t **buffer);

/*
get_next returns the most recent telemetry buffer pointer,
it will wait a short time for a new one to be available
it will also reset the telem tail for the get_last functions
*/
size_t queue_get(queue_t *q, uint8_t **buffer);

/*
get_last returns the telemetry pointer that hasn't been read yet
if this function is called in a loop
it will keep reading from the circular buffer until it has caught up
*/
size_t queue_get(queue_t *q, uint8_t **buffer);

/*
copy_next_entries will reset the telem counter and wait for a number
of entries to arrive before copying them to the supplied buffer
the number of entries cannot be more than half of the circular buffer entries size
*/
size_t queue_copy_next_entries(queue_t *q, uint8_t *buffer, size_t entries);

/*
copy_last_entries will get a number of entries from the last read entry
it will wait if there are not enough entries to copy.
the number of entries cannot be more than half of the circular buffer entries size
*/
size_t queue_copy_last_entries(queue_t *q, uint8_t *buffer, size_t entries);


#endif
