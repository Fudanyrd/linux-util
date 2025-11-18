
#include "nio.h"

ssize_t SockFd::write(const void *buf, size_t size) {
  ssize_t ret = 0;
  while (size > 0) {
    ssize_t nw = ::write(fd_, buf, size);
    if (nw < 0) {
      if (ret == 0) {
        ret = nw;
      }
      break; /* Error */
    }
    ret += nw;
    size -= nw;
    buf += nw;
  }
  return ret;
}
