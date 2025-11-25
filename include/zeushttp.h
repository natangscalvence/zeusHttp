/**
 * zeushttp.h
 * Public API header for the ZeusHTTP library.
 * Defines public data structures and handler functions.
 */
#ifndef ZEUSHTTP_H
#define ZEUSHTTP_H

#include <stddef.h>
#include <stdint.h>

// --- Public Data Structures ---

/**
 * @brief Main server structure (contains the event loop and listening socket).
 */
typedef struct zeus_server zeus_server_t;

/**
 * @brief Structure representing an incoming HTTP request.
 */
typedef struct zeus_request {
    const char *method;     /**< HTTP method (e.g., "GET"). */
    const char *path;       /**< Decoded request URI path. */
    // Placeholders for headers, body, query params access.
} zeus_request_t;

/**
 * @brief Structure representing the outgoing HTTP response.
 */
typedef struct zeus_response {
    uint16_t status_code;   /**< HTTP status code (e.g., 200). */
    // Internal pointer to connection for writing data.
} zeus_response_t;

// --- Function Types ---

/**
 * @brief User-defined callback for handling HTTP requests.
 */
typedef void (*zeus_handler_cb)(zeus_request_t *req, zeus_response_t *res);

// --- Public API Functions ---

/**
 * @brief Initializes the ZeusHTTP server and allocates resources.
 * @param host IP address to listen on (e.g., "0.0.0.0").
 * @param port Port number to listen on (e.g., 8080).
 * @return Pointer to zeus_server_t on success, or NULL on error.
 */
zeus_server_t* zeus_server_init(const char *host, int port);

/**
 * @brief Registers a handler function for a specific URI path.
 * @param server The initialized server instance.
 * @param path The URI path to map (e.g., "/users").
 * @param handler The user callback function.
 * @return 0 on success, -1 on error.
 */
int zeus_server_add_handler(zeus_server_t *server, const char *path, zeus_handler_cb handler);

/**
 * @brief Starts the main blocking event loop.
 * @param server The server instance.
 * @return 0 on graceful shutdown, -1 on fatal error.
 */
int zeus_server_run(zeus_server_t *server);

/**
 * @brief Sets the HTTP status code for the response.
 */
void zeus_response_set_status(zeus_response_t *res, uint16_t status_code);

/**
 * @brief Adds an HTTP header to the response buffer.
 */
int zeus_response_add_header(zeus_response_t *res, const char *key, const char *value);

/**
 * @brief Sends the response body data and finalizes the response.
 */
int zeus_response_send_data(zeus_response_t *res, const char *data, size_t len);

/**
 * @brief Sends a static file using zero-copy (sendfile/io_uring).
 */
int zeus_response_send_file(zeus_response_t *res, const char *filepath);

#endif // ZEUSHTTP_H