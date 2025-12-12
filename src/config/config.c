#include "../../include/config/config.h"
#include "../../include/core/log.h"

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

#define MAX_LINE_LEN 256

/** 
 * Maps a key string to an enumeration ID for use in the 
 * switch statement.
 * 
 * This is a replacement for hash string. And yes, i know the identation 
 * is different, i maded it like this to improve the readability :)
 */

static config_key_t get_key_id(const char *key) {
    if (strcmp(key, "bind_host") == 0) return CONFIG_KEY_BIND_HOST;
    if (strcmp(key, "bind_port") == 0) return CONFIG_KEY_BIND_PORT;
    if (strcmp(key, "num_workers") == 0) return CONFIG_KEY_NUM_WORKERS;
    if (strcmp(key, "tls_cert_path") == 0) return CONFIG_KEY_TLS_CERT_PATH;
    if (strcmp(key, "tls_key_path") == 0) return CONFIG_KEY_TLS_KEY_PATH;
    if (strcmp(key, "log_file") == 0) return CONFIG_KEY_LOG_FILE;

    return CONFIG_KEY_UNKNOWN;
}

/**
 * Remove white spaces from the beginning to the end of a string
 */

static char *trim_whitesapce(char *str) {
    char *end;

    /**
     * Trim leading space.
     */

    while (isspace((unsigned char)*str)) {
        str++;
    }

    if (*str == 0) {
        return str;
    }

    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        end--;
    }

    *(end + 1) = 0;
    return str;
}

/**
 * Initiate the configuration with default value.
 */

void zeus_config_init_default(zeus_config_t *config) {
    if (!config) {
        return;
    }

    strncpy(config->bind_host, "127.0.0.1", sizeof(config->bind_host));
    config->bind_port = DEFAULT_PORT;
    config->num_workers = DEFAULT_WORKERS;

    strncpy(config->log_file, "stderr", sizeof(config->log_file));
    strncpy(config->tls_cert_path, "certs/server.crt", sizeof(config->tls_cert_path));
    strncpy(config->tls_key_path, "certs/server.key", sizeof(config->tls_key_path));

    ZLOG_INFO("Config: Initialized with default settings.");
}

/**
 * Load the configs from the file.
 */

int zeus_config_load(zeus_config_t *config, const char *config_path) {
    zeus_config_init_default(config);

    if (!config_path) {
        ZLOG_INFO("Config: No configuration file specified. Using defaults.");
        return 0;
    }

    FILE *fp = fopen(config_path, "r");
    if (!fp) {
        ZLOG_PERROR("Config: Failed to open configuration file '%s'. Using defaults.", config_path);
        return 0;
    }

    char line[MAX_LINE_LEN];
    int line_num = 0;

    while (fgets(line, MAX_LINE_LEN, fp) != NULL) {
        line_num++;
        if (line[0] == '\0' || line[0] == '#' || line[0] == '[') {
            continue;
        }

        char *delimiter = strchr(line, '=');
        if (!delimiter) {
            continue;
        }

        *delimiter = '\0';
        char *key = trim_whitesapce(line);
        char *value = trim_whitesapce(delimiter + 1);

        if (strlen(key) == 0 || strlen(value) == 0) {
            continue;
        }

        config_key_t key_id = get_key_id(key);

        switch (key_id) {
            case CONFIG_KEY_BIND_HOST:
                strncpy(config->bind_host, value, sizeof(config->bind_host) - 1);
                break;
            case CONFIG_KEY_BIND_PORT:
                config->bind_port = (uint16_t)atoi(value);
                break;
            case CONFIG_KEY_NUM_WORKERS:
                config->num_workers = atoi(value);
                break;
            case CONFIG_KEY_TLS_CERT_PATH:
                strncpy(config->tls_cert_path, value, sizeof(config->tls_cert_path) - 1);
                break;
            case CONFIG_KEY_TLS_KEY_PATH:
                strncpy(config->tls_key_path, value, sizeof(config->tls_key_path) - 1);
                break;
            case CONFIG_KEY_LOG_FILE:
                strncpy(config->log_file, value, sizeof(config->log_file) - 1);
                break;
            case CONFIG_KEY_UNKNOWN:
            default:
                ZLOG_FATAL("Config: Unknown key '%s' found at line. Ignoring.", key, line_num);
                break;
        }
    }

    fclose(fp);
    ZLOG_INFO("Config: Successfully loaded settings from '%s'.", config_path);
    return 0;
}

