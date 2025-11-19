
#include "nio.h"

size_t FdConsumer::consume(void *addr, size_t len) {
  if (!len) {
    /* Although unlikely, may help avoid one context switch. */
    return len;
  }

  size_t nw = 0;
  char *buf = (char *)addr;
  do {
    ssize_t res = write(this->wr_fd_, buf, len - nw);
    if (res < 0) {
      break;
    }

    /* Advance. */
    nw += res;
    buf += res;
  } while (nw < len);

  this->n_written_ += nw;
  return nw;
}
