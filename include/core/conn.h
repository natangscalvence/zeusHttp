#ifndef ZEUS_CONN_H
#define ZEUS_CONN_H

#include "../zeushttp.h"
#include "../http/http.h"
#include "../config/config.h"  
#include "io_event.h"

#include <stddef.h>

#include <openssl/ssl.h>
#include <openssl/err.h>


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
    

    volatile int fd;
    volatile int refcount;
    volatile int closing;
    volatile int ready_to_free;

    char response_buffer[MAX_RESPONSE_BUFFER];
    size_t response_len;
    size_t write_offset;

    SSL *ssl_conn;                
    int handshake_done;             /** 0 = Handshake in progress, 1 = ready for R/W */
    int is_ssl;

    zeus_request_t req;
    zeus_response_t res;

    int sendfile_fd;                /** File descriptor of file to be sended. */
    size_t sendfile_size;           /** Total size of file */
    off_t sendfile_offset;          /** File offset */
    int is_sending_file;            /** Flag to distinguish between buffered and senfile I/O */
} zeus_conn_t;


#endif // ZEUS_CONN_H
