/**
 * response.c
 * Implements HTTP response header generation and socket writing.
 */

#include "../../include/zeushttp.h"
#include "../../include/core/conn.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <stddef.h> /* For offsetof */

extern void close_connection(zeus_conn_t *conn);

/**
 * Finds the connection structure from the response pointer using
 * offsetof.
 */

static zeus_conn_t* get_conn_from_res(zeus_response_t *res) {
    return (zeus_conn_t*)((char*)res - offsetof(zeus_conn_t, res));
}

/**
 * Returns the standard status message for a given code.
 */

static const char* get_status_message(uint16_t code) {
    switch (code) {
        case 200:
            return "OK";
        case 404:
            return "Not Found";
        case 431:
            return "Request Header Fields Too Large";
        case 500:
            return "Internal Server Error";
        default:
            return "Unknown";
    }
}

/**
 * Sets the HTTP status code for the response.
 */

void zeus_response_set_status(zeus_response_t *res, uint16_t status_code) {
    res->status_code = status_code;
}

/**
 * Adds an HTTP header to the response buffer.
 */

int zeus_response_add_header(zeus_response_t *res, const char *key, const char *value) {
    zeus_conn_t *conn = get_conn_from_res(res);
    int remaining_space = MAX_RESPONSE_BUFFER - conn->response_len;
    int written;

    written = snprintf(conn->response_buffer + conn->response_len, remaining_space,
        "%s: %s\r\n", key, value);

    if (written < 0 || written >= remaining_space) {
        fprintf(stderr, "Error: Response header buffer overflow.\n");
        return -1;
    }
    conn->response_len += written;
    return 0;
}

/**
 * Sends the response headers and data, then close the connection
 * (non-keep-alive).
 */

int zeus_response_send_data(zeus_response_t *res, const char *data, size_t len) {
    zeus_conn_t *conn = get_conn_from_res(res);
    int total_bytes_sent = 0;

    /**
     * Append Content-Lenght header and final empty line of buffer.
     */
    size_t required_len = snprintf(
        conn->response_buffer + conn->response_len,
        MAX_RESPONSE_BUFFER - conn->response_len,
        "Content-Length: %zu\r\n\r\n", len
    );
    conn->response_len += required_len;

    /**
     * Format status line (HTTP/1.1 200 OK\r\n)
     */
    int status_line_len;
    char status_line_buf[128];
    const char *status_msg = get_status_message(res->status_code);
    
    status_line_len = snprintf(status_line_buf, 128,
        "HTTP/1.1 %u %s\r\n", res->status_code, status_msg);

    /**
     * Shift existing headers to make space for the status line
     */
    memmove(conn->response_buffer + status_line_len,
        conn->response_buffer, conn->response_len);

    /**
     * Copy the status line into the start of the buffer.
     */
    memcpy(conn->response_buffer, status_line_buf, (size_t)status_line_len);
    conn->response_len += (size_t)status_line_len;

    /**
     * Send headers...
     */
    printf("DEBUG: Attempting to send %zu bytes of headers/data.\n", conn->response_len);
    ssize_t sent = write(conn->event.fd, conn->response_buffer, conn->response_len);
    if (sent > 0) {
        total_bytes_sent += (int)sent;
    }

    /**
     * Send the body.
     */
    if (len > 0) {
        sent = write(conn->event.fd, data, len);
        if (sent > 0) {
            total_bytes_sent += (int)sent;
        }
    }

    close_connection(conn);
    return total_bytes_sent;
}