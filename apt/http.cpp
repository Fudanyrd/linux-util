#include "http.h"
#include <cctype>
#include <cstring>

#include <arpa/inet.h>
#include <iostream>

using Token = std::string;

std::vector<unsigned char> HttpClient::request(
    const sockaddr *addr, const std::string &method /* = "GET" | "POST"*/,
    const std::string &host /* eg. "www.aol.com" */,
    const std::string &path /* eg. /index.html */,
    const std::vector<unsigned char> &content, bool showServerResponse) {
  std::string req_header =
      method + " " + path + " HTTP/1.1\r\nHost: " + host +
      "\r\nUser-Agent: Mozilla-5.0\r\nConnection: close\r\n";

  std::vector<unsigned char> ret;
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    perror("socket");
    return ret;
  }

  if (connect(fd, addr, sizeof(*addr)) < 0) {
    perror("connect");
    close(fd);
    return ret;
  }

  SockFd sfd(fd);
  buf_.reset(sfd);

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
  do {
    int ch = buf_.getch();
    std::vector<Token> tokens;
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

    if (showServerResponse) {
      for (const auto &token : tokens) {
        std::cerr << ' ' << token;
      }
    }

  } while (0);

  ret = std::vector<unsigned char>(content_len);
  dbg.log("parsed content length = %ld\n", content_len);
  buf_.read(ret.data(), content_len);
  buf_.close_sock();

  return ret;
}

int HttpClient::test_main(int argc, char **argv, char **envp) {
  HttpClient client;

  struct sockaddr_in saddr;
  saddr.sin_family = AF_INET;
  *(uint32_t *)&saddr.sin_addr = 0xcb117a2f /* jyywiki.cn */;
  saddr.sin_port = htons(80);
  std::vector<unsigned char> dummy;
  auto ret = client.request((const sockaddr *)&saddr, "GET", "jyywiki.cn",
                            "/OS/2022/index.html", dummy, true);

  write(1, ret.data(), ret.size());
  return 0;
}
