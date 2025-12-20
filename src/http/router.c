/**
 * router.c
 * Implements the routing table and request dispatch logic.
 */

#include "../../include/zeushttp.h"
#include "../../include/http/http.h"
#include "../../include/http/router.h"
#include "../../include/core/conn.h"
#include "../../include/core/log.h"
#include "../../include/core/io_event.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int zeus_response_send_data(zeus_response_t *res, const char *data, size_t len);

/**
 * Global list of routes. 
 */

static size_t ROUTE_COUNT = 0;

/**
 * Defines a route (path + handler).
 */
#define MAX_ROUTES 64
static zeus_route_t ROUTE_TABLE[MAX_ROUTES];

static void not_found_handler(zeus_conn_t *conn, zeus_request_t *req) {
    const char *response_404 = 
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Length: 10\r\n"
        "Connection: close\r\n"
        "\r\n"
        "Not Found\n";

    zeus_response_send_data(&conn->res, response_404, strlen(response_404));
}

/**
 * Adds a user handler to the routing table.
 */

int router_add_handler(zeus_server_t *server, const char *path, zeus_handler_cb handler) {
    if (ROUTE_COUNT >= MAX_ROUTES) {
        fprintf(stderr, "Error: Max routes reached.\n");
        return -1;
    }

    /**
     * Safe copy from path.
     */
    strncpy(ROUTE_TABLE[ROUTE_COUNT].path, path, sizeof(ROUTE_TABLE[ROUTE_COUNT].path) - 1);
    ROUTE_TABLE[ROUTE_COUNT].path[sizeof(ROUTE_TABLE[ROUTE_COUNT].path) - 1] = '\0';
    ROUTE_TABLE[ROUTE_COUNT].handler = handler;
    ROUTE_COUNT++;

    return 0;
}

/**
 * The main dispatcher: Finds the corresponding route and calls
 * its handler.
 */

void router_dispatch(zeus_conn_t *conn) {
    if (!conn || !conn->req.method || !conn->req.path) {
        not_found_handler(conn, NULL);
        return;
    }

    const char *req_method = conn->req.method;
    const char *req_path = conn->req.path;

    for (size_t i = 0; i < ROUTE_COUNT; i++) {
        zeus_route_t *route = &ROUTE_TABLE[i];

        /**
         * Compare the method and path (exactly correspondence).
         */

        if (strcmp(req_method, route->method) ==  0 && 
            strcmp(req_path, route->path) == 0) {
            
            ZLOG_INFO("Router: Matched route %s %s. Dispatching handler.",
                route->method, route->path);

            route->handler(conn, &conn->req);
            return;
        }
    }

    ZLOG_INFO("Router: No handler found for %s %s.", req_method, req_path);
    not_found_handler(conn, &conn->req);
}

int register_route(const char *method, const char *path, zeus_handler_cb handler) {
    if (!method || !path || !handler) {
        ZLOG_FATAL("Router: Invalid route registration (method=%p, path=%p, handler=%p)", 
            method, path, handler);

        return -1;
    }

    if (ROUTE_COUNT >= MAX_ROUTES) {
        ZLOG_FATAL("Router: Cannot register route '%s %s'. Route limit (%d) reached.",
            method, path, MAX_ROUTES);
    
        return -1;
    }

    zeus_route_t *new_route = &ROUTE_TABLE[ROUTE_COUNT];
    memset(new_route, 0, sizeof(*new_route));

    strncpy(new_route->method, method, sizeof(new_route->method) - 1);
    strncpy(new_route->path, path, sizeof(new_route->path) - 1);
    new_route->handler = handler;

    ROUTE_COUNT++;
    ZLOG_INFO("Router: Registered route %s %s.", method, path);
    return 0;
}