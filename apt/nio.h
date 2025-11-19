#ifndef _NIO_H_
#define _NIO_H_ 1

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <errno.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include <stdexcept>

#define BUFSZ 8192

/*
 * The `NIOBuf` is made a template class because: we want it to support
 * SSL/TLS later, which requires using libssl's APIs.
 *
 * The buffer actually keeps to buffers, each of size `BUFSZ`: one for reading,
 * the other for writing. It is recommended to keep the NIOBuf's lifetime as
 * long as possible, for you can always re-initialize it with its `reset`
 * method.
 *
 * The NIOBuf will safely deal with short count when reading/writing socket.
 */

template <typename _Context /* implements close,valid,read,write,copyable */>
struct NIOBuf {
  NIOBuf(void) : socket_() {
    in_buf_ = (unsigned char *)mmap(NULL, BUFSZ, PROT_READ | PROT_WRITE,
                                    MAP_PRIVATE | MAP_ANON, -1, 0);
    out_buf_ = (unsigned char *)mmap(NULL, BUFSZ, PROT_READ | PROT_WRITE,
                                     MAP_PRIVATE | MAP_ANON, -1, 0);
  }
  ~NIOBuf();

  /* Call this when starting a new transaction. */
  void reset(_Context &ctx) {
    in_off_ = out_off_ = out_end_ = 0;
    socket_ = ctx;
  }

  void close_in(void) {
    if (in_buf_) {
      munmap(in_buf_, BUFSZ);
    }
    in_buf_ = nullptr;
  }
  void close_out(void) {
    if (out_buf_) {
      munmap(out_buf_, BUFSZ);
    }
    out_buf_ = nullptr;
  }

  void close_sock(void) {
    if (socket_.valid()) {
      socket_.close();
    }
  }

  /**
   * Reserve `size` bytes in the input buffer of socket.
   * @return the address of the reserved buffer.
   */
  unsigned char *reserve_in(size_t size) {
    APT_ASSERT(size < BUFSZ);
    if (size + in_off_ > BUFSZ) {
      flush();
    }
    auto *ret = in_buf_ + in_off_;
    in_off_ += size;
    return ret;
  }
  void putch(unsigned char ch) {
    reserve_in(1);
    in_buf_[in_off_ - 1] = ch;
  }
  void write(const void *buf, size_t len);
  void flush(void);

  unsigned char *reserve_out(size_t);

  int getch(void) {
    auto *pt = this->reserve_out(1);
    if (!pt) {
      return EOF;
    }
    return *pt;
  }
  /** Discard some bytes in sock output. */
  void skip(size_t nb) { this->reserve_out(nb); }
  void read(void *buf, size_t len);
  void fill(void);

private:
  _Context socket_;
  unsigned char *in_buf_;
  unsigned int in_off_{0};
  unsigned char *out_buf_;
  unsigned int out_off_{0};
  unsigned int out_end_{0};

  /**
   * Read at least `minb` bytes from socket and at most
   * `maxb` bytes.
   * @return number of bytes read.
   * @throw std::runtime_error if EOF before `minb` bytes.
   */
  size_t short_read(void *buf, size_t minb, size_t maxb);

  /**
   * Deal with short value when writing to socket. If no IO error,
   * this always writes `len` bytes to socket.
   * @throw std::runtime_error if `socket_.write` fails.
   */
  void short_write(const void *buf, size_t len);
};

template <typename _Context>
size_t NIOBuf<_Context>::short_read(void *buf, size_t minb, size_t maxb) {
  APT_ASSERT(minb <= maxb);
  size_t nr = 0;
  while (nr < minb) {
    ssize_t res = socket_.read(buf, maxb);
    if (res < 0) {
      dbg.log("socket: %s\n", strerror(errno));
      throw std::runtime_error("socket read error");
    }

    if (res == 0) {
      break;
    }

    nr += res;
    buf += res;
    APT_ASSERT(maxb >= (size_t)res);
    maxb -= res;
  }

  if (nr < minb) {
    throw std::runtime_error("ealy EOF when reading socket");
  }
  return nr;
}

template <typename _Context>
void NIOBuf<_Context>::short_write(const void *_buf, size_t len) {
  const char *buf = (const char *)_buf;

  while (len) {
    ssize_t nw = socket_.write((const void *)buf, len);
    if (nw <= 0) {
      dbg.log("socket: %s\n", strerror(errno));
      throw std::runtime_error("socket write error");
    }

    len -= nw;
    buf += nw;
  }
}

template <typename _Context> NIOBuf<_Context>::~NIOBuf() {
  close_in();
  close_out();
  close_sock();
}

template <typename _Context> void NIOBuf<_Context>::flush(void) {
  this->short_write(in_buf_, this->in_off_);
  this->in_off_ = 0;
}
template <typename _Context> void NIOBuf<_Context>::fill(void) {
  memmove(out_buf_, out_buf_ + out_off_, out_end_ - out_off_);
  out_end_ -= out_off_;
  out_off_ = 0;
  ssize_t n = socket_.read(out_buf_ + (out_end_), BUFSZ - (out_end_));
  if (n < 0) {
    throw std::runtime_error("connection error");
  }
  out_end_ += n;
}

template <typename _Context>
void NIOBuf<_Context>::write(const void *buf, size_t len) {
  if (len) {
    if (in_off_ == BUFSZ) {
      this->flush();
    }

    /* Write a small batch of data. */
    size_t nw = std::min((size_t)BUFSZ - in_off_, len);
    memcpy(in_buf_ + in_off_, buf, nw);
    in_off_ += nw;
    this->flush();
    buf += nw;
    len -= nw;
  }

  if (len) {
    /* Directly write to socket. */
    this->short_write(buf, len);
  }
}

template <typename _Ctx> unsigned char *NIOBuf<_Ctx>::reserve_out(size_t size) {
  APT_ASSERT(size < BUFSZ);
  if (out_off_ + size > out_end_) {
    fill();
  }
  if (out_off_ + size > out_end_) {
    /* EOF */
    return nullptr;
  }
  auto *ret = out_buf_ + out_off_;
  out_off_ += size;
  return ret;
}

struct SockFd {
  SockFd(int fd = -1) : fd_(fd) {}
  ~SockFd() = default /* NIOBuf will close this. */;

  bool valid(void) const { return fd_ != -1; }
  void close(void) {
    if (valid()) {
      ::close(fd_);
      fd_ = -1;
    }
  }

  ssize_t read(void *buf, size_t size) { return ::read(fd_, buf, size); }

  /* wrapper of write syscall, deal with short value. */
  ssize_t write(const void *buf, size_t size) {
    return ::write(fd_, buf, size);
  }
  int fd_;
};

template <typename _Context>
void NIOBuf<_Context>::read(void *buf, size_t len) {
  if (out_off_ < out_end_ && len) {
    size_t nr = std::min(len, (size_t)out_end_ - out_off_);
    memcpy(buf, this->out_buf_ + out_off_, nr);
    out_end_ = out_off_ = 0;
    len -= nr;
    buf += nr;
  }

  if (len) {
    this->short_read(buf, len, len);
  }
}

#include <openssl/err.h>
#include <openssl/ssl.h>

struct SSLSockFd {
  SSLSockFd() = default;

  void close() {
    if (ssl_) {
      SSL_shutdown(ssl_);
      SSL_free(ssl_);
      ssl_ = nullptr;
    }
  }

  bool valid() const { return ssl_; }

  ssize_t read(void *buf, size_t len) {
    ssize_t nr = SSL_read(ssl_, buf, (int)len);
    return nr;
  }

  ssize_t write(const void *buf, size_t len) {
    ssize_t nw = SSL_write(ssl_, buf, (int)len);
    return nw;
  }

  SSL *ssl_{nullptr};
};

#endif /* _NIO_H_ 1 */
