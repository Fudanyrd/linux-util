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
  HttpClientTmpl(SharedBuf &sb) : buf_(sb) {}

  /**
   * A wrapper of connect-read-close routine.
   */
  HttpResponse request(_Context &ctx,
                       const std::string &method /* = "GET" | "POST"*/,
                       const std::string &host /* eg. "www.aol.com" */,
                       const std::string &path /* eg. /index.html */ = "/",
                       const std::vector<unsigned char> &content = {},
                       const std::string &cookies = "");

  /**
   * Send request header and parse response header.
   * @return [Tokenized response header, data length]
   */
  std::pair<std::vector<std::string>, size_t>
  connect(_Context &ctx, const std::string &method, const std::string &host,
          const std::string &path,
          const std::vector<unsigned char> &content = {},
          const std::string &cookies = "");
  /**
   * After `connect`, read the data for you to use.
   */
  size_t read(void *buf, size_t len) { return this->buf_.read(buf, len); }
  template <typename _Consumer> void consume(_Consumer &consumer, size_t len) {
    this->buf_.consume(consumer, len);
  }
  /**
   * In the end, remember to close the socket.
   */
  void close() { buf_.close_sock(); }

  /* Used by wget to download file. */
  int wget(_Context &ctx, FdConsumer &consumer,
           const std::string &method /* = "GET" | "POST"*/,
           const std::string &host /* eg. "www.aol.com" */,
           const std::string &path /* eg. /index.html */ = "/",
           const std::vector<unsigned char> &content = {},
           const std::string &cookies = "");

private:
  NIOBuf<_Context> buf_;
};

template <typename _Context>
std::pair<std::vector<std::string>, size_t> HttpClientTmpl<_Context>::connect(
    _Context &ctx, const std::string &method, const std::string &host,
    const std::string &path, const std::vector<unsigned char> &content,
    const std::string &cookies) {
  using Token = std::string;
  std::string req_header =
      method + " " + path + " HTTP/1.1\r\nHost: " + host +
      "\r\nUser-Agent: Mozilla-5.0\r\nConnection: keep-alive\r\n";
  req_header += cookies;

  buf_.reset(ctx);

  /* Write http requests. */
  buf_.write(req_header.c_str(), req_header.size());
  if (content.empty()) {
  } else {
    /* Write content */
    char buf[128];
    sprintf(buf, "Content-Length: %ld\r\n", content.size());
    buf_.write(buf, strlen(buf));
    buf_.write((const void *)content.data(), content.size());
  }
  buf_.write("\r\n", 2);
  buf_.flush();

  /* Start reading responses. */
  size_t content_len = 0;
  bool set_len = false;
  bool succ = false;
  std::vector<Token> tokens;
  do {
    int ch = buf_.getch();
    tokens.reserve(48);
    Token current;
    while (ch != EOF) {
      if (isblank(ch)) {
        ch = buf_.getch();
        continue;
      }

      if (ch == '\r') {
        ch = buf_.getch();
        if (ch == '\n') {
          /* End of line. */
          if (tokens.back() == "\n") {
            succ = true;
            break;
          }
          tokens.push_back("\n");
        }
        ch = buf_.getch();
        continue;
      }

      /* isgraph(ch); */
      while (ch != EOF && isgraph(ch)) {
        current.push_back(ch);
        ch = buf_.getch();
      }
      if (set_len) {
        content_len = atol(current.c_str());
        set_len = false;
      }
      if (current == "Content-Length:") {
        set_len = true;
      }
      tokens.push_back(current);
      current.clear();
    }

  } while (0);

  if (!succ) {
    return {tokens, 0};
  }
  return {tokens, content_len};
}

template <typename _Context>
HttpResponse HttpClientTmpl<_Context>::request(
    _Context &ctx, const std::string &method /* = "GET" | "POST"*/,
    const std::string &host /* eg. "www.aol.com" */,
    const std::string &path /* eg. /index.html */,
    const std::vector<unsigned char> &content, const std::string &cookies) {

  std::vector<unsigned char> ret;
  auto [tokens, content_len] =
      this->connect(ctx, method, host, path, content, cookies);

  ret = std::vector<unsigned char>(content_len);
  dbg.log("parsed content length = %ld\n", content_len);
  buf_.read(ret.data(), content_len);
  // buf_.close_sock();

  return HttpResponse(tokens, ret);
}

template <typename _Context>
int HttpClientTmpl<_Context>::wget(
    _Context &ctx, FdConsumer &consumer,
    const std::string &method /* = "GET" | "POST"*/,
    const std::string &host /* eg. "www.aol.com" */,
    const std::string &path /* eg. /index.html */,
    const std::vector<unsigned char> &content, const std::string &cookies) {
  dbg.log("%s %s %s\n", method.c_str(), host.c_str(), path.c_str());
  dbg.log("HTTP request sent, awaiting response... ");
  auto conn_res = this->connect(ctx, method, host, path, content, cookies);
  auto &tokens = conn_res.first;
  size_t data_len = conn_res.second;
  if (tokens.size() < 3) {
    this->close();
    dbg.log("invalid response header\n");
    return 0;
  }

  const int status = atoi(tokens[1].c_str());
  if (status != 200) {
    /* Error status. Print  */
    this->close();
    dbg.log("%d", status);
    auto n_tokens = tokens.size();
    for (size_t i = 2; i < n_tokens; i++) {
      if (tokens[i] == "\n")
        break;
      dbg.log(" %s", tokens[i].c_str());
    }
    dbg.log("\n");
    return status;
  }

  consumer.n_written_ = 0;
  this->consume(consumer, data_len);
  this->close();
  dbg.log("200 OK\n");
  if (consumer.n_written_ < data_len) {
    return 501 /* Fake an server error. */;
  }
  return status /* = 200 */;
}

using HttpClient = HttpClientTmpl<SockFd>;
int test_http_main(int argc, char **argv, char **envp = nullptr);

int open_clientfd(const struct sockaddr *addr,
                  size_t len = sizeof(struct sockaddr));

/**
 * Test connection to addr, and close the socket.
 * @return 0 if a connection can be made; else errno.
 */
static inline int spider(const struct sockaddr *addr,
                         size_t len = sizeof(struct sockaddr)) {
  int sfd = open_clientfd(addr);
  if (sfd < 0) {
    return errno;
  }
  close(sfd);
  return 0;
}

#endif /* _HTTP_H_ 1 */
