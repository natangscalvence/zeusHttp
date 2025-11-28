/**
 * file.c
 * Implements zero-copy file serving (sendfile) and file metadata caching.
 */

#include "../../include/zeushttp.h"
#include "../../include/core/conn.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/sendfile.h>

extern void close_connection(zeus_conn_t *conn);

/**
 * Sends a static file using zero-copy (sendfile).
 */
int zeus_response_send_file(zeus_response_t *res, const char *filepath) {
    zeus_conn_t *conn = (zeus_conn_t*)((char*)res - offsetof(zeus_conn_t, res));
    int total_bytes_sent = 0;
    ssize_t headers_sent = 0;

    int file_fd = open(filepath, O_RDONLY);
    if (file_fd < 0) {
        fprintf(stderr, "File not found: %s\n", filepath);
        zeus_response_set_status(res, 404);
        zeus_response_add_header(res, "Content-Type", "text/plain");
        zeus_response_send_data(res, "404 Not Found", 13);
        return -1;
    }

    struct stat stat_buf;
    if (fstat(file_fd, &stat_buf) < 0) {
        close(file_fd);
        close_connection(conn);
        return -1;
    }
    off_t file_size = stat_buf.st_size;

    /**
     * Generate HTTP headers.
     */
    zeus_response_set_status(res, 200);
    zeus_response_add_header(res, "Content-Type", "application/octet-stream");

    /**
     * Attach Content-Lenght and close headers in response buffer.
     */
    size_t required_len = snprintf(
        conn->response_buffer + conn->response_len,
        MAX_RESPONSE_BUFFER - conn->response_len,
        "Content-Lenght: %ld\r\n\r\n", (long)file_size);
    conn->response_len += required_len;

    char status_line_buf[128];
    const char *status_msg = "OK";
    int status_line_len = snprintf(status_line_buf, 128,
        "HTTP/1.1 %u %s\r\n", res->status_code, status_msg);

    memmove(conn->response_buffer + status_line_len,
        conn->response_buffer, conn->response_len);
    memcpy(conn->response_buffer, status_line_buf, (size_t)status_line_len);
    conn->response_len += (size_t)status_line_len;

    headers_sent = write(conn->event.fd, conn->response_buffer, conn->response_len);
    if (headers_sent <= 0) {
        close(file_fd);
        close_connection(conn);
        return -1;
    }
    total_bytes_sent += (int)headers_sent;

    off_t offset = 0;
    ssize_t bytes_transferred = sendfile(
        conn->event.fd,
        file_fd,
        &offset,
        file_size
    );

   if (bytes_transferred < 0 || bytes_transferred != file_size) {
        fprintf(stderr, "Error during sendfile: %ld/%ld bytes sent (Error: %d)\n", (long)bytes_transferred, (long)file_size, errno);
        total_bytes_sent = -1;
    } else {
        total_bytes_sent += (int)bytes_transferred;
    }

    close(file_fd);
    close_connection(conn);

    return total_bytes_sent;
}
