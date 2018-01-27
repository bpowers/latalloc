// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2018 Bobby Powers

#define _GNU_SOURCE
#include <dlfcn.h>
#undef _GNU_SOURCE

#include <stddef.h>
#include <string.h>
#include <unistd.h>

// if we are being called from inside libdl, don't attempt to recurse,
// give libdl an allocation from a static buffer
#define _DL_BUF_LEN 1048576
static char _DL_BUF[_DL_BUF_LEN];
static size_t _DL_BUF_OFF = 0;
int _IN_DLSYM_COUNT = 0;

static inline void *internal_alloc(size_t sz) {
  if (_DL_BUF_OFF + sz > _DL_BUF_LEN) {
    (void)write(2, "internal_alloc exhausted\n", strlen("internal_alloc exhausted\n"));
    _exit(1);
  }

  // bump pointer
  size_t off = _DL_BUF_OFF;
  _DL_BUF_OFF += sz;

  return (void *)&_DL_BUF[off];
}

static inline int is_internal_alloc(void *ptr) {
  char *cptr = (char *)ptr;
  return cptr >= _DL_BUF && cptr < (_DL_BUF + _DL_BUF_LEN);
}

static inline int is_in_dlsym() {
  return _IN_DLSYM_COUNT > 0;
}

static inline void dlsym_push() {
  _IN_DLSYM_COUNT++;
}

static inline void dlsym_pop() {
  _IN_DLSYM_COUNT--;
}

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
  static fname##_fn _##fname
#define MDEF2(type, fname, arg1, arg2)    \
  typedef type (*fname##_fn)(arg1, arg2); \
  static fname##_fn _##fname
#define MDEF3(type, fname, arg1, arg2, arg3)    \
  typedef type (*fname##_fn)(arg1, arg2, arg3); \
  static fname##_fn _##fname

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

#define ensure_loaded(sym)                       \
  {                                              \
    if (unlikely(_##sym == NULL)) {              \
      dlsym_push();                              \
      _##sym = (sym##_fn)dlsym(RTLD_NEXT, #sym); \
      dlsym_pop();                               \
    }                                            \
  }

#define ensure_loaded_alloc(sym, sz)             \
  {                                              \
    if (unlikely(_##sym == NULL)) {              \
      if (is_in_dlsym())                         \
        return internal_alloc(sz);               \
      dlsym_push();                              \
      _##sym = (sym##_fn)dlsym(RTLD_NEXT, #sym); \
      dlsym_pop();                               \
    }                                            \
  }

#ifdef __cplusplus
extern "C" {
#endif

void *malloc(size_t sz) {
  ensure_loaded_alloc(malloc, sz);

  return _malloc(sz);
}

void free(void *ptr) {
  ensure_loaded(free);
  if (unlikely(is_internal_alloc(ptr)))
    return;

  _free(ptr);
}

void cfree(void *ptr) {
  ensure_loaded(cfree);
  if (unlikely(is_internal_alloc(ptr)))
    return;

  _cfree(ptr);
}

void *calloc(size_t n, size_t sz) {
  ensure_loaded_alloc(calloc, n * sz);

  return _calloc(n, sz);
}

void *realloc(void *ptr, size_t sz) {
  ensure_loaded(realloc);
  if (unlikely(is_internal_alloc(ptr)))
    return NULL;

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
#ifdef __cplusplus
}  // extern "C"
#endif
