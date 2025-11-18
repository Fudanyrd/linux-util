#ifndef _NIO_H_
#define _NIO_H_ 1

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include <stdexcept>

#define BUFSZ 8192

/*
 * The `NIOBuf` is made a template class because: I want it to support
 * SSL/TLS later, which requires using libssl's APIs.
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
};

template <typename _Context> NIOBuf<_Context>::~NIOBuf() {
  close_in();
  close_out();
  close_sock();
}

template <typename _Context> void NIOBuf<_Context>::flush(void) {
  if (socket_.write(in_buf_, this->in_off_) != in_off_) {
    throw std::runtime_error("connection error");
  }
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
    this->socket_.write(buf, len);
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
  ssize_t write(const void *buf, size_t size);
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
    this->socket_.read(buf, len);
  }
}

#endif /* _NIO_H_ 1 */
