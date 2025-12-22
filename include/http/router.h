#ifndef ZEUS_ROUTER_H
#define ZEUS_ROUTER_H

#include "../core/conn.h"
#include "../http/http.h"

/**
 * Handler function prototype (callback). Receiveis the connection
 * for I/O and request.
 */

typedef void (*zeus_handler_cb)(zeus_conn_t *conn, zeus_request_t *req);

/**
 * Structure that defines a route in the server.
 */

#define MAX_METHOD_LEN 8
#define MAX_PATH_LEN 256

typedef struct {
    char *path;
    zeus_handler_cb handler;

    struct zeus_route_node *left;
    struct zeus_route_node *right;
    int height;
} zeus_route_node_t;

typedef struct {
    char method[MAX_METHOD_LEN];
    char path[MAX_PATH_LEN];
    zeus_handler_cb handler;
} zeus_route_t;

/**
 * Register a new handler (function) for a method and path specified.
 */

int register_route(const char *method, const char *path, zeus_handler_cb handler);

/**
 * The main dispatcher: Finds a new route and call their handler.
 */

void router_dispatch(zeus_conn_t *conn);

#endif // ZEUS_ROUTER_H