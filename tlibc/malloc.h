
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif /* C++ */
extern void *mymalloc(size_t);
extern void myfree(void *);

/**
 * Allocator for C++'s new and delete operator. The `delete` operator will
 * book-keep the size of allocated memory block, so the allocator can be
 * simplified(just a little)
 *
 * TODO: cpp_alloc and cpp_free is NOT thread safe.
 */

extern void *cpp_alloc(size_t);
extern void *cpp_free(void *, size_t);
#ifdef __cplusplus
}
#endif /* C++ */

#ifdef __cplusplus
inline void *operator new(size_t, void *__p) { return __p; }
inline void *operator new[](size_t, void *__p) { return __p; }
#endif /* C++ */
