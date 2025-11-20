#ifndef _BUF_H_
#define _BUF_H_ 1

#ifndef BUFSZ
#define BUFSZ 8192
#endif

#include <sys/mman.h>
#include <sys/types.h>

/**
 * A manager that holds two large memory buffer. It should
 * have very long lifetime. It may be shared by different NIOBuf,
 * but not two threads.
 */
struct SharedBuf {
  void *in_buf_;
  void *out_buf_;

  SharedBuf(void) {
    in_buf_ = mmap(NULL, BUFSZ, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON,
                   -1, 0);
    out_buf_ = mmap(NULL, BUFSZ, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON,
                    -1, 0);
  }

  ~SharedBuf() {
    munmap(in_buf_, BUFSZ);
    munmap(out_buf_, BUFSZ);
  }

private:
};

#endif /* _BUF_H_ 1 */
