#include "../../include/zeushttp.h"
#include "../../include/http/http.h"
#include "../../include/core/conn.h"
#include "../../include/core/log.h"
#include <stdio.h>
#include <string.h>
#include <strings.h> 

/**
 * Attempts to parse the start line (method, path, version).
 */

static int parse_start_line(zeus_conn_t *conn) {
    char *buffer = conn->read_buffer;
    char *end_of_line = strstr(buffer, "\r\n");
    char *start = buffer;
    char *space_one, *space_two;

    if (!end_of_line) {
        return -1;      /** Need more data or malformed. */
    }

    /**
     * Find the first space (end of METHOD)
     */
    space_one = strchr(start, ' ');
    if (!space_one) {
        return -1;
    }
    *space_one = '\0';  // end of METHOD.

    /**
     * Find the second space (end of PATH).
     */

     space_two = strchr(space_one + 1, ' ');
     if (!space_two) {
        return -1;
     }
     *space_two = '\0';     /** end of PATH */

     /**
      * Verify and assign the METHOD.
      */
     if (strncasecmp(start, "GET", 3) != 0 && strncasecmp(start, "POST", 4) != 0) {
        return -1;
     }
     conn->req.method = start;

     /**
      * Verify and assign PATH.
      */
     conn->req.path = space_one + 1;
     if (strstr(conn->req.path, "../")) {
        return -1;
     }

     /**
      * Verify the version
      */
     char *version = space_two + 1;
     if (strncasecmp(version, "HTTP/1.", 7) != 0) {
        return -1;
     }

     conn->parse_cursor = end_of_line + 3;
     return 1;
}

/**
 * The core HTTP State Machine entry point.
 */
void http_parser_run(zeus_conn_t *conn) {
    if (conn->parser_state == PS_ERROR) {
        return;
    }
    if (conn->parser_state == PS_START_LINE) {

        int result = parse_start_line(conn);

        if (result == 1) {
            conn->parser_state = PS_HEADERS;
        } else if (result == -1) {
            conn->parser_state = PS_ERROR;
            return;
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
        conn->parser_state = PS_COMPLETED;
    }

    /**
     * PS_COMPLETE (Dispatch Handler)
     */
    if (conn->parser_state == PS_COMPLETED) {
        printf("Parser: Dispatching. Method: %s, Path: %s\n",
            conn->req.method, conn->req.path);
        
        router_dispatch(conn);
    }
}

/** 
 * Implements the HTTP parser extracting the start line and headers.
 */

int parse_http_request(zeus_conn_t *conn, zeus_request_t *req) {
    if (!conn || !req || conn->buffer_used == 0) {
        return -1;
    }

    char *buf = conn->read_buffer;
    size_t len = conn->buffer_used;

    char *eol = memmem(buf, len, "\r\n", 2);
    if (!eol) {
        return -1;
    }

    char *sp1 = memchr(buf, ' ', eol - buf);
    if (!sp1) return -1;

    char *sp2 = memchr(sp1 + 1, ' ', eol - (sp1 + 1));
    if (!sp2) return -1;

    *sp1 = *sp2 = *eol = '\0';

    req->method  = buf;
    req->path    = sp1 + 1;
    req->version = sp2 + 1;

    return 0;
}

