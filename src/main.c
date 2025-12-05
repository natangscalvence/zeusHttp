#include "../include/zeushttp.h"
#include <stdio.h>

extern int tls_context_init(zeus_server_t *server, const char *cert_file, const char *key_file);

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
    zeus_server_t *server = zeus_server_init("127.0.0.1", 8443);
    if (!server) {
        fprintf(stderr, "Failed to initialize server.\n");
        return 1;
    }

    if (tls_context_init(server, "server.pem", "server.key") != 0) {
        fprintf(stderr, "Fatal: TLS initialization failed. Aborting\n");
        return 1;
    }

    printf("Master Process starting Worker Model...\n");
    if (worker_master_start(server, 4) != 0) {
        fprintf(stderr, "Fatal: Worker master failed.\n");
        return 1;
    }

    return 0;
}