#ifdef CONFIG_HAS_SSL

#include "https.h"
#include "defer.h"
#include <arpa/inet.h>

int test_https_main(int argc, char **argv, char **envp) {
  struct sockaddr_in saddr;
  saddr.sin_family = AF_INET;
  *(uint32_t *)&saddr.sin_addr = 0xcb117a2f /* jyywiki.cn */;
  saddr.sin_port = htons(443);
  int rfd = open_clientfd((const sockaddr *)&saddr);
  if (rfd < 0) {
    return 1;
  }
  defer(close(rfd));

  const SSL_METHOD *method = TLS_client_method();
  SSL_CTX *ctx = SSL_CTX_new(method);
  if (!ctx) {
    perror("wget: ssl_ctx_new: ");
    return 1;
  }
  defer(SSL_CTX_free(ctx));
  SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
  if (!SSL_CTX_set_default_verify_paths(ctx)) {
    dbg.log("libssl: "
            "Failed to set up trust store\n");
    return 1;
  }

  SSL *ssl = SSL_new(ctx);
  if (!SSL_set_fd(ssl, rfd)) {
    dbg.log("SSL_set_fd failed\n");
    return 1;
  }

  SSLSockFd ssfd;
  ssfd.ssl_ = ssl;

  HttpsClient client;
  if (SSL_connect(ssl) != 1) {
    dbg.log("SSL_connect failed\n");
    return 1;
  }

  std::vector<unsigned char> dummy;
  auto ret = client.request(ssfd, "GET", "jyywiki.cn", "/OS/2022/index.html",
                            dummy, "");
  ret.printheader(stderr);
  auto nw = write(1, ret.data_.data(), ret.data_.size());
  dbg.log("written %ld bytes\n", nw);
  return 0;
}
#endif /* CONFIG_HAS_SSL */
