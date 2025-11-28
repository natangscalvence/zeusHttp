#include "../include/zeushttp.h"
#include <stdio.h>

/**
 * Handler for static files.
 */

void file_handler(zeus_request_t *req, zeus_response_t *res) {
    zeus_response_send_file(res, "test.txt");
}

/** 
 * Simple test handler for the root path. 
 */

void home_handler(zeus_request_t *req, zeus_response_t *res) {
    zeus_response_set_status(res, 200);
    zeus_response_add_header(res, "Content-Type", "text/plain");
    zeus_response_send_data(res, "Hello, zeusHTTP! The request was successful.", 43);
}

/**
 * Main entry point to start the server. 
 */

int main() {
    zeus_server_t *server = zeus_server_init("127.0.0.1", 8080);
    if (!server) {
        fprintf(stderr, "Failed to initialize server.\n");
        return 1;
    }

    zeus_server_add_handler(server, "/", home_handler);
    zeus_server_add_handler(server, "/file", file_handler);

    printf("Starting zeusHTTP...\n");
    if (zeus_server_run(server) != 0) {
        fprintf(stderr, "Server execution error.\n");
        return 1;
    }

    return 0;
}