#ifndef _DNS_H_
#define _DNS_H_ 1

#include <string>
#include <vector>

#include "nio.h"

struct Ipv4Addr {
  unsigned char addr_[4];
};

class Resolver {
public:
  Resolver(const char *config = "/etc/resolv.conf");

  /**
   * Returns a list of ipv4 addresses.
   */
  std::vector<Ipv4Addr> LookupV4(const std::string &domain) const;

  static int test_main(int argc, char **argv);

private:
  bool big_endian_;
  unsigned char name_server_[8];
  mutable NIOBuf<SockFd> buf_;
};

#endif /* _DNS_H_ 1 */
