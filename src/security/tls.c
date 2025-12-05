/**
 * tls.c 
 * Manages the OpenSSL context for zeusHttp.
 */

#include "../../include/zeushttp.h"
#include "../../include/core/server.h"
#include <stdio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

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

    /**
     * ALPN Configuration.
     * Define the protocols supported by the server: h2 and http/1.1
     */
    const unsigned char alpn_protos[] = "\x02h2\x08http/1.1";

    if (SSL_CTX_set_alpn_protos(server->ssl_ctx, alpn_protos, sizeof(alpn_protos) - 1) != 0) {
        fprintf(stderr, "TLS Error: Failed to set ALPN protocols.\n");
        SSL_CTX_free(server->ssl_ctx);
        return -1;
    }

    printf("TLS: ALPN Configured (h2, http/1.1).\n");
    printf("TLS: SSL Context successfully initialized.\n");
    return 0;
}