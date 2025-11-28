#ifndef ZEUS_CONN_H
#define ZEUS_CONN_H

#include "../zeushttp.h"
#include "../http/http.h" 
#include "io_event.h"

#include <stddef.h>


#define MAX_RESPONSE_BUFFER 4096

typedef struct zeus_server zeus_server_t;
typedef struct zeus_io_event zeus_io_event_t; 

/**
 * Represents a single HTTP connection (socket)
 */

typedef struct zeus_conn {
    zeus_io_event_t event;          /** The I/O event wrapper for this connection. */
    zeus_server_t *server;          /** Back-reference to the server instance. */
    
    int parser_state;               /** Current state of the HTTP State Machine. */
    size_t header_len_count;
    size_t headers_count;
    char read_buffer[4096];         /** Fixed-size read buffer. */
    size_t buffer_used;
    char *parse_cursor;             /** Current position in read_buffer for parsing. */
    
    char response_buffer[MAX_RESPONSE_BUFFER];
    size_t response_len;


    zeus_request_t req;
    zeus_response_t res;
} zeus_conn_t;


#endif // ZEUS_CONN_H
