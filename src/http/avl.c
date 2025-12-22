#include "../../include/http/avl.h"
#include <stdlib.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define HEIGHT(n) ((n) ? (n)->height : 0)

static zeus_h2_stream_t* rotate_right(zeus_h2_stream_t *y) {
    zeus_h2_stream_t *x = y->left;
    zeus_h2_stream_t *t = x->right;

    x->right = y;
    y->left = t;
    y->height = MAX(HEIGHT(y->left), HEIGHT(y->right)) + 1;
    x->height = MAX(HEIGHT(x->left), HEIGHT(x->right)) + 1;

    return x;
}

static zeus_h2_stream_t* rotate_left(zeus_h2_stream_t *x) {
    zeus_h2_stream_t *y = x->right;
    zeus_h2_stream_t *t = y->left;
    y->left = x;
    x->right = t;
    x->height = MAX(HEIGHT(x->left), HEIGHT(x->right)) + 1;
    y->height = MAX(HEIGHT(y->left), HEIGHT(y->right)) + 1;
    return y;
}

zeus_h2_stream_t* avl_insert(zeus_h2_stream_t* node, uint32_t id) {
    if (!node) {
        zeus_h2_stream_t* new_node = calloc(1, sizeof(zeus_h2_stream_t));
        new_node->id = id;
        new_node->height = 1;
        return new_node;
    }

    if (id < node->id) {
        node->left = avl_insert(node->left, id);
    } else if (id > node->id) {
        node->right = avl_insert(node->right, id);
    } else {
        return node;
    }

    node->height = 1 + MAX(HEIGHT(node->left), HEIGHT(node->right));
    int balance = HEIGHT(node->left) - HEIGHT(node->right);

    if (balance > 1 && id < node->left->id) {
        return rotate_right(node);
    } 
    
    if (balance < -1 && id > node->right->id) {
        return rotate_left(node);
    } 
    
    if (balance > 1 && id > node->left->id) {
        node->left = rotate_left(node->left);
        return rotate_right(node);
    } 
    
    if (balance < 1 && id < node->right->id) {
        node->right = rotate_right(node->right);
        return rotate_left(node);
    }

    return node;
}

zeus_h2_stream_t* avl_find(zeus_h2_stream_t* root, uint32_t id) {
    while (root) {
        if (id == root->id) {
            return root;
            root = (id < root->id) ? root->left : root->right;
        }
    }
    return NULL;
}

void avl_free(zeus_h2_stream_t *root) {
    if (!root) {
        return;
    }

    avl_free(root->left);
    avl_free(root->right);

    if (root->req.path) {
        // free((void*)root->req_path);     /** Only useful if the HPACK uses malloc... */
    }

    free(root);
}