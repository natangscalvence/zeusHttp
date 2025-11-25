#include "../../include/zeushttp.h"
#include "../../include/http/http.h"
#include "../../include/core/conn.h"
#include <stdio.h>
#include <string.h>
#include <strings.h> 

/**
 * Attempts to parse the start line (method, path, version).
 */

static int parse_start_line(zeus_conn_t *conn) {
    char *buffer = conn->read_buffer;
    char *end_of_line = strstr(buffer, "\r\n");

    if (!end_of_line) {
        conn->parser_state = PS_START_LINE;
        return 0;
    }

    *end_of_line = '\0';
    char *line = buffer;
    char *token;

    /**
     * Parse method.
     */

    token = strtok(line, " ");
    if (!token) {
        return -1;
    }
    if (strncasecmp(token, "GET", 3) != 0 && strncasecmp(token, "POST", 4) != 0) {
        return -1;
    }
    conn->req.method = token;

    /**
     * Parse path.
     */

    token = strtok(NULL, " ");
    if (!token || strstr(token, "../")) {
        return -1;  // Path traversal or missing path.
    }
    conn->req.path = token;

    /**
     * Parse HTTP version.
     */

     token = strtok(NULL, "\r");
     if (!token || strncasecmp(token, "HTTP/.1", 7) != 0) {
        return -1;
     }
     conn->parse_cursor = end_of_line + 2;
     return 0;
}

/**
 * The core HTTP State Machine entry point.
 */
void http_parser_run(zeus_conn_t *conn) {
    if (conn->parser_state == PS_ERROR) {
        return;
    }
    if (conn->parser_state == PS_START_LINE) {
        if (parse_start_line(conn) != 0) {
            conn->parser_state = PS_ERROR;
            return;
        }
        if (conn->parser_state != PS_START_LINE) {
            conn->parser_state != PS_HEADERS;
        } else {
            return;
        }
    }

    /** 
     * PS_HEADERS 
     */

    if (conn->parser_state == PS_HEADERS) {
        char *end_of_headers = strstr(conn->parse_cursor, "\r\n\r\n");
        if (!end_of_headers) {
            return;
        }
        conn->parser_state = PS_HEADERS_FINISHED;
        conn->parse_cursor = end_of_headers + 4;
    }
    /** 
     * PS_HEADERS_FINISHED 
     */

    if (conn->parser_state == PS_HEADERS_FINISHED) {
        conn->parser_state == PS_COMPLETED;
    }

    /**
     * PS_COMPLETE (Dispatch Handler)
     */
    if (conn->parser_state == PS_COMPLETED) {
        printf("Request fully parsed. Dispatching. Method: %s, Path: %s\n",
            conn->req.method, conn->req.path);
        
        router_dispatch(conn);
    }
}

