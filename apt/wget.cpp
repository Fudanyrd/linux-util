#include "defer.h"
#include "dns.h"
#include "http.h"
#include "https.h"
#include "url.h"

#include <cstdio>
#include <fcntl.h>

#ifdef CONFIG_HAS_SSL
template class HttpClientTmpl<SSLSockFd>;

struct Wget {
public:
  Wget()
      : s_buf_(), dns_client_(s_buf_), http_client_(s_buf_),
        https_client_(s_buf_) {}
  Wget(SSL_CTX *ctx)
      : s_buf_(), dns_client_(s_buf_), http_client_(s_buf_),
        https_client_(s_buf_), ctx_(ctx) {}

  ~Wget() {
    if (ctx_)
      SSL_CTX_free(ctx_);
  }

  /**
   * @return 0 on success; else failure.
   */
  int download(const char *url, const char *ofile = nullptr);

private:
  SharedBuf s_buf_;
  Resolver dns_client_;
  HttpClient http_client_;
  HttpsClient https_client_;
  SSL_CTX *ctx_{nullptr};

  static void url_error(const char *fmt, ...) { fprintf(stderr, fmt); }

  template <typename _Sock>
  int download(HttpClientTmpl<_Sock> &client, int ofd, const std::string &host,
               const std::string &path, const std::string &method = "GET",
               const std::string &cookies = "") {

    try {
      auto [tokens, content_len] = client.connect();
    } catch (std::runtime_error &ex) {
      dbg.log("wget error: %s\n", ex.what());
      return 1;
    }
  }
};

int main(int argc, char **argv) {
  SSL_CTX *ctx = SSLSockFd::SSLAllocContext();
  if (!ctx) {
    perror("wget:");
    dbg.log("ssl initialization failed.\n");
    return 1;
  }

  dbg.on();
  Wget worker(ctx);
  if (argv[1]) {
    return worker.download(argv[1]);
  }
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
  std::string host(url + result.host.begin, url + result.host.end);

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

  int ret = 1;
  dbg.log("saving to %s\n", ofile);
  struct sockaddr saddr;
  for (const auto &addr : addrs) {
    addr.fill(&saddr, result.port);
    int ofd = open(ofile, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (ofd < 0) {
      perror("open");
      continue;
    }
    FdConsumer file_consumer(ofd);
    int rfd = open_clientfd(&saddr);
    if (rfd < 0) {
      perror("connect:");
      continue;
    }

    int status;
    switch (result.proto) {
    case (PROTO_HTTP): {
      SockFd sfd(rfd);
      status = http_client_.wget(sfd, file_consumer, "GET", host, path);
      break;
    }
    case (PROTO_HTTPS): {
      SSLSockFd ssfd(rfd, this->ctx_);
      status = https_client_.wget(ssfd, file_consumer, "GET", host, path);
      break;
    }
    }

    if (status / 100 != 5 /* Not the server error. */) {
      ret = (status == 200) ? 0 /* exit ok */ : 1;
      break;
    }
  }

  return ret;
}

#else

int main(int argc, char **argv) { return test_http_main(argc, argv); }

#endif /* CONFIG_HAS_SSL */
