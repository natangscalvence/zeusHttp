#ifndef ZEUS_HUFFMAN_TABLE_H
#define ZEUS_HUFFMAN_TABLE_H

#include <stdint.h>

typedef struct {
    uint32_t code;
    uint8_t len;
} huff_code_t;

static const huff_code_t huff_table[256] = {
    [48] = {0x00, 5},    // '0' -> 00000
    [47] = {0x18, 5},    // '/' -> 11000
    [103] = {0x08, 5},   // 'g' -> 01000
    [115] = {0x1d, 6},   // 's' -> 11101
    [116] = {0x16, 5},   // 't' -> 10110
    [97] = {0x03, 5},    // 'a' -> 00011
};

#endif 