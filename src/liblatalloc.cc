// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2018 Bobby Powers

#include <dlfcn.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define ATTRIBUTE_ALWAYS_INLINE __attribute__((always_inline))
#define ATTRIBUTE_HIDDEN __attribute__((visibility("hidden")))
#define ATTRIBUTE_ALIGNED(s) __attribute__((aligned(s)))
#define CACHELINE 64

#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName &);              \
  void operator=(const TypeName &)

// creates e.g. "malloc_fn" typedef and _malloc static var
#define MDEF1(type, fname, arg1)    \
  typedef type (*fname##_fn)(arg1); \
  static fname##_fn _##fname;
#define MDEF2(type, fname, arg1, arg2)    \
  typedef type (*fname##_fn)(arg1, arg2); \
  static fname##_fn _##fname;
#define MDEF3(type, fname, arg1, arg2, arg3)    \
  typedef type (*fname##_fn)(arg1, arg2, arg3); \
  static fname##_fn _##fname;

MDEF1(void *, malloc, size_t);
MDEF1(void, free, void *);
MDEF1(void, cfree, void *);
MDEF2(void *, calloc, size_t, size_t);
MDEF2(void *, realloc, void *, size_t);
MDEF2(void *, memalign, size_t, size_t);
MDEF3(int, posix_memalign, void **, size_t, size_t);
MDEF2(void *, aligned_alloc, size_t, size_t);
MDEF1(size_t, malloc_usable_size, void *);

#undef MDEF1
#undef MDEF2
#undef MDEF3

#define ensure_loaded(sym)                                         \
  {                                                                \
    if (unlikely(_##sym == nullptr)) {                             \
      _##sym = reinterpret_cast<sym##_fn>(dlsym(RTLD_NEXT, #sym)); \
      write(2, #sym "\n", strlen(#sym "\n"));                      \
    }                                                              \
  }

extern "C" {

void *malloc(size_t sz) {
  ensure_loaded(malloc);

  return _malloc(sz);
}

void free(void *ptr) {
  ensure_loaded(free);

  _free(ptr);
}

void cfree(void *ptr) {
  ensure_loaded(cfree);

  _cfree(ptr);
}

void *calloc(size_t n, size_t sz) {
  ensure_loaded(calloc);

  return _calloc(n, sz);
}

void *realloc(void *ptr, size_t sz) {
  ensure_loaded(realloc);

  return _realloc(ptr, sz);
}

void *memalign(size_t alignment, size_t sz) {
  ensure_loaded(memalign);

  return _memalign(alignment, sz);
}

int posix_memalign(void **ptr, size_t alignment, size_t sz) {
  ensure_loaded(posix_memalign);

  return _posix_memalign(ptr, alignment, sz);
}

void *aligned_alloc(size_t alignment, size_t sz) {
  ensure_loaded(aligned_alloc);

  return _aligned_alloc(alignment, sz);
}

size_t malloc_usable_size(void *ptr) {
  ensure_loaded(malloc_usable_size);

  return _malloc_usable_size(ptr);
}
}  // extern "C"
