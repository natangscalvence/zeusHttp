#include "../../include/zeushttp.h"
#include "../../include/core/conn.h"
#include "../../include/core/log.h"
#include "../../include/http/http2.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/**
 * HPACK static table (RGC 7451 - Section 2).
 * Indexing starts at 1...
 */

typedef struct {
    const char *name;
    const char *value;
} hpack_pair_t;

static const hpack_pair_t static_table[] = {
    {":authority", ""},
    {":method", "GET"},
    {":method", "POST"},
    {":path", "/"},
    {":path", "/index.html"},
    {":scheme", "http"},
    {":scheme", "https"},
    {":status", "200"},
    {":status", "204"},
    {":status", "206"},
    {":status", "304"},
    {":status", "400"},
    {":status", "404"},
    {":status", "500"},
    {"accept-charset", ""},
    {"accept-encoding", "gzip, deflate"},
    {"accept-language", ""},
    {"accept-ranges", ""},
    {"accept", ""},
    {"access-control-allow-origin", ""},
    {"age", ""},
    {"allow", ""},
    {"authorization", ""},
    {"cache-control", ""},
    {"content-disposition", ""},
    {"content-encoding", ""},
    {"content-language", ""},
    {"content-length", ""},
    {"content-location", ""},
    {"content-range", ""},
    {"content-type", ""},
    {"cookie", ""},
    {"date", ""},
    {"etag", ""},
    {"expect", ""},
    {"expires", ""},
    {"from", ""},
    {"host", ""},
    {"if-match", ""},
    {"if-modified-since", ""},
    {"if-none-match", ""},
    {"if-range", ""},
    {"if-unmodified-since", ""},
    {"last-modified", ""},
    {"link", ""},
    {"location", ""},
    {"max-forwards", ""},
    {"proxy-authenticate", ""},
    {"proxy-authorization", ""},
    {"range", ""},
    {"referer", ""},
    {"refresh", ""},
    {"retry-after", ""},
    {"server", ""},
    {"set-cookie", ""},
    {"strict-transport-security", ""},
    {"transfer-encoding", ""},
    {"user-agent", ""},
    {"vary", ""},
    {"via", ""},
    {"www-authenticate", ""}
};

#define HPACK_STATIC_TABLE_SIZE \
    (sizeof(static_table) / sizeof(static_table[0]))

/**
 * Dynamic table
 */

void zeus_hpack_table_init(zeus_hpack_table_t *table) {
    table->entries = NULL;
    table->count = 0;
    table->current_size = 0;
    table->max_size = 4096;
}

static void hpack_table_evict(zeus_hpack_table_t *table, size_t needed) {
    while (table->count > 0 &&
           table->current_size + needed > table->max_size) {

        size_t idx = table->count - 1;
        table->current_size -= table->entries[idx].size;
        free(table->entries[idx].name);
        free(table->entries[idx].value);
        table->count--;
    }
}

void zeus_hpack_table_add(
    zeus_hpack_table_t *table,
    const char *name, size_t nlen,
    const char *value, size_t vlen
) {
    if (!name || !value) return;

    /**
     * Hpack override for 32 bytes.
     */

    size_t entry_size = nlen + vlen + 32;
    hpack_table_evict(table, entry_size);

    char *name_copy  = strndup(name, nlen);
    char *value_copy = strndup(value, vlen);

    if (!name_copy || !value_copy) {
        free(name_copy); free(value_copy);
        return;
    }

    zeus_hpack_entry_t *new_entries = realloc(table->entries, sizeof(zeus_hpack_entry_t) * (table->count + 1));
    if (!new_entries) {
        free(name_copy); free(value_copy);
        return;
    }
    table->entries = new_entries;

    /**
     * In HPACK, new entries will be added in index 0 (LIFO-like).
     */

    if (table->count > 0) {
        memmove(&table->entries[1], &table->entries[0], sizeof(zeus_hpack_entry_t) * table->count);
    }

    table->entries[0].name  = name_copy;
    table->entries[0].value = value_copy;
    table->entries[0].size  = entry_size;

    table->count++;
    table->current_size += entry_size;
}

/**
 * Index resolver.
 */

void hpack_resolve_index(zeus_conn_t *conn, uint32_t index, const char **name, const char **value) {
    if (!name || !value || index == 0) return;

    *name = NULL;
    *value = NULL;

    if (index <= HPACK_STATIC_TABLE_SIZE) {
        *name  = static_table[index - 1].name;
        *value = static_table[index - 1].value;
        return;
    }

    uint32_t dyn_index = index - HPACK_STATIC_TABLE_SIZE - 1;

    if (dyn_index < conn->h2_dynamic_table.count) {
        *name  = conn->h2_dynamic_table.entries[dyn_index].name;
        *value = conn->h2_dynamic_table.entries[dyn_index].value;
    }
}

/**
 * HPACK decoder (simplified, but it works by now :D)
 */

void zeus_hpack_decode(
    zeus_conn_t *conn,
    const uint8_t *payload,
    size_t len,
    zeus_request_t *req
) {
    size_t pos = 0;

    while (pos < len) {
        uint8_t byte = payload[pos];

        /**
         * Indexed header field.
         */

        if (byte & 0x80) {
            uint32_t index = byte & 0x7F;
            const char *name = NULL;
            const char *value = NULL;

            hpack_resolve_index(conn, index, &name, &value);

            if (name && value) {
                if (!strcmp(name, ":path")) {
                    free(req->path); req->path = strdup(value);
                } else if (!strcmp(name, ":method")) {
                    free(req->method); req->method = strdup(value);
                }
            }
            pos++;
        }

        /**
         * Literal with incremental indexing.
         */

        else if ((byte & 0xC0) == 0x40) {
            uint32_t name_idx = byte & 0x3F;
            pos++;

            const char *name_ptr = NULL;
            char *tmp_name = NULL;
            size_t final_nlen = 0;

            if (name_idx > 0) {
                const char *unused = NULL;
                hpack_resolve_index(conn, name_idx, &name_ptr, &unused);
                if (name_ptr) final_nlen = strlen(name_ptr);
            } else {
                if (pos >= len) break;
                uint8_t h = payload[pos] & 0x80;
                uint32_t l = payload[pos++] & 0x7F;
                if (pos + l > len) break;

                tmp_name = malloc(l * 4 + 1); /** Safe buffer for huffman. */
                if (h) {
                    final_nlen = zeus_hpack_huffman_decode(&payload[pos], l, tmp_name);
                    tmp_name[final_nlen] = '\0';
                } else {
                    memcpy(tmp_name, &payload[pos], l);
                    tmp_name[l] = '\0';
                    final_nlen = l;
                }
                name_ptr = tmp_name;
                pos += l;
            }

            /**  Value */

            if (pos >= len) { free(tmp_name); break; }
            uint8_t hv = payload[pos] & 0x80;
            uint32_t lv = payload[pos++] & 0x7F;
            if (pos + lv > len) { free(tmp_name); break; }

            char *value_str = malloc(lv * 4 + 1);
            size_t final_vlen = 0;
            if (hv) {
                final_vlen = zeus_hpack_huffman_decode(&payload[pos], lv, value_str);
                value_str[final_vlen] = '\0';
            } else {
                memcpy(value_str, &payload[pos], lv);
                value_str[lv] = '\0';
                final_vlen = lv;
            }

            if (name_ptr && value_str) {
                zeus_hpack_table_add(&conn->h2_dynamic_table, name_ptr, final_nlen, value_str, final_vlen);

                if (!strcmp(name_ptr, ":path")) {
                    free(req->path); req->path = strdup(value_str);
                } else if (!strcmp(name_ptr, ":method")) {
                    free(req->method); req->method = strdup(value_str);
                }
            }

            pos += lv;
            free(tmp_name);
            free(value_str);
        }
        else {
            /**
             * Ignores another types of frames at the moment just to not crash.
             */
            pos++;
        }
    }
}
