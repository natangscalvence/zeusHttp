#include "../../include/http/huffman_table.h"

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>>

/**
 * Structure node for search tree
 */

typedef struct huff_node {
    struct huff_node *left;         /** bit 0 */
    struct huff_node *right;        /** bit 1 */
    int16_t sym;
} huff_node_t;

static huff_node_t *huff_tree_root = NULL;

/**
 * Experimental huffman decoding...
 * This is not the proper way to do that, i think, but it works by now....
 */

int zeus_hpack_huffman_decode(const uint8_t *src, size_t src_len, char *dst, size_t dst_max) {
    uint32_t state = 0;
    size_t dst_len = 0;

    for (size_t i = 0; i < src_len; i++) {
        uint8_t byte = src[i];

        uint8_t high = byte >> 4;
        state = huffman_table[state][high];
        if (state & HUFF_SYM_FOUND) {
            if (dst_len < dst_max - 1) {
                dst[dst_len++] = (char)(state >> 8);
                state = 8;
            }
        }

        uint8_t low = byte & 0x0F;
        state = huffman_table[state][low];
        if (state & HUFF_SYM_FOUND) {
            if (dst_len < dst_max - 1) {
                dst[dst_len++] = (char)(state >> 8);
            }
            state = 0;
        }
    }
    dst[dst_len] = '\0';
    return (int)dst_len;
}

uint32_t zeus_hpack_decode_int(const uint8_t *payload, size_t len, size_t *pos, uint8_t prefix_mask) {
    uint8_t prefix_limit = (1 << prefix_mask) - 1;
    uint32_t value = payload[*pos] & prefix_limit;
    (*pos)++;

    if (value < prefix_limit) {
        return value;
    }

    uint32_t shift = 0;
    while (*pos < len) {
        uint8_t byte = payload[(*pos)++];
        value += (byte & 0x7F) << shift;

        if (!(byte & 0x80)) {
            break;
        }
        shift += 7;
    }

    return value;
}