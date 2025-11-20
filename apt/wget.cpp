#include "dns.h"
#include "defer.h"
#include "http.h"
#include "url.h"

#include <fcntl.h>
#include <cstdio>

#ifdef CONFIG_HAS_SSL
struct Wget {
public:
  Wget() = default;
  Wget(SSL_CTX *ctx) : ctx_(ctx) {}

  ~Wget() {
    if (ctx_)
      SSL_CTX_free(ctx_);
  }

  /**
   * @return 0 on success; else failure.
   */
  int download(const char *url, const char *ofile = nullptr);

private:
  Resolver dns_client_;
  HttpClient http_client_;
  HttpsClient https_client_;
  SSL_CTX *ctx_{nullptr};

  static void url_error(const char *fmt, ...) { fprintf(stderr, fmt); }

  template <typename _Sock>
  int download(HttpClientTmpl<_Sock> &client, int ofd,
    const std::string &host, const std::string &path, 
    const std::string &method = "GET",
    const std::string &cookies = "") {
    
    try {
      auto [tokens, content_len] = client.connect()
    } catch (std::runtime_error &ex) {
      dbg.log("wget error: %s\n", ex.what());
      return 1;
    }
  }
};

int main(int argc, char **argv) {
  test_https_main(argc, argv);

  SSL_CTX *ctx = SSLSockFd::SSLAllocContext();
  if (!ctx) {
    perror("wget:");
    dbg.log("ssl initialization failed.\n");
    return 1;
  }

  Wget worker(ctx);
  return 0;
}

int Wget::download(const char *url, const char *ofile) {
  /* Parse url. */
  url_parse result;
  result.url_error = Wget::url_error;
  int pret = urlparse(url, &result);
  if (pret) {
    return pret;
  }

  /* Get host name. */
  unsigned int hostlen = result.host.end - result.host.begin;
  std::string host(hostlen + 1, (char)0);
  url_strcpy(host.data(), url, &result.host);

  /* Fix ofile. */
  dbg.log("resolving %s:%d\n", host.c_str(), (int)result.port);
  if (ofile == nullptr) {
    if (result.ofile.end == result.ofile.begin) {
      ofile = "index.html";
    } else {
      ofile = url + result.ofile.begin;
    }

    if (*ofile == 0) {
      ofile = "index.html";
    }
  }

  /* Fix path. */
  const char *path = url + result.path.begin;
  if (*path == 0) {
    path = "/";
  }

  /* Search Ip address. */
  auto addrs = dns_client_.LookupV4(host);
  if (addrs.empty()) {
    dbg.log("cannot lookup %s\n", host.c_str());
    return 1;
  }

  dbg.log("saving to %s\n", ofile);
  struct sockaddr saddr;
  for (const auto &addr : addrs) {
    addr.fill(&saddr, result.port);
    int ofd = open(ofile, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (ofd < 0) {
      perror("open");
      continue;
    }
    int rfd = open_clientfd(&saddr);
    if (rfd < 0) {
      close(ofd);
      perror("connect:");
      continue;
    }

    int res;
    switch (result.proto) {
    case (PROTO_HTTP): {
      SockFd sfd(rfd);
      http_client_.connect(sfd, "GET", host, path);
      break;
    }
    case (PROTO_HTTPS): {
      break;
    }
    }

    if (res == 0) {
      break;
    }
  }

  /* TODO: download and save. */
  return 0;
}

#else

int main(int argc, char **argv) {
  return test_http_main(argc, argv);
}

#endif /* CONFIG_HAS_SSL */
