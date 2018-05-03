// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2018 Bobby Powers

#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <chrono>
#include <mutex>

#include <hdr_histogram.h>

using namespace std;

// if we are being called from inside libdl, don't attempt to recurse,
// give libdl an allocation from a static buffer
constexpr size_t _DL_BUF_LEN = 1048576;
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

// mutex protecting debug and __latalloc_assert_fail to avoid concurrent
// use of static buffers by multiple threads
static inline mutex *getAssertMutex(void) {
  static char assertBuf[sizeof(mutex)];
  static mutex *assertMutex = new (assertBuf) mutex();

  return assertMutex;
}

#define hard_assert(expr)                \
  ((likely(expr)) ? static_cast<void>(0) \
                  : internal::__latalloc_assert_fail(#expr, __FILE__, __PRETTY_FUNCTION__, __LINE__, ""))

namespace internal {
// out-of-line function called to report an error and exit the program
// when an assertion failed.
void __latalloc_assert_fail(const char *assertion, const char *file, const char *func, int line, const char *fmt, ...) {
  constexpr size_t buf_len = 4096;
  constexpr size_t usr_len = 512;
  static char buf[buf_len];
  static char usr[usr_len];
  std::lock_guard<std::mutex> lock(*getAssertMutex());

  va_list args;

  va_start(args, fmt);
  (void)vsnprintf(usr, usr_len - 1, fmt, args);
  va_end(args);

  usr[usr_len - 1] = 0;

  int len = snprintf(buf, buf_len - 1, "%s:%d:%s: ASSERTION '%s' FAILED: %s\n", file, line, func, assertion, usr);
  if (len > 0) {
    auto _ __attribute__((unused)) = write(STDERR_FILENO, buf, len);
  }

  // void *array[32];
  // size_t size = backtrace(array, 10);
  // backtrace_symbols_fd(array, size, STDERR_FILENO);

  abort();
}
};  // namespace internal

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

static constexpr int32_t buckets_needed_to_cover_value(int64_t value, int32_t sub_bucket_count,
                                                       int32_t unit_magnitude) {
  int64_t smallest_untrackable_value = ((int64_t)sub_bucket_count) << unit_magnitude;
  int32_t buckets_needed = 1;
  while (smallest_untrackable_value <= value) {
    if (smallest_untrackable_value > INT64_MAX / 2) {
      return buckets_needed + 1;
    }
    smallest_untrackable_value <<= 1;
    buckets_needed++;
  }

  return buckets_needed;
}

static constexpr int32_t ceil(double num) {
  return (static_cast<double>(static_cast<int32_t>(num)) == num) ? static_cast<int32_t>(num)
                                                                 : static_cast<int32_t>(num) + ((num > 0) ? 1 : 0);
}

static constexpr int32_t floor(double num) {
  return static_cast<int32_t>(num);
}

static int constexpr staticlog(int v) {
  return ((v == 1) ? 0 : (v == 2) ? 1 : (v > 1) ? staticlog(v / 2) + 1 : 0);
}

static constexpr int64_t pow(int64_t base, int64_t exp) {
  int64_t result = 1;
  while (exp) {
    result *= base;
    exp--;
  }
  return result;
}

static constexpr size_t histogramSize(int64_t lowest_trackable_value, int64_t highest_trackable_value,
                                      int significant_figures) {
  int64_t largest_value_with_single_unit_resolution = 2 * pow(10, significant_figures);
  int32_t sub_bucket_count_magnitude =
      (int32_t)ceil(staticlog((double)largest_value_with_single_unit_resolution) / staticlog(2));
  int32_t sub_bucket_half_count_magnitude = ((sub_bucket_count_magnitude > 1) ? sub_bucket_count_magnitude : 1) - 1;

  int32_t unit_magnitude = (int32_t)floor(staticlog((double)lowest_trackable_value) / staticlog(2));

  int32_t sub_bucket_count = (int32_t)pow(2, (sub_bucket_half_count_magnitude + 1));
  int32_t sub_bucket_half_count = sub_bucket_count / 2;
  int64_t sub_bucket_mask = ((int64_t)sub_bucket_count - 1) << unit_magnitude;

  // determine exponent range needed to support the trackable value with no overflow:
  int32_t bucket_count =
      buckets_needed_to_cover_value(highest_trackable_value, sub_bucket_count, (int32_t)unit_magnitude);
  const int32_t counts_len = (bucket_count + 1) * (sub_bucket_count / 2);

  return sizeof(struct hdr_histogram) + counts_len * sizeof(int64_t);
}

constexpr int64_t minNanosecs = 5;
constexpr int64_t maxNanosecs = 50000000;  // 50 ms
constexpr int sigFigs = 3;

static char *malloc_histogram_buf[histogramSize(minNanosecs, maxNanosecs, sigFigs)];
static char *free_histogram_buf[histogramSize(minNanosecs, maxNanosecs, sigFigs)];
static char *cfree_histogram_buf[histogramSize(minNanosecs, maxNanosecs, sigFigs)];
static char *calloc_histogram_buf[histogramSize(minNanosecs, maxNanosecs, sigFigs)];
static char *realloc_histogram_buf[histogramSize(minNanosecs, maxNanosecs, sigFigs)];
static char *memalign_histogram_buf[histogramSize(minNanosecs, maxNanosecs, sigFigs)];
static char *posix_memalign_histogram_buf[histogramSize(minNanosecs, maxNanosecs, sigFigs)];
static char *aligned_alloc_histogram_buf[histogramSize(minNanosecs, maxNanosecs, sigFigs)];
static char *malloc_usable_size_histogram_buf[histogramSize(minNanosecs, maxNanosecs, sigFigs)];

static struct hdr_histogram *malloc_histogram = reinterpret_cast<struct hdr_histogram *>(&malloc_histogram_buf);
static struct hdr_histogram *free_histogram = reinterpret_cast<struct hdr_histogram *>(&free_histogram_buf);
static struct hdr_histogram *cfree_histogram = reinterpret_cast<struct hdr_histogram *>(&cfree_histogram_buf);
static struct hdr_histogram *calloc_histogram = reinterpret_cast<struct hdr_histogram *>(&calloc_histogram_buf);
static struct hdr_histogram *realloc_histogram = reinterpret_cast<struct hdr_histogram *>(&realloc_histogram_buf);
static struct hdr_histogram *memalign_histogram = reinterpret_cast<struct hdr_histogram *>(&memalign_histogram_buf);
static struct hdr_histogram *posix_memalign_histogram =
    reinterpret_cast<struct hdr_histogram *>(&posix_memalign_histogram_buf);
static struct hdr_histogram *aligned_alloc_histogram =
    reinterpret_cast<struct hdr_histogram *>(&aligned_alloc_histogram_buf);
static struct hdr_histogram *malloc_usable_size_histogram =
    reinterpret_cast<struct hdr_histogram *>(&malloc_usable_size_histogram_buf);

static inline void _init() {
  if (likely(_LATALLOC_INITIALIZED))
    return;

  struct hdr_histogram_bucket_config cfg;
  int result = hdr_calculate_bucket_config(minNanosecs, maxNanosecs, sigFigs, &cfg);
  hard_assert(result == 0);

  hdr_init_preallocated(malloc_histogram, &cfg);
  hard_assert(result == 0);
  hdr_init_preallocated(free_histogram, &cfg);
  hard_assert(result == 0);
  hdr_init_preallocated(cfree_histogram, &cfg);
  hard_assert(result == 0);
  hdr_init_preallocated(calloc_histogram, &cfg);
  hard_assert(result == 0);
  hdr_init_preallocated(realloc_histogram, &cfg);
  hard_assert(result == 0);
  hdr_init_preallocated(memalign_histogram, &cfg);
  hard_assert(result == 0);
  hdr_init_preallocated(posix_memalign_histogram, &cfg);
  hard_assert(result == 0);
  hdr_init_preallocated(aligned_alloc_histogram, &cfg);
  hard_assert(result == 0);
  hdr_init_preallocated(malloc_usable_size_histogram, &cfg);
  hard_assert(result == 0);

  _LATALLOC_INITIALIZED = true;
}

class time_call {
private:
  struct hdr_histogram *_type;
  chrono::high_resolution_clock::time_point _start;

public:
  explicit time_call(struct hdr_histogram *type) : _type(type), _start(chrono::high_resolution_clock::now()) {
  }
  ~time_call() {
    const auto end = chrono::high_resolution_clock::now();
    auto nanos = chrono::duration_cast<chrono::nanoseconds>(end - _start).count();

    hdr_record_value(_type, nanos);
  }
};

#ifdef __cplusplus
extern "C" {
#endif

void *malloc(size_t sz) {
  ensure_loaded_alloc(malloc, sz);

  time_call timer(malloc_histogram);
  return _malloc(sz);
}

void free(void *ptr) {
  ensure_loaded(free);
  if (unlikely(is_internal_alloc(ptr)))
    return;

  time_call timer(free_histogram);
  _free(ptr);
}

void cfree(void *ptr) {
  ensure_loaded(cfree);
  if (unlikely(is_internal_alloc(ptr)))
    return;

  time_call timer(cfree_histogram);
  _cfree(ptr);
}

void *calloc(size_t n, size_t sz) {
  ensure_loaded_alloc(calloc, n * sz);

  time_call timer(calloc_histogram);
  return _calloc(n, sz);
}

void *realloc(void *ptr, size_t sz) {
  ensure_loaded(realloc);
  if (unlikely(is_internal_alloc(ptr)))
    return NULL;

  time_call timer(realloc_histogram);
  return _realloc(ptr, sz);
}

void *memalign(size_t alignment, size_t sz) {
  ensure_loaded(memalign);

  time_call timer(memalign_histogram);
  return _memalign(alignment, sz);
}

int posix_memalign(void **ptr, size_t alignment, size_t sz) {
  ensure_loaded(posix_memalign);

  time_call timer(posix_memalign_histogram);
  return _posix_memalign(ptr, alignment, sz);
}

void *aligned_alloc(size_t alignment, size_t sz) {
  ensure_loaded(aligned_alloc);

  time_call timer(aligned_alloc_histogram);
  return _aligned_alloc(alignment, sz);
}

size_t malloc_usable_size(void *ptr) {
  ensure_loaded(malloc_usable_size);

  time_call timer(malloc_usable_size_histogram);
  return _malloc_usable_size(ptr);
}
#ifdef __cplusplus
}  // extern "C"

static __attribute__((destructor)) void liblatalloc_fini() {
  _log("malloc:\n");
  hdr_percentiles_print(malloc_histogram,
                        stderr,  // File to write to
                        10,      // Granularity of printed values
                        1.0,     // Multiplier for results
                        CSV);    // Format CLASSIC/CSV supported.
  _log("free:\n");
  hdr_percentiles_print(free_histogram,
                        stderr,  // File to write to
                        10,      // Granularity of printed values
                        1.0,     // Multiplier for results
                        CSV);    // Format CLASSIC/CSV supported.
}

#endif
