#ifndef _HTTP_H_
#define _HTTP_H_ 1

#include "nio.h"

#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <utility>
#include <vector>

struct HttpResponse {
  HttpResponse() = default;
  HttpResponse(const std::vector<std::string> &tokens,
               const std::vector<unsigned char> &data)
      : header_tokens_(tokens), data_(data) {}
  ~HttpResponse() = default;

  int status(void) const {
    if (header_tokens_.empty()) {
      throw std::domain_error("uninitialized response");
    }

    int ret = atoi(header_tokens_[1].c_str());
    return ret;
  }

  void printheader(FILE *ofile) const {
    for (const auto &token : header_tokens_) {
      fprintf(ofile, " %s", token.c_str());
    }
  }

  std::vector<std::string> header_tokens_;
  std::vector<unsigned char> data_;
};

template <typename _Context /* implements close,valid,read,write,copyable */>
class HttpClientTmpl {
public:
  HttpClientTmpl() = default;

  HttpResponse request(_Context &ctx,
                       const std::string &method /* = "GET" | "HOST"*/,
                       const std::string &host /* eg. "www.aol.com" */,
                       const std::string &path /* eg. /index.html */ = "/",
                       const std::vector<unsigned char> &content = {},
                       const std::string &cookies = "");

private:
  NIOBuf<_Context> buf_;
};

using HttpClient = HttpClientTmpl<SockFd>;
int test_http_main(int argc, char **argv, char **envp = nullptr);

using HttpsClient = HttpClientTmpl<SSLSockFd>;
int test_https_main(int argc, char **argv, char **envp = nullptr);

int open_clientfd(const struct sockaddr *addr);

#endif /* _HTTP_H_ 1 */
