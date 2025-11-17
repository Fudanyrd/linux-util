#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>

typedef uint16_t u16;
typedef uint32_t u32;

static size_t nalloc;

#define PGSIZE 4096

#ifdef __DEBUG__
#include <assert.h>
#include <stdio.h>
#define DBG(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)

#define MyAssert(cond) assert(cond)

#else
#define DBG(fmt, ...)                                                          \
  do {                                                                         \
  } while (0)
#define MyAssert(cond)                                                         \
  do {                                                                         \
  } while (0)
#endif

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void *alloc_page(size_t pages) {
  nalloc += pages * PGSIZE;
  void *ret = mmap(NULL, pages * PGSIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANON, -1, 0);
  DBG("alloc %ld pages = %p", pages, ret);
  if (ret == MAP_FAILED) {
    return NULL;
  }
  return ret;
}

static void free_page(void *pt, size_t pages) {
  DBG("free %ld pages = %p", pages, pt);
  munmap(pt, pages * PGSIZE);
}

#define MAGIC 0x55aa

struct arena;

struct block {
  struct block *next;
};

struct freelist {
  struct block *first;
  struct block *last;
};

static void freelist_init(struct freelist *fl) { fl->first = fl->last = NULL; }
static void freelist_free(struct freelist *fl, struct block *blk) {
  blk->next = fl->first;
  if (!fl->first) {
    /* empty list, set `last` too. */
    MyAssert(!fl->last);
    fl->last = blk;
  }
  fl->first = blk;
}

__attribute__((unused)) static struct block *
freelist_alloc(struct freelist *fl) {
  struct block *ret = fl->first;
  if (!ret) {
    return ret;
  }
  if (fl->last == ret) {
    /* only one block, set `last` to NULL. */
    MyAssert(!ret->next);
    fl->last = NULL;
  }
  fl->first = ret->next;
  return ret;
}

struct page_header {
  struct arena *are;
  union {
    /* For small allocations(less than a page. )*/
    struct freelist fl;
    size_t alloc_size;
  };
  u16 avail;
  u16 magic;
  u16 total;
};

struct arena {
  struct block *freelist;
  struct page_header *idlepage;
  /* Initialize these. */
  u32 block_size;
};

static void page_header_init(struct arena *are, void *page) {
  MyAssert(((size_t)page) % PGSIZE == 0);
  size_t bsize = are->block_size;
  size_t offset = 0;
  while (offset < sizeof(struct page_header)) {
    offset += bsize;
  }

  struct page_header *ph = page;
  ph->are = are;
  ph->magic = MAGIC;
  ph->avail = 0;
  freelist_init(&ph->fl);

  while (offset < PGSIZE) {
    struct block *b = (page + offset);
    b->next = NULL;
    freelist_free(&ph->fl, b);
    offset += bsize;
    ph->avail++;
  }
  MyAssert(ph->avail > 1 /* If only one blocks, too wasteful. */);
  ph->total = ph->avail;
}

static void *arena_alloc(struct arena *are) {
  if (!are->freelist) {
    if (!are->idlepage) {
      void *page = alloc_page(1);
      if (!page) {
        /* Impossible to continue. */
        return NULL;
      }
      page_header_init(are, page);
      are->idlepage = page;
    }

    /* seize blocks from this idle page(at most one at any given time). */
    struct page_header *ph = are->idlepage;
    MyAssert(((size_t)ph) % PGSIZE == 0);
    MyAssert(ph->magic == MAGIC);
    MyAssert(ph->are == are);
    MyAssert(ph->avail > 0);
    ph->avail = 0;
    ph->fl.last->next = are->freelist;
    are->freelist = ph->fl.first;
    freelist_init(&ph->fl);
    are->idlepage = NULL;
  }

  struct block *ret = are->freelist;
  are->freelist = ret->next;
  return ret;
}

static void arena_add_idle(struct arena *are, struct page_header *ph) {
  MyAssert(((size_t)ph) % PGSIZE == 0);
  MyAssert(ph->magic == MAGIC);
  MyAssert(ph->are == are);
  MyAssert(ph->avail > 0);

  if (are->idlepage) {
    free_page(ph, 1);
  } else {
    are->idlepage = ph;
  }
}

static struct arena caches[] = {
    {
        .block_size = 16,
    },
    {
        .block_size = 32,
    },
    {
        .block_size = 64,
    },
    {
        .block_size = 128,
    },
    {
        .block_size = 256,
    },
    {
        .block_size = 512,
    },
    {
        .block_size = 1024,
    },
};

#define ncaches ((sizeof(caches)) / sizeof(caches[0]))

__attribute__((destructor)) static void malloc_fini(void) {
  for (int i = 0; i < ncaches; i++) {
    struct arena *are = &caches[i];
    if (are->idlepage) {
      free_page(are->idlepage, 1);
    }
  }
}

void *mymalloc(size_t size) {
  if (size > 1024) {
    /* This is a large allocation. */
    size += sizeof(struct page_header);
    size_t pages = (size + PGSIZE - 1) / PGSIZE;
    void *ret = alloc_page(pages);
    if (!ret) {
      return NULL;
    }

    struct page_header *ph = ret;
    ph->are = NULL;
    ph->alloc_size = pages * PGSIZE;
    ph->magic = MAGIC;

    return ret + sizeof(struct page_header);
  }
  struct arena *are = caches;
  while (are->block_size < size) {
    are++;
    MyAssert(are < caches + ncaches);
  }
  return arena_alloc(are);
}

void *cpp_alloc(size_t size) {
  DBG("cpp_alloc(%ld)", size);
  if (size > 1024) {
    size += PGSIZE - 1;
    size /= PGSIZE;
    return alloc_page(size);
  }

  return mymalloc(size);
}

static inline void *block_to_page(void *block) {
  uintptr_t addr = (uintptr_t)block;
  addr -= (addr % PGSIZE);
  return (void *)addr;
}

void myfree(void *pt) {
  if (pt == NULL) {
    return;
  }
  struct page_header *ph = block_to_page(pt);
  if (ph->magic != MAGIC) {
    (void)write(2, "Invalid pointer passed to free.\n", 32);
    abort();
  }

  if (!ph->are) {
    /* This is a large allocation. */
    free_page(ph, ph->alloc_size / PGSIZE);
    return;
  }

  ph->avail++;
  freelist_free(&ph->fl, pt);
  if (ph->avail == ph->total) {
    /* keep this as a idle page, or free the page. */
    arena_add_idle(ph->are, ph);
  }
}

void cpp_free(void *pt, size_t size) {
  DBG("cppfree(%p, %ld)", pt, size);
  if (size > 1024) {
    MyAssert(((uintptr_t)pt) % PGSIZE == 0);
    if (pt)
      free_page(pt, (size + PGSIZE - 1) / PGSIZE);
  } else {
    myfree(pt);
  }
}

void *myrealloc(void *pt, size_t size) {
  if (pt == NULL) {
    return mymalloc(size);
  }
  struct page_header *ph = block_to_page(pt);
  MyAssert(ph->magic == MAGIC);
  if (!ph->are) {
    /* This is a large allocation. */
    if (size <= ph->alloc_size - sizeof(struct page_header)) {
      return pt;
    }
    void *newpt = mymalloc(size);
    if (!newpt) {
      return NULL;
    }
    memcpy(newpt, pt, ph->alloc_size - sizeof(struct page_header));
    myfree(pt);
    return newpt;
  }

  struct arena *are = ph->are;
  if (size <= are->block_size) {
    return pt;
  }
  void *newpt = mymalloc(size);
  if (!newpt) {
    return NULL;
  }
  memcpy(newpt, pt, are->block_size);
  myfree(pt);
  return newpt;
}

#ifdef __HOOK__
void *malloc(size_t size) { return mymalloc(size); }
void free(void *pt) { return myfree(pt); }

void *realloc(void *pt, size_t size) { return myrealloc(pt, size); }

void *calloc(size_t nmemb, size_t size) {
  size_t total = nmemb * size;
  void *ret = mymalloc(total);
  if (ret) {
    memset(ret, 0, total);
  }
  return ret;
}

void *reallocarray(void *pt, size_t nmemb, size_t size) {
  size_t total = nmemb * size;
  return myrealloc(pt, total);
}
#endif
