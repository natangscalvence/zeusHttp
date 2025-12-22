#include "../../include/zeushttp.h"

#include <stdint.h>
#include <stdio.h>

typedef struct {
    const char *name;
    const char *value;
} hpack_pair_t;

/**
 * Static table (Excerpt from the first indexes of RFC 7541)
 */

static hpack_pair_t static_table[] = {
    {NULL, NULL},                  /** Index 0 does not exist. */
    {":authority", ""},           /** Index 1 */
    {":method", "GET"},          /** Index 2 */
    {":method", "POST"},         /** Index 3 */
    {":path", "/"},             /** Index 4 */
    {":path", "/index.html"},   /** Index 5 */
    {":scheme", "http"},        /** Index 6 */
    {":scheme", "https"},       /** Index 7 */
    {":status", "200"},         /** Index 8 */
};



/**
 * Decodes a block of HPACK headers (simplified).
 */

void zeus_hpack_decode(const uint8_t *payload, size_t len, zeus_request_t *req) {
    size_t pos = 0;

    while (pos < len) {
        uint8_t byte = payload[pos];

        if (byte & 0x80) {
            uint8_t index = byte & 0x7F;
            if (index > 0 && index <= 8) {
                hpack_pair_t pair = static_table[index];

                if (strcmp(pair.name, ":method") == 0) {
                    req->method == (char*)pair.value;
                } else if (strcmp(pair.name, ":path") == 0) {
                    req->path = (char*)pair.value;
                }

                printf("HPACK: Decoded indexed %s: %s\n", pair.name, pair.value);
            }
            pos += 1;
        } else if (byte & 0x40) {
            /**
             * Literal header field with incremental indexing.
             */
            printf("HPACK: Literal header detection (implementing next...)\n");
            pos = len;
        } else {
            pos++;      /** Fallback. */
        }
    }
}

