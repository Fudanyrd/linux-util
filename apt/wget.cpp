#include "dns.h"
#include "http.h"
#include "url.h"

#include <cstdio>

struct Wget {
public:
  Wget() = default;

  /**
   * @return 0 on success; else failure.
   */
  int download(const char *url, const char *ofile = nullptr);

private:
  Resolver dns_client_;
  HttpClient http_client_;
  HttpsClient https_client_;

  static void url_error(const char *fmt, ...) { fprintf(stderr, fmt); }
};

int main(int argc, char **argv) {
  test_https_main(argc, argv);
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
  }

  /* TODO: download and save. */
  return 0;
}
