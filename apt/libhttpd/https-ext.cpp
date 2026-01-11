#include "https-ext.h"

int SSLSocket::setConnectFd(int fd, ClientProvider *myProvider) {
  auto *provider = dynamic_cast<SSLClientProvider *>(myProvider);
  http_assert(provider && "not an instance of SSLClientProvider");

  this->ssl = SSL_new(provider->ctx);
  if (!this->ssl) return ENOMEM;
  if (!SSL_set_fd(ssl, fd)) {
    return 1;
  }
  SSL_set_tlsext_host_name(ssl, provider->getHost());
  if (!SSL_set1_host(ssl, provider->getHost())) {
    return 1;
  }

  return (SSL_connect(ssl) == 1) ? 0 : 1;
}

int SSLSocket::setConnectFd(int fd, SocketProvider *myProvider) {
  SSLSocketProvider *provider = dynamic_cast<SSLSocketProvider *>(myProvider);
  http_assert(provider && "not an instance of SSLSocketProvider");
  this->ssl = SSL_new(provider->ctx);
  if (!this->ssl) {
    return ENOMEM;
  }
  if (!SSL_set_fd(ssl, fd)) {
    return 1;
  }
  if (SSL_accept(ssl) <= 0) {
    return 1;
  }

  return 0;
}

SSL_CTX *SSLSocketProvider::createSSLContext(const char *certChainFile,
                                             const char *privateKeyFile) {
  const SSL_METHOD *method;
  SSL_CTX *ctx;
  method = TLS_server_method();

  ctx = SSL_CTX_new(method);

  if (ctx == nullptr) {
    return nullptr;
  }

  if (SSL_CTX_use_certificate_chain_file(ctx, certChainFile) <= 0) {
    SSL_CTX_free(ctx);
    return nullptr;
  }
  if (SSL_CTX_use_PrivateKey_file(ctx, privateKeyFile, SSL_FILETYPE_PEM) <= 0) {
    SSL_CTX_free(ctx);
    return nullptr;
  }
  return ctx;
}

SSL_CTX *SSLClientProvider::createClientContext() {
  const SSL_METHOD *method;
  SSL_CTX *ctx;
  method = TLS_client_method();

  ctx = SSL_CTX_new(method);

  if (ctx == nullptr) {
    return nullptr;
  }

  SSL_CTX_set_default_verify_paths(ctx);
  SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
  return ctx;
}
