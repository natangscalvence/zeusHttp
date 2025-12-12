#ifndef ZEUS_CONFIG_H
#define ZEUS_CONFIG_H

#include <stddef.h>

#define DEFAULT_PORT 8443
#define DEFAULT_WORKERS 4

/**
 * Structure that contains the global configuration for server.
 */

typedef struct {
    char bind_host[32];
    int bind_port;
    int num_workers;

    char log_file[128];
    char tls_cert_path[128];
    char tls_key_path[128];
} zeus_config_t;

typedef enum {
    CONFIG_KEY_UNKNOWN,
    CONFIG_KEY_BIND_HOST,
    CONFIG_KEY_BIND_PORT,
    CONFIG_KEY_NUM_WORKERS,
    CONFIG_KEY_TLS_CERT_PATH,
    CONFIG_KEY_TLS_KEY_PATH,
    CONFIG_KEY_LOG_FILE,
} config_key_t;

/**
 * Load the settings from the default file.
 */

int zeus_config_load(zeus_config_t *config, const char *config_path);

/**
 * Initiate the configuration with default value.
 */

void zeus_config_init_default(zeus_config_t *config);

#endif // ZEUS_CONFIG_H
