/**
 * router.c
 * Implements the routing table and request dispatch logic.
 */

#include "../../include/zeushttp.h"
#include "../../include/http/http.h"
#include "../../include/core/conn.h"
#include "../../include/core/io_event.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Defines a route (path + handler).
 */

typedef struct zeus_route {
    char path[256];
    zeus_handler_cb handler;
} zeus_route_t;

#define MAX_ROUTES 32
static zeus_route_t ROUTE_TABLE[MAX_ROUTES];
static int ROUTE_COUNT = 0;

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
 * Finds the matching handler and executes it.
 */
void router_dispatch(zeus_conn_t *conn) {
    zeus_request_t *req = &conn->req;
    zeus_response_t *res = &conn->res;

    for (int i = 0; i < ROUTE_COUNT; i++) {
        if (strcmp(ROUTE_TABLE[i].path, req->path) == 0) {
            printf("Router: Matched path '%s'. Executing handler.\n", req->path);

            /**
             * Execute user handler.
             */
            ROUTE_TABLE[i].handler(req, res);
            return;
        }
    }
    printf("Router: Path '%s' not found (404).\n", req->path);
    zeus_response_set_status(res, 404);
    zeus_response_add_header(res, "Content-Type", "text/plain");
    zeus_response_send_data(res, "404 Not Found", 13);
}