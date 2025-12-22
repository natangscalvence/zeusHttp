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
    size_t dst_len = 0;
    uint32_t bit_buffer = 0;
    int bit_count = 0;

    for (size_t i = 0; i < src_len; i++) {
        bit_buffer = (bit_buffer << 8) | src[i];
        bit_count += 8;
    }
    dst[dst_len] = '\0';
    return (int)dst_len;
}