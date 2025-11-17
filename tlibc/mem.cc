#include "malloc.h"

#include <stdio.h>
#include <stdlib.h>

#define WEAK __attribute__((weak))

WEAK void *operator new(unsigned long size) { return cpp_alloc(size); }

WEAK void *operator new[](unsigned long size) { return cpp_alloc(size); }

WEAK void operator delete(void *ptr, unsigned long size) {
  cpp_free(ptr, size);
}

WEAK void operator delete[](void *ptr, unsigned long size) {
  cpp_free(ptr, size);
}
