#ifndef _DEBUG_H_
#define _DEBUG_H_ 1

/* -include debug.h */

#include <cassert>
#include <cstdarg>
#include <cstdio>

#define APT_ASSERT(cond) assert(cond)

struct Debug {
public:
  Debug() = default;

  void on() { stat_ = true; }
  void off() { stat_ = false; }

  void log(const char *fmt, ...) {
    if (stat_) {
      va_list vl;
      va_start(vl, fmt);
      vfprintf(stderr, fmt, vl);
      va_end(vl);
    }
  }

private:
  bool stat_{false};
};

extern Debug dbg;

#endif /* _DEBUG_H_ */
