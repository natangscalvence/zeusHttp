#ifndef ZEUS_HTTP_INTERNAL_H
#define ZEUS_HTTP_INTERNAL_H

#include "../zeushttp.h"
#include <stddef.h>

/**
 * HTTP parser states.
 */

 enum {
    PS_START_LINE,          /** Reading Method, Path, Version */
    PS_HEADERS,             /** Reading Headers line by line */
    PS_HEADERS_FINISHED,    /** Header section ended (empty line) */
    PS_BODY_IDENTITY,       /** Reading body (Content-Lenght is known) */
    PS_BODY_CHUNKED,        /** Request body (Chunked Encoding) */
    PS_COMPLETED,           /** Request fully parsed and ready to handle */
    PS_ERROR                /** Parsing error or security limit reached. */
};

/**
 * Security limits (adjustable).
 */
#define MAX_HEADERS_LEN 8192        /** 8 KB total for all headers. */
#define MAX_HEADERS 100             /** 100 headers maximum */

typedef struct zeus_conn zeus_conn_t;

/**
 * The core HTTP State Machine. Parses the request incrementally.
 */

void http_parser_run(zeus_conn_t *conn);

/**
 * Dispatches the request to the correct user handler based on the path.
 */

void router_dispatch(zeus_conn_t *conn);

/**
 * Adds a user handler to the routing table.
 */

int router_add_handler(zeus_server_t *server, const char *path, zeus_handler_cb handler);

#endif // ZEUS_HTTP_INTERNAL_H