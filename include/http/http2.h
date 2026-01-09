#ifndef ZEUS_HTTP2_H
#define ZEUS_HTTP2_H

#include <stdint.h>
#include <stddef.h>

#define H2_PREFACE "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
#define H2_PREFACE_LEN 24
#define H2_HDR_SIZE 9

typedef struct {
    char *name;
    char *value;
    size_t size;
} zeus_hpack_entry_t;

typedef struct {
    zeus_hpack_entry_t *entries;
    size_t count;
    size_t current_size;
    size_t max_size;
} zeus_hpack_table_t;

typedef enum {
    H2_FRAME_DATA          = 0x0,
    H2_FRAME_HEADERS       = 0x1,
    H2_FRAME_PRIORITY      = 0x2,
    H2_FRAME_RST_STREAM    = 0x3,
    H2_FRAME_SETTINGS      = 0x4,
    H2_FRAME_PUSH_PROMISE  = 0x5,
    H2_FRAME_PING          = 0x6,
    H2_FRAME_GOAWAY        = 0x7,
    H2_FRAME_WINDOW_UPDATE = 0x8,
    H2_FRAME_CONTINUATION  = 0x9
} zeus_h2_frame_type;

typedef struct {
    uint32_t length;
    uint8_t type;
    uint8_t flags;
    uint32_t stream_id;
} zeus_h2_frame_t;


#endif // ZEUS_HTTP2_H