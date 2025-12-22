#include "../../include/http/http2.h"
#include "../../include/http/avl.h"
#include "../../include/core/conn.h"
#include "../../include/core/log.h"
#include <string.h>
#include <arpa/inet.h>

static uint32_t read_u24(const uint8_t *b) {
    return (b[0] << 16) | (b[1] << 8) | b[2];
}

static uint32_t read_u32(const uint8_t *b) {
    return ntohl(*(uint32_t *)b) & 0x7FFFFFFF;
}

void zeus_h2_send_initial_settings(zeus_conn_t *conn) {
    uint8_t f[9] = {
        0,0,0,
        0x04, 
        0x00,
        0,0,0,0
    };
    zeus_conn_send(conn, f, 9);
    ZLOG_INFO("H2: Sent initial SETTINGS");
}

void zeus_h2_send_response_simple(zeus_conn_t *conn, uint32_t stream_id) {

    /** :status: 200 */

    uint8_t hpack[] = { 0x88 };

    uint8_t headers[9 + sizeof(hpack)];
    headers[0] = 0;
    headers[1] = 0;
    headers[2] = sizeof(hpack);
    headers[3] = 0x01; 
    headers[4] = 0x04; 

    uint32_t sid = htonl(stream_id & 0x7FFFFFFF);
    memcpy(headers + 5, &sid, 4);
    memcpy(headers + 9, hpack, sizeof(hpack));

    zeus_conn_send(conn, headers, sizeof(headers));

    const char *body = "Server running...";
    size_t blen = strlen(body);

    uint8_t data[9];
    data[0] = (blen >> 16) & 0xFF;
    data[1] = (blen >> 8) & 0xFF;
    data[2] = blen & 0xFF;
    data[3] = 0x00; 
    data[4] = 0x01; 
    memcpy(data + 5, &sid, 4);

    zeus_conn_send(conn, data, 9);
    zeus_conn_send(conn, body, blen);

    ZLOG_INFO("H2: Responded on stream %u", stream_id);
}

int zeus_h2_handler(zeus_conn_t *conn) {

    uint8_t *buf = (uint8_t *)conn->read_buffer;
    size_t  *len = &conn->buffer_used;


    /** 
     * Preface
     */

    if (!conn->handshake_done) {
        if (*len < H2_PREFACE_LEN)
            return 0;

        if (memcmp(buf, H2_PREFACE, H2_PREFACE_LEN) != 0) {
            ZLOG_ERROR("H2: Invalid preface");
            return -1;
        }

        memmove(buf, buf + H2_PREFACE_LEN, *len - H2_PREFACE_LEN);
        *len -= H2_PREFACE_LEN;

        conn->handshake_done = 1;
        conn->h2_ready = 0;

        zeus_h2_send_initial_settings(conn);
        return 0;
    }

    while (1) {

        if (*len < 9)
            break;

        uint32_t flen = read_u24(buf);
        uint8_t  type = buf[3];
        uint8_t  flags = buf[4];
        uint32_t sid = read_u32(buf + 5);

        if (*len < 9 + flen)
            break; /** Wait for more data */

        uint8_t *payload = buf + 9;

        switch (type) {

            case 0x04: /** Settings */
                if (!(flags & 0x01)) {
                    uint8_t ack[9] = {
                        0,0,0,
                        0x04,
                        0x01,
                        0,0,0,0
                    };
                    zeus_conn_send(conn, ack, 9);
                    conn->h2_ready = 1;
                    ZLOG_INFO("H2: SETTINGS ACK sent");
                }
                break;

            case 0x01: /** Headers */
                if (!conn->h2_ready) {
                    ZLOG_WARN("H2: HEADERS before SETTINGS ACK ignored");
                    break;
                }

                /** 
                 * Fully ignores hpack client.
                 */

                if (flags & 0x01) { /** End stream */
                    zeus_h2_send_response_simple(conn, sid);
                }
                break;

            case 0x06: /** Ping */
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

        memmove(buf, buf + 9 + flen, *len - (9 + flen));
        *len -= (9 + flen);
    }

    return 0;
}
