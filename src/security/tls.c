/**
 * tls.c 
 * Manages the OpenSSL context for zeusHttp.
 */

#include "../../include/zeushttp.h"
#include "../../include/core/server.h"
#include "../../include/core/log.h"
#include <stdio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

static const unsigned char ALPN_SERVER_PROTOS[] = "\x02h2\x08http/1.1";
static const unsigned int ALPN_SERVER_PROTOS_LEN = sizeof(ALPN_SERVER_PROTOS) - 1;

/**
 * Select the ALPN protocol comparing the list offered by the client (in)
 * with the list of server.
 */

static int alpn_select_cb(SSL *ssl, unsigned char **out, 
    unsigned char *outlen, const unsigned char *in, unsigned int inlen, void *arg) {

    int status = SSL_select_next_proto((unsigned char **)out, outlen,
        ALPN_SERVER_PROTOS, ALPN_SERVER_PROTOS_LEN, in, inlen);

    if (status != OPENSSL_NPN_NEGOTIATED) {
        return SSL_TLSEXT_ERR_NOACK;
    }

    return SSL_TLSEXT_ERR_OK;
}

int tls_context_init(zeus_server_t *server, const char *cert_file, const char *key_file) {
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    const SSL_METHOD *method = TLS_server_method();
    server->ssl_ctx = SSL_CTX_new(method);

    if (!server->ssl_ctx) {
        ERR_print_errors_fp(stderr);
        fprintf(stderr, "TLS Error: Failed to create SSL context.\n");
        return -1;
    }

    /**
     * Load cert from server.
     */


    if (SSL_CTX_use_certificate_file(server->ssl_ctx, cert_file, SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        fprintf(stderr, "TLS Error: Failed to load server certificate file: %s\n", cert_file);
        SSL_CTX_free(server->ssl_ctx);
        return -1;
    }

    /**
     * Load the private key
     */

    if (SSL_CTX_use_PrivateKey_file(server->ssl_ctx, key_file, SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        fprintf(stderr, "TLS Error: Failed to load server certificate file: %s\n", key_file);
        SSL_CTX_free(server->ssl_ctx);
        return -1;
    }

    /**
     * Verify if the private key and certificate match 
     */

    if (!SSL_CTX_check_private_key(server->ssl_ctx)) {
        fprintf(stderr, "TLS Error: Private key does not match the certificate public key.\n");
        SSL_CTX_free(server->ssl_ctx);
        return -1;
    }

    if (SSL_CTX_set_alpn_protos(server->ssl_ctx, ALPN_SERVER_PROTOS, ALPN_SERVER_PROTOS_LEN) != 0) {
        fprintf(stderr, "TLS Error: Failed to set ALPN protocols.\n");
        SSL_CTX_free(server->ssl_ctx);
        return -1;
    }

    SSL_CTX_set_alpn_select_cb(server->ssl_ctx, alpn_select_cb, NULL);

    ZLOG_INFO("TLS: ALPN configured (h2, http/1.1) and callback registered.");
    ZLOG_INFO("TLS: SSL Context successfully initialized.");
    return 0;
}