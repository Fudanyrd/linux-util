#include "http.h"
#include "defer.h"
#include <cctype>
#include <cstring>

#include <arpa/inet.h>
#include <iostream>

int open_clientfd(const struct sockaddr *addr, size_t len) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    perror("socket");
    return fd;
  }

  if (connect(fd, addr, len) < 0) {
    perror("connect");
    close(fd);
    return -1;
  }
  return fd;
}

int test_http_main(int argc, char **argv, char **envp) {
  HttpClient client;

  struct sockaddr_in saddr;
  saddr.sin_family = AF_INET;
  *(uint32_t *)&saddr.sin_addr = 0xcb117a2f /* jyywiki.cn */;
  saddr.sin_port = htons(80);
  std::vector<unsigned char> dummy;

  int rfd = open_clientfd((const sockaddr *)&saddr);
  if (rfd < 0) {
    return 1;
  }
  SockFd sfd(rfd);
  auto ret = client.request(sfd, "GET", "jyywiki.cn", "/OS/2022/index.html",
                            dummy, "");

  ret.printheader(stderr);
  auto nw = write(1, ret.data_.data(), ret.data_.size());
  dbg.log("written %ld bytes\n", nw);
  return 0;
}
