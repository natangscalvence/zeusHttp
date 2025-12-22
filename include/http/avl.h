#ifndef ZEUS_AVL_H
#define ZEUS_AVL_H

#include "../http/http.h"
#include <stdint.h>

typedef struct zeus_h2_stream {
    uint32_t id;
    zeus_request_t req;
    zeus_response_t res;

    struct zeus_h2_stream *left;
    struct zeus_h2_stream *right;
    int height;
} zeus_h2_stream_t;

zeus_h2_stream_t* avl_insert(zeus_h2_stream_t* root, uint32_t id);
zeus_h2_stream_t* avl_find(zeus_h2_stream_t* root, uint32_t id);
void avl_free(zeus_h2_stream_t* root);

#endif // ZEUS_AVL_H