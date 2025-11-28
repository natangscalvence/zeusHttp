/**
 * include/http/response.h
 * Function declarations for the HTTP response subsystem.
 */
#ifndef ZEUS_RESPONSE_H
#define ZEUS_RESPONSE_H

#include "../zeushttp.h"
#include "../core/conn.h"

/**
 * Maximum HTTP response buffer size.
 */

#define MAX_RESPONSE_BUFFER 4096

/**
 * Adds the HTTP header to the response buffer.
 */

int zeus_response_add_header(zeus_response_t *res, const char *key, const char *val);

/**
 * Sends the complete response (including headers and the body).
 */

 int zeus_response_send_data(zeus_response_t *res, const char *data, size_t len);

 #endif // ZEUS_RESPONSE_H