#ifndef _HTTPS_EXT_H
#define _HTTPS_EXT_H 1
/**
 * <h1>HTTPS extension for http daemon</h1>
 *
 * Must have <i>-lssl</i>.
 */
#include <openssl/err.h>
#include <openssl/ssl.h>

#include "httpd.h"

struct SSLSocket : public Socket {
  SSL *ssl{nullptr};

  SSLSocket() = default;
  ~SSLSocket() {}

  void close() override {
    /* libssl does not check null pointer. */
    if (ssl) {
      SSL_shutdown(ssl);
      SSL_free(ssl);
      ssl = nullptr;
    }
  }

  int setConnectFd(int fd, SocketProvider *provider) override;
  int setConnectFd(int fd, ClientProvider *provider) override;

  ssize_t read(void *buf, size_t len) override {
    return SSL_read(ssl, buf, (int)len);
  }

  ssize_t write(const void *buf, size_t len) override {
    return SSL_write(ssl, buf, (int)len);
  }
};

struct SSLSocketProvider : public InetSocketProvider {
  SSL_CTX *ctx;

  SSLSocketProvider(SSL_CTX *ctx, int port = 8080)
      : InetSocketProvider(port), ctx(ctx) {}
  ~SSLSocketProvider() override {
    SSL_CTX_free(ctx);
    close(sockFd);
  }

  static SSL_CTX *createSSLContext(const char *certChainFile,
                                   const char *privateKeyFile);

};

struct SSLClientProvider : public HttpClientProvider {
  SSL_CTX *ctx;
  const char *host;

  SSLClientProvider(const char *myHost, int port = 443) 
    : HttpClientProvider(myHost, port), ctx(createClientContext()) {
    http_assert (ctx != nullptr && "cannot create ctx");
    this->host = myHost;
  }

  ~SSLClientProvider() { SSL_CTX_free(ctx); }
  static SSL_CTX *createClientContext(void);

  const char *getHost(void) const {
    return this->host;
  }
};

#endif /* _HTTPS_EXT_H */
