/**
 * zeushttp.h
 * Defines public data structures and handler functions.
 */

#ifndef ZEUSHTTP_H
#define ZEUSHTTP_H

#include <stddef.h>
#include <stdint.h>

#include "../include/config/config.h"

/**
 * Main server structure (contains the event loop and listening socket).
 */

typedef struct zeus_server zeus_server_t;

/**
 * Structure representing an incoming HTTP request.
 */

typedef struct zeus_request {
    const char *method;     /** HTTP method (e.g., "GET"). */
    const char *path;       /** Decoded request URI path. */
} zeus_request_t;

/**
 * Structure representing the outgoing HTTP response.
 */

typedef struct zeus_response {
    uint16_t status_code;   /** HTTP status code (e.g., 200). */
    /** 
     * Internal pointer to connection for writing data.
     */
} zeus_response_t;


/**
 * User-defined callback for handling HTTP requests.
 */

typedef void (*zeus_handler_cb)(zeus_request_t *req, zeus_response_t *res);


/**
 * Initializes the ZeusHTTP server and allocates resources.
 */

zeus_server_t* zeus_server_init(zeus_config_t *config);

/**
 * Registers a handler function for a specific URI path.
 */

int zeus_server_add_handler(zeus_server_t *server, const char *path, zeus_handler_cb handler);

/**
 * Starts the main blocking event loop.
 */

int zeus_server_run(zeus_server_t *server);

/**
 * Sets the HTTP status code for the response.
 */

void zeus_response_set_status(zeus_response_t *res, uint16_t status_code);

/**
 * Adds an HTTP header to the response buffer.
 */

int zeus_response_add_header(zeus_response_t *res, const char *key, const char *value);

/**
 * Sends the response body data and finalizes the response.
 */

int zeus_response_send_data(zeus_response_t *res, const char *data, size_t len);

/**
 * Sends a static file using zero-copy (sendfile/io_uring).
 */

int zeus_response_send_file(zeus_response_t *res, const char *filepath);

/**
 * Initialize the SSL context for the entire zeusHttp core.
 */

int tls_context_init(zeus_server_t *server, const char *cert_file, const char *key_file);

#endif // ZEUSHTTP_H