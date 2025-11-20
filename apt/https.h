#ifndef _HTTPS_H_
#define _HTTPS_H_

#ifdef CONFIG_HAS_SSL
#include <openssl/err.h>
#include <openssl/ssl.h>

struct SSLSockFd {
  SSLSockFd() = default;

  SSLSockFd(int sockfd, SSL_CTX *ctx) {
    ssl_ = SSL_new(ctx);
    if (ssl_) {
      if (SSL_set_fd(ssl_, sockfd) && SSL_connect(ssl_) == 1) {
        /* Success. */
      } else {
        SSL_free(ssl_);
        ssl_ = nullptr;
      }
    }
  }

  void close() {
    if (ssl_) {
      SSL_shutdown(ssl_);
      SSL_free(ssl_);
      ssl_ = nullptr;
    }
  }

  bool valid() const { return ssl_; }

  ssize_t read(void *buf, size_t len) {
    ssize_t nr = SSL_read(ssl_, buf, (int)len);
    return nr;
  }

  ssize_t write(const void *buf, size_t len) {
    ssize_t nw = SSL_write(ssl_, buf, (int)len);
    return nw;
  }

  SSL *ssl_{nullptr};

  static SSL_CTX *SSLAllocContext() {
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (ctx) {
      SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
      if (!SSL_CTX_set_default_verify_paths(ctx)) {
        SSL_CTX_free(ctx);
        ctx = nullptr;
      }
    }
    return ctx;
  }
};

#include "http.h"

using HttpsClient = HttpClientTmpl<SSLSockFd>;
int test_https_main(int argc, char **argv, char **envp = nullptr);

#endif /* CONFIG_HAS_SSL */
#endif /* _HTTPS_H_ 1*/
