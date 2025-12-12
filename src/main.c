#include "../include/zeushttp.h"
#include "../include/config/config.h"
#include <stdio.h>

extern int tls_context_init(zeus_server_t *server, const char *cert_file, const char *key_file);
extern int zeus_config_load(zeus_config_t *config, const char *config_path);
extern zeus_server_t *zeus_server_init(zeus_config_t *config);
extern int worker_master_start(zeus_server_t *server);


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
    zeus_config_t config;
    const char *config_path = "zeus.conf";

    /** 
     * Load the configuration (default file if exists)... 
     */

    if (zeus_config_load(&config, config_path) < 0) {
        fprintf(stderr, "Fatal: Failed to load configuration.\n");
        return 1;
    }


    zeus_server_t *server = zeus_server_init(&config);
    if (!server) {
        fprintf(stderr, "Failed to initialize server.\n");
        return 1;
    }

    if (tls_context_init(server, "server.pem", "server.key") != 0) {
        fprintf(stderr, "Fatal: TLS initialization failed. Aborting\n");
        return 1;
    }

    printf("Master Process starting Worker Model...\n");
    if (worker_master_start(server) != 0) {
        fprintf(stderr, "Fatal: Worker master failed.\n");
        return 1;
    }
}