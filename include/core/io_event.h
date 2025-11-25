/**
 * inclue/core/io_event.h
 * Defines the internal structure for I/O event registration (epoll/kqueue abstraction).
 */

#ifndef ZEUS_IO_EVENT_H
#define ZEUS_IO_EVENT_H

#include <stdint.h>

typedef struct zeus_conn zeus_conn_t;

/**
 * Represents a single I/O event registered in the loop.
 */
typedef struct zeus_io_event {
    int fd;
    void *data;     /** Pointer to the associated zues_conn_t or server struct. */
    void (*read_cb)(struct zeus_io_event *ev);
    void (*write_cb)(struct zeus_io_event *ev);
} zeus_io_event_t;

#endif  // ZEUS_IO_EVENT_H