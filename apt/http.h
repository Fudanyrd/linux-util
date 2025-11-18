#ifndef _HTTP_H_
#define _HTTP_H_ 1

#include "nio.h"

#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <vector>

class HttpClient {
public:
  HttpClient() = default;

  std::vector<unsigned char>
  request(const sockaddr *addr, const std::string &method /* = "GET" | "HOST"*/,
          const std::string &host /* eg. "www.aol.com" */,
          const std::string &path /* eg. /index.html */ = "/",
          const std::vector<unsigned char> &content = {},
          bool showServerResponse = false);

  static int test_main(int argc, char **argv, char **envp = nullptr);

private:
  NIOBuf<SockFd> buf_;
};

#endif /* _HTTP_H_ 1 */
