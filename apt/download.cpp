#include <arpa/inet.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sys/socket.h>
#include <sys/types.h>

#include "defer.h"
#include "dns.h"
#include "http.h"

static void usage() {
  printf("Usage: apt-download [resource-path] [cookie-file]\n");
}

int main(int argc, char **argv) {
  dbg.on();
  if (!argv[1] || strcmp(argv[1], "--help") == 0) {
    usage();
    return 0;
  }

  std::string cookies;
  if (argv[2]) {
    std::ifstream fs((const char *)argv[2]);
    std::string line;
    while (std::getline(fs, line)) {
      cookies += line;
      cookies += "\r\n";
    }
  }

  const char *domain = "archive.ubuntu.com";
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

  std::vector<Ipv4Addr> addrs;
  do {
    Resolver resolv("/etc/resolv.conf");
    addrs = resolv.LookupV4(domain);
  } while (0);

  struct sockaddr_in saddr;
  saddr.sin_family = AF_INET;
#if 0
  /* HTTPS won't work. DELETE ME */
  saddr.sin_port = htons(443);
  for (const auto &addr : addrs) {
    const unsigned char *b = (const unsigned char *)addr.addr_;
    fprintf(stderr, "Trying %d.%d.%d.%d\n", b[0], b[1], b[2], b[3]);
    *(uint32_t *)&saddr.sin_addr = *(uint32_t *)addr.addr_;
    int rfd = open_clientfd((const sockaddr *)&saddr);
    if (rfd < 0) {
      return 1;
    }
    defer(close(rfd));

    SSL *ssl = SSL_new(ctx);
    if (!SSL_set_fd(ssl, rfd)) {
      dbg.log("\tSSL_set_fd failed\n");
      continue;
    }

    SSLSockFd ssfd;
    ssfd.ssl_ = ssl;

    HttpsClient client;
    if (SSL_connect(ssl) != 1) {
      dbg.log("\tSSL_connect failed\n");
      continue;
    }

    std::vector<unsigned char> dummy;
    auto ret =
        client.request(ssfd, "GET", domain, argv[1], dummy, true, cookies);
    (void)write(1, ret.data(), ret.size());
  }
#endif
  saddr.sin_port = htons(80);
  HttpClient client;
  for (const auto &addr : addrs) {
    const unsigned char *b = (const unsigned char *)addr.addr_;
    fprintf(stderr, "Trying %d.%d.%d.%d\n", b[0], b[1], b[2], b[3]);
    *(uint32_t *)&saddr.sin_addr = *(uint32_t *)addr.addr_;
    int rfd = open_clientfd((const sockaddr *)&saddr);
    if (rfd < 0) {
      continue;
    }
    defer(close(rfd));

    SockFd sfd(rfd);
    HttpResponse ret;

    /* Safely connect to remote, catch possible IO errors. */
    try {
      std::vector<unsigned char> dummy;
      ret = client.request(sfd, "GET", domain, argv[1], dummy, cookies);
    } catch (std::runtime_error &ex) {
      dbg.log("dowload error: %s\n", ex.what());
      continue;
    }

    /* Validate status code. */
    if (ret.status() != 200) {
      fprintf(stderr, "Error status(reason: %s)\n",
              ret.header_tokens_[2].c_str());
      continue;
    }

    /* OK. */
    ret.printheader(stderr);
    auto nw = write(1, ret.data_.data(), ret.data_.size());
    dbg.log("written %ld bytes\n", nw);
    break;
  }

  return 0;
}
