#ifndef queue_internal_h_
#define queue_internal_h_

#include "queue.h"

/** Metadata (header) for a message in the queue
 */
typedef struct __message_t__ {
    size_t  len;
    size_t  seq;
    size_t  miss;
    size_t  padd;
} message_t;

#endif
