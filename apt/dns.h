#ifndef _DNS_H_
#define _DNS_H_ 1

#include <arpa/inet.h>
#include <string>
#include <sys/socket.h>
#include <vector>

#include "nio.h"

struct sockaddr;

static inline void make_ipv4(struct sockaddr *addr, unsigned char a,
                             unsigned char b, unsigned char c, unsigned char d,
                             unsigned short port) {
  struct sockaddr_in *dst = (struct sockaddr_in *)addr;
  dst->sin_family = AF_INET;
  dst->sin_port = htons(port);
  unsigned char *addrbuf = (unsigned char *)&dst->sin_addr;
  addrbuf[0] = a;
  addrbuf[1] = b;
  addrbuf[2] = c;
  addrbuf[3] = d;
}

struct Ipv4Addr {
  unsigned char addr_[4];
  void fill(struct sockaddr *dst, unsigned int port) const;
};

class Resolver {
public:
  Resolver(const char *config = "/etc/resolv.conf") { do_init(config); }
  Resolver(SharedBuf &sb, const char *config = "/etc/resolv.conf") : buf_(sb) {
    do_init(config);
  }

  /**
   * Returns a list of ipv4 addresses.
   */
  std::vector<Ipv4Addr> LookupV4(const std::string &domain) const;

  static int test_main(int argc, char **argv);

private:
  bool big_endian_;
  struct sockaddr server_addr_;
  mutable NIOBuf<SockFd> buf_;

  void do_init(const char *config);
};

#endif /* _DNS_H_ 1 */
