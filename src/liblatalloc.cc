// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2018 Bobby Powers

#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <chrono>
#include <mutex>

using namespace std;

// if we are being called from inside libdl, don't attempt to recurse,
// give libdl an allocation from a static buffer
#define _DL_BUF_LEN 1048576
static char _DL_BUF[_DL_BUF_LEN];
static size_t _DL_BUF_OFF = 0;
int _IN_DLSYM_COUNT = 0;
static bool _LATALLOC_INITIALIZED;

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
      _init();                                   \
      dlsym_push();                              \
      _##sym = (sym##_fn)dlsym(RTLD_NEXT, #sym); \
      dlsym_pop();                               \
    }                                            \
  }

#define ensure_loaded_alloc(sym, sz)             \
  {                                              \
    if (unlikely(_##sym == NULL)) {              \
      _init();                                   \
      if (is_in_dlsym())                         \
        return internal_alloc(sz);               \
      dlsym_push();                              \
      _##sym = (sym##_fn)dlsym(RTLD_NEXT, #sym); \
      dlsym_pop();                               \
    }                                            \
  }

// mutex protecting debug and __mesh_assert_fail to avoid concurrent
// use of static buffers by multiple threads
static inline mutex *getAssertMutex(void) {
  static char assertBuf[sizeof(mutex)];
  static mutex *assertMutex = new (assertBuf) mutex();

  return assertMutex;
}

// threadsafe printf-like debug statements safe for use in an
// allocator (it will never call into malloc or free to allocate
// memory)
static void _log(const char *fmt, ...) {
  constexpr size_t buf_len = 4096;
  static char buf[buf_len];
  lock_guard<mutex> lock(*getAssertMutex());

  va_list args;

  va_start(args, fmt);
  int len = vsnprintf(buf, buf_len - 1, fmt, args);
  va_end(args);

  buf[buf_len - 1] = 0;
  if (len > 0) {
    (void)write(STDERR_FILENO, buf, len);
    // ensure a trailing newline is written out
    if (buf[len - 1] != '\n')
      (void)write(STDERR_FILENO, "\n", 1);
  }
}

static inline void _init() {
  if (likely(_LATALLOC_INITIALIZED))
    return;

  _log("func\ttime\n");

  _LATALLOC_INITIALIZED = true;
}

class time_call {
private:
  const char *_type;
  chrono::high_resolution_clock::time_point _start;

public:
  explicit time_call(const char *type) : _type(type), _start(chrono::high_resolution_clock::now()) {
  }
  ~time_call() {
    const auto end = chrono::high_resolution_clock::now();
    auto nanos = chrono::duration_cast<chrono::nanoseconds>(end - _start).count();

    _log("%s\t%9d\n", _type, nanos);
  }
};

#ifdef __cplusplus
extern "C" {
#endif

void *malloc(size_t sz) {
  ensure_loaded_alloc(malloc, sz);

  time_call timer("malloc");
  return _malloc(sz);
}

void free(void *ptr) {
  ensure_loaded(free);
  if (unlikely(is_internal_alloc(ptr)))
    return;

  time_call timer("free");
  _free(ptr);
}

void cfree(void *ptr) {
  ensure_loaded(cfree);
  if (unlikely(is_internal_alloc(ptr)))
    return;

  time_call timer("cfree");
  _cfree(ptr);
}

void *calloc(size_t n, size_t sz) {
  ensure_loaded_alloc(calloc, n * sz);

  time_call timer("calloc");
  return _calloc(n, sz);
}

void *realloc(void *ptr, size_t sz) {
  ensure_loaded(realloc);
  if (unlikely(is_internal_alloc(ptr)))
    return NULL;

  time_call timer("realloc");
  return _realloc(ptr, sz);
}

void *memalign(size_t alignment, size_t sz) {
  ensure_loaded(memalign);

  time_call timer("memalign");
  return _memalign(alignment, sz);
}

int posix_memalign(void **ptr, size_t alignment, size_t sz) {
  ensure_loaded(posix_memalign);

  time_call timer("posix_memalign");
  return _posix_memalign(ptr, alignment, sz);
}

void *aligned_alloc(size_t alignment, size_t sz) {
  ensure_loaded(aligned_alloc);

  time_call timer("aligned_alloc");
  return _aligned_alloc(alignment, sz);
}

size_t malloc_usable_size(void *ptr) {
  ensure_loaded(malloc_usable_size);

  time_call timer("malloc_usable_size");
  return _malloc_usable_size(ptr);
}
#ifdef __cplusplus
}  // extern "C"
#endif
