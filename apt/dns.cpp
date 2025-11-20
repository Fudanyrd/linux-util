#include <arpa/inet.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/socket.h>
#include <sys/types.h>

#include "dns.h"
#include "http.h"
#include "nio.h"

void Ipv4Addr::fill(struct sockaddr *dst, unsigned int port) const {
  struct sockaddr_in *saddr = (sockaddr_in *)dst;
  saddr->sin_family = AF_INET;
  *(uint32_t *)&(saddr->sin_addr) = *(uint32_t *)this->addr_;
  saddr->sin_port = htons(port);
}

void Ipv6Addr::fill(struct sockaddr_in6 *dst, unsigned int port) const {
  dst->sin6_family = AF_INET6;
  memcpy(&dst->sin6_addr, this->addr_, sizeof(dst->sin6_addr));
  dst->sin6_port = htons(port);
}

/*
 * Reference: https://en.wikipedia.org/wiki/Domain_Name_System
 */
extern "C" {

#include <string.h>

struct dns_header {
  uint16_t transaction_id_;

  /* Recursion Desired, indicates if the client means a recursive query. */
  uint8_t rd_ : 1;
  /*
   * TrunCation, indicates that this message was truncated due to excessive
   * length.
   */
  uint8_t tc_ : 1;
  /* Authoritive answer */
  uint8_t aa_ : 1;
/* Standard query */
#define QUERY 0
/* Inverse query */
#define IQUERY 1
/* Server status request */
#define STATUS 2
  uint8_t opcode_ : 4;
  /* Query or reply; */
  uint8_t qr_ : 1;

#define NOERROR (0)
#define FORMERR (1)
#define SERVFAIL (2)
#define NXDOMAIN (3) /* No such domain */
  /* Response code */
  uint8_t rcode_ : 4;

  /* indicates that non-verified data is acceptable in a response */
  uint8_t cd_ : 1;
  /* indicates if the replying DNS server verified the data. */
  uint8_t ad_ : 1;
  /* reserved, set to 0 */
  uint8_t z_ : 1;
  /* recursion available */
  uint8_t ra_ : 1;

  uint16_t n_question_;
  uint16_t n_answer_;
  /* Number of authority resource records. */
  uint16_t n_auth_rr_;
  /* Number of addtional resource records. */
  uint16_t n_add_rr_;

} __attribute__((packed));

static uint8_t *dns_header_make(uint8_t *buf, uint16_t transId) {
  struct dns_header *dhdr = (struct dns_header *)buf;
  memset(dhdr, 0, sizeof(*dhdr));

  dhdr->transaction_id_ = htons(transId);
  dhdr->n_question_ = htons(1);
  dhdr->opcode_ = QUERY;

  dhdr->cd_ = (0);
  dhdr->rd_ = (1);

  return buf + sizeof(*dhdr);
}

struct dns_question {
#define A 0x0001    /* Host  Big Endian */
#define AAAA 0x001c /* IP6 Big Endian */

  uint16_t type_;

#define IN 0x0001

  uint16_t class_;
} __attribute__((packed));

struct dns_rr {
  uint16_t type_;
  uint16_t class_;
  uint32_t ttl_;
  uint16_t rd_len_;
} __attribute__((packed));

} /* C */

#include "dns.h"

void Resolver::do_init(const char *config) {
  int dummy = 1;
  big_endian_ = !(*(char *)(&dummy));
  static_assert(sizeof(struct dns_header) == 12);
  FILE *conf = fopen(config, "r");

  if (!conf) {
    perror("fopen");
    return;
  }

  char buf[512];
  int a, b, c, d;

  /* Ignore lines starting with '#', and parse the line
   * starting  with 'nameserver'. */

  while (fgets(buf, sizeof(buf), conf)) {
    if (buf[0] == '#') {
      continue;
    }

    if (sscanf(buf, "nameserver %d.%d.%d.%d", &a, &b, &c, &d) == 5) {
      break;
    }
  }

  make_ipv4(&this->server_addr_, a, b, c, d, 53);

  dbg.log("DNS server: use %d.%d.%d.%d\n", a, b, c, d);
}

static void push_question(const std::string &q, NIOBuf<SockFd> &buf,
                          uint16_t class_, uint16_t type_) {
  /* Split domain name by . */
  const size_t n = q.size();
  for (size_t i = 0; i < n;) {
    if (q[i] == '.') {
      ++i;
      continue;
    }
    size_t j = i + 1;
    while (j < n && q[j] != '.') {
      ++j;
    }
    buf.putch(static_cast<unsigned char>(j - i));
    for (size_t k = i; k < j; ++k) {
      buf.putch(q[k]);
    }
    i = j;
  }

  /* Terminate question. */
  buf.putch('\0');
  dns_question *dq = (dns_question *)buf.reserve_in(sizeof(dns_question));
  dq->class_ = htons(class_);
  dq->type_ = htons(type_);
}

template <typename _Ip_Addr /* implements addrlen,data */>
static void pop_rr(NIOBuf<SockFd> &buf, std::vector<_Ip_Addr> &addrs,
                   uint16_t class_, uint16_t type_) {
  int ch;

  /* Ignore Name. */
  ch = buf.getch();
  if (ch == EOF) {
    return;
  }
  ch = buf.getch();
  if (ch == EOF) {
    return;
  }

  uint16_t class_n = ntohs(class_);
  uint16_t type_n = ntohs(type_);

  dns_rr *rr;
  rr = (dns_rr *)buf.reserve_out(sizeof(*rr));
  if (rr->class_ != class_n || rr->type_ != type_n) {
    /* Invalid record. */
    buf.skip(ntohs(rr->rd_len_));
    return;
  }

  _Ip_Addr addr;
  unsigned char *dat = addr.data();
  size_t addrlen = addr.addrlen();
  if (rr->rd_len_ != htons((uint16_t)addrlen)) {
    /* Invalid record. */
    buf.skip(ntohs(rr->rd_len_));
    return;
  }

  size_t i;
  for (i = 0; i < addrlen; i++) {
    ch = buf.getch();
    if (ch == EOF) {
      break;
    }
    dat[i] = (unsigned char)ch;
  }

  if (i == addrlen)
    addrs.push_back(addr);
}

template <typename _Ip_Addr>
void Resolver::LookupGeneric(const std::string &domain,
                             std::vector<_Ip_Addr> &result, uint16_t class_,
                             uint16_t type_) const {
  NIOBuf<SockFd> &buf = this->buf_;

  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    perror("socket");
    return;
  }

  int cfd = connect(fd, &this->server_addr_, sizeof(server_addr_));
  if (cfd < 0) {
    perror("connect");
    close(fd);
    return;
  }

  SockFd sfd(fd);

  auto &ret = result;
  try {
    /* Send request. */
    buf.reset(sfd);
    unsigned char *header = buf.reserve_in(sizeof(struct dns_header));
    dns_header_make(header, 0x5);
    push_question(domain, buf, class_, type_);
    buf.flush();

    /* Parse resonse. */
    header = buf.reserve_out(sizeof(struct dns_header));
    unsigned int answers = 0;
    struct dns_header *dhdr = (struct dns_header *)header;
    if (dhdr->rcode_ != 0) {
      dbg.log("dnsserver returned %x\n", (unsigned)dhdr->rcode_);
      throw std::runtime_error("dnsserver returned error");
    }
    answers += ntohs(dhdr->n_answer_);
    answers += ntohs(dhdr->n_auth_rr_);
    answers += ntohs(dhdr->n_add_rr_);

    /* Skip the query name. */
    int ch = buf.getch();
    while (ch != 0 && ch != EOF) {
      ch = buf.getch();
    }
    for (size_t _i = 0; _i < sizeof(dns_question); _i++) {
      ch = buf.getch();
      if (ch == EOF) {
        break;
      }
    }

    if (ch != EOF) {
      for (unsigned int it = 0; it < answers; it++) {
        pop_rr(buf, ret, class_, type_);
      }
    }
  } catch (std::runtime_error &ex) {
    dbg.log("dnsclient: error: %s\n", ex.what());
  }

  buf.close_sock();
}

std::vector<Ipv4Addr> Resolver::LookupV4(const std::string &domain) const {
  std::vector<Ipv4Addr> ret;
  this->LookupGeneric(domain, ret, IN, A);
  return ret;
}
std::vector<Ipv6Addr> Resolver::LookupV6(const std::string &domain) const {
  std::vector<Ipv6Addr> ret;
  this->LookupGeneric(domain, ret, IN, AAAA);
  return ret;
}

int Resolver::test_main(int argc, char **argv) {
  dbg.on();
  SharedBuf sb;
  Resolver resolv(sb, "/etc/resolv.conf");
  const char *host = argv[1];
  if (!host) {
    /* Use a default. */
    host = "archive.ubuntu.com";
  }
  auto res = resolv.LookupV4(host);

  for (const auto &addr : res) {
    const unsigned char *b = addr.addr_;
    printf("Address: ");
    printf("%d.%d.%d.%d\n", b[0], b[1], b[2], b[3]);
  }

  for (const auto &addr : resolv.LookupV6(host)) {
    printf("Address: ");
    addr.print(stdout);
    putchar('\n');
  }
  return 0;
}
