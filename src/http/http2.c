#include "../../include/http/http2.h"
#include "../../include/http/avl.h"
#include "../../include/core/conn.h"
#include "../../include/core/log.h"

#include <string.h>
#include <arpa/inet.h>

/**
 * Safe reads
 */

static inline uint32_t read_u24(const uint8_t *b) {
    return ((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | (uint32_t)b[2];
}

static inline uint32_t read_u32_sid(const uint8_t *b) {
    return (((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | 
            ((uint32_t)b[2] << 8)  | (uint32_t)b[3]) & 0x7FFFFFFF;
}

/**
 * Just HTTP settings.
 */

static void zeus_h2_parse_settings(zeus_conn_t *conn, const uint8_t *payload, uint32_t len) {
    if (len % 6 != 0) return;

    for (uint32_t i = 0; i < len; i += 6) {
        uint16_t id  = ((uint16_t)payload[i] << 8) | payload[i + 1];
        uint32_t val = ((uint32_t)payload[i+2] << 24) | ((uint32_t)payload[i+3] << 16) | 
                       ((uint32_t)payload[i+4] << 8)  | (uint32_t)payload[i+5];

        switch (id) {
            case 0x03: conn->h2_max_streams = val; break;
            case 0x04: conn->h2_window_size = val; break;
        }
    }
}

/**
 * Outgoing frames
 */

void zeus_h2_send_initial_settings(zeus_conn_t *conn) {
    uint8_t frame[] = {
        0x00, 0x00, 0x0c,       /**  Length: 12 bytes */
        0x04,                   /**  Type: SETTINGS */
        0x00,                   /**  Flags: 0 */
        0x00, 0x00, 0x00, 0x00, /** Stream: 0 */
        
        /** MAX_CONCURRENT_STREAMS (ID: 0x03) = 100 */

        0x00, 0x03, 0x00, 0x00, 0x00, 0x64,
        
        /** INITIAL_WINDOW_SIZE (ID: 0x04) = 65535 */

        0x00, 0x04, 0x00, 0x00, 0xff, 0xff
    };

    zeus_conn_send(conn, frame, sizeof(frame));
    ZLOG_INFO("H2: Sent initial SETTINGS (MAX_STREAMS=100, WINDOW=65535)");
}

void zeus_h2_send_response_simple(zeus_conn_t *conn, uint32_t sid) {
    /**
     * Header frames.
     */
    uint8_t headers_frame[] = {
        0, 0, 1,      /** Payload length */
        0x01,         /**  HEADERS */
        0x04,         /** Flags: END_HEADERS */
        0, 0, 0, 0,   /** SID */
        0x88          /** HPACK: status 200  */
    };
    
    /**
     * Fill up the SID header.
     */

    headers_frame[5] = (sid >> 24) & 0xFF;
    headers_frame[6] = (sid >> 16) & 0xFF;
    headers_frame[7] = (sid >> 8) & 0xFF;
    headers_frame[8] = sid & 0xFF;

    zeus_conn_send(conn, headers_frame, sizeof(headers_frame));

    /**
     * Data frame content.
     */

    const char *msg = "Zeus Server: HTTP/2 Active\n";
    uint32_t msg_len = strlen(msg);
    
    uint8_t data_header[9] = {
        (msg_len >> 16) & 0xFF,
        (msg_len >> 8) & 0xFF,
        msg_len & 0xFF,
        0x00,         
        0x01,         
        headers_frame[5], headers_frame[6], headers_frame[7], headers_frame[8]
    };

    zeus_conn_send(conn, data_header, 9);
    zeus_conn_send(conn, (uint8_t *)msg, msg_len);
    
    ZLOG_INFO("H2: Response sent to stream %u", sid);
}


/**
 * Connection init
 */

void zeus_conn_init_h2(zeus_conn_t *conn) {
    conn->h2_streams = NULL;
    conn->h2_max_streams = 100;
    conn->h2_window_size = 65535;
}

void zeus_h2_send_window_update(zeus_conn_t *conn, uint32_t sid, uint32_t increment) {
    uint8_t frame[13];

    /**
     * 4 Bytes
     */

    frame[0] = 0;
    frame[1] = 0;
    frame[2] = 4;

    /** Type: WINDOW_UPDATE (0x08) */
    
    frame[3] = 0x08;

    /** Flags */

    frame[4] = 0x00;

    /**  Stream ID (31 bits) */

    frame[5] = (sid >> 24) & 0x7F;
    frame[6] = (sid >> 16) & 0xFF;
    frame[7] = (sid >> 8)  & 0xFF;
    frame[8] = sid & 0xFF;

    /** Window Size Increment (31 bits) */

    frame[9]  = (increment >> 24) & 0x7F;
    frame[10] = (increment >> 16) & 0xFF;
    frame[11] = (increment >> 8)  & 0xFF;
    frame[12] = increment & 0xFF;

    zeus_conn_send(conn, frame, sizeof(frame));
}

/**
 * Main HTTP2 handler.
 */

int zeus_h2_handler(zeus_conn_t *conn)
{
    uint8_t *buf = (uint8_t *)conn->read_buffer;
    size_t  *len = &conn->buffer_used;

    if (!conn->h2_preface_done) {

        if (*len < H2_PREFACE_LEN)
            return 0;

        if (memcmp(buf, H2_PREFACE, H2_PREFACE_LEN) != 0) {
            ZLOG_ERROR("H2: Invalid preface (FD %d)", conn->event.fd);
            return -1;
        }

        memmove(buf, buf + H2_PREFACE_LEN, *len - H2_PREFACE_LEN);
        *len -= H2_PREFACE_LEN;

        conn->h2_preface_done = 1;

        zeus_h2_send_initial_settings(conn);
        zeus_h2_send_window_update(conn, 0, 65535);

        ZLOG_INFO("H2: Preface OK (FD %d)", conn->event.fd);
    }

    while (*len >= 9) {

        uint32_t flen = read_u24(buf);
        if (flen > 0x4000)
            return -1;

        if (*len < 9 + flen)
            return 0;

        uint8_t  type  = buf[3];
        uint8_t  flags = buf[4];
        uint32_t sid   = read_u32_sid(buf + 5);
        uint8_t *payload = buf + 9;

        switch (type) {

       /**
        * Settings.
        */

        case 0x04:
            if (flags & 0x01) {
                ZLOG_INFO("H2: SETTINGS ACK (FD %d)", conn->event.fd);
            } else {

                uint8_t ack[9] = {0,0,0, 0x04, 0x01, 0,0,0,0};
                zeus_conn_send(conn, ack, 9);
            }
            break;

        /**
         * Headers.
         */

        case 0x01:
            if (sid == 0)
                return -1;

            conn->h2_header_sid = sid;
            conn->h2_header_len = 0;

            conn->h2_header_block = realloc(conn->h2_header_block, flen);
            memcpy(conn->h2_header_block, payload, flen);
            conn->h2_header_len = flen;

            if (flags & 0x04) { /** END_HEADERS */
                goto decode_headers;
            }
            break;

        /** CONTINUATION Headers */

        case 0x09:
            if (sid != conn->h2_header_sid)
                return -1;

            conn->h2_header_block =
                realloc(conn->h2_header_block,
                        conn->h2_header_len + flen);

            memcpy(conn->h2_header_block + conn->h2_header_len,
                   payload, flen);

            conn->h2_header_len += flen;

            if (flags & 0x04) { /** END_HEADERS */
decode_headers:
                zeus_h2_stream_t *stream =
                    avl_find(conn->h2_streams, sid);

                if (!stream) {
                    conn->h2_streams = avl_insert(conn->h2_streams, sid);
                    stream = avl_find(conn->h2_streams, sid);
                    memset(&stream->req, 0, sizeof(stream->req));
                }

                zeus_hpack_decode(conn,
                                  conn->h2_header_block,
                                  conn->h2_header_len,
                                  &stream->req);

                if (flags & 0x01) { /** END_STREAM */
                    zeus_h2_send_response_simple(conn, sid);
                }
            }
            break;

        /**
         * Ping.
         */

        case 0x06:
            if (flen != 8)
                return -1;

            if (!(flags & 0x01)) {
                uint8_t pong[17];
                memcpy(pong, buf, 17);
                pong[4] = 0x01;
                zeus_conn_send(conn, pong, 17);
            }
            break;

        default:
            break;
        }

        size_t frame_sz = 9 + flen;
        memmove(buf, buf + frame_sz, *len - frame_sz);
        *len -= frame_sz;
    }

    return 0;
}