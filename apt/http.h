#ifndef _HTTP_H_
#define _HTTP_H_ 1

#include "nio.h"

#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <vector>

template <typename _Context /* implements close,valid,read,write,copyable */>
class HttpClientTmpl {
public:
  HttpClientTmpl() = default;

  std::vector<unsigned char>
  request(_Context &ctx, const std::string &method /* = "GET" | "HOST"*/,
          const std::string &host /* eg. "www.aol.com" */,
          const std::string &path /* eg. /index.html */ = "/",
          const std::vector<unsigned char> &content = {},
          bool showServerResponse = false);

private:
  NIOBuf<_Context> buf_;
};

using HttpClient = HttpClientTmpl<SockFd>;
int test_http_main(int argc, char **argv, char **envp = nullptr);

using HttpsClient = HttpClientTmpl<SSLSockFd>;
int test_https_main(int argc, char **argv, char **envp = nullptr);

#endif /* _HTTP_H_ 1 */
