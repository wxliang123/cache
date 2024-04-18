
#pragma once

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>  // For 'gettimeofday'
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#include "sys/mman.h"

namespace utils {

#if defined(__GNUC__) && __GNUC__ >= 4
#define LIKELY(x) (__builtin_expect((x), 1))
#define UNLIKELY(x) (__builtin_expect((x), 0))
#else
#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#endif

inline uint64_t NowMicros() {
  static constexpr uint64_t kUsecondsPerSecond = 1000000;
  struct ::timeval tv;
  ::gettimeofday(&tv, nullptr);
  return static_cast<uint64_t>(tv.tv_sec) * kUsecondsPerSecond + tv.tv_usec;
}

class MySet {
 public:
  MySet() : size_(0), cursor_(0) {
    data_ = (double*)mmap(nullptr, capacity_, PROT_READ | PROT_WRITE,
                          MAP_SHARED | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);

    assert(data_ != nullptr);
  }

  ~MySet() { munmap(data_, capacity_); }

  void insert(double value) {
    uint64_t index = __sync_fetch_and_add(&size_, 1);
    assert(index < max_size_);
    data_[index] = value;
    sorted_ = false;
  }

  double sum() { return std::accumulate(data_, data_ + size_, 0.0); }

  double get(uint64_t index) {
    assert(index < size_);
    return data_[index];
  }

  double& operator[](uint64_t index) {
    assert(index < size_);
    return data_[index];
  }

  uint64_t size() { return size_; }

  void reset() {
    memset(data_, 0, sizeof(double) * size_);
    size_ = 0;
    cursor_ = 0;
    sorted_ = false;
  }

  double get_tail(double f) {
    if (!sorted_) {
      std::sort(data_, data_ + size_);
      sorted_ = true;
    }
    return data_[(uint64_t)(size_ * f)];
  }

  double print_tail() {
    if (size_ == 0) {
      printf("no stat to print tail\n");
      fflush(stdout);
      return 100;
    }
    auto size = size_;
    std::sort(data_, data_ + size);
    sorted_ = true;
    auto sum = std::accumulate(data_, data_ + size, 0.0);
    printf(
        "Avg: %.3lf (stat size: %ld, real size_: %ld), median: %.3lf, p9999: "
        "%.3lf, p999: %.3lf, p99: %.3lf, p90: %.3lf\n",
        sum * 1.0 / size, size, size_, data_[uint64_t(size * 0.50)],
        data_[uint64_t(size * 0.9999)], data_[uint64_t(size * 0.999)],
        data_[uint64_t(size * 0.99)], data_[uint64_t(size * 0.90)]);
    fflush(stdout);
    return sum * 1.0 / size;
  }

  double print_tail(uint64_t& size) {
    if (size_ == 0) {
      printf("no stat to print tail\n");
      return 100;
    }
    size = size_;
    std::sort(data_, data_ + size);
    sorted_ = true;
    auto sum = std::accumulate(data_, data_ + size, 0.0);
    printf(
        "avg: %.3lf (stat size: %ld, real size_: %ld), median: %.3lf, p9999: "
        "%.3lf, p999: %.3lf, p99: %.3lf, p90: %.3lf\n",
        sum * 1.0 / size, size, size_, data_[uint64_t(size * 0.50)],
        data_[uint64_t(size * 0.9999)], data_[uint64_t(size * 0.999)],
        data_[uint64_t(size * 0.99)], data_[uint64_t(size * 0.90)]);
    fflush(stdout);
    return sum * 1.0 / size;
  }

  // single writer!
  double print_from_last_end(uint64_t& step) {
    if (size_ == 0 || cursor_ >= size_) {
      printf("none\n");
      return 100;
    }
    auto size = size_;
    auto sum = std::accumulate(data_ + cursor_, data_ + size, 0.0);
    step = size - cursor_;
    printf("avg: %.3lf (stat size: %ld, size: %ld -> %ld)\n", sum * 1.0 / step,
           step, cursor_, size);
    fflush(stdout);
    cursor_ = size + 1;
    return sum * 1.0 / step;
  }

  uint64_t size_from_last_end() {
    auto size = size_;
    // assert(cursor <= size);
    if (size == 0 || cursor_ >= size) {
      return 0;
    } else {
      return size - cursor_;
    }
  }

  void print_data(std::string filename) {
    if (size_ == 0) return;
    FILE* out = fopen(filename.c_str(), "w");
    for (uint64_t i = 0; i < size_; i++) {
      fprintf(out, "%lf\n", data_[i]);
    }
    fclose(out);
    std::sort(data_, data_ + size_);
    sorted_ = true;
    printf("%s: %lf %lu %lf\n", filename.c_str(), sum(), (long int)size_,
           sum() / size_);
    printf("        p9999: %.2lf, p999: %.2lf, p99: %.2lf, p90: %.2lf\n",
           data_[uint64_t(size_ * 0.9999)], data_[uint64_t(size_ * 0.999)],
           data_[uint64_t(size_ * 0.99)], data_[uint64_t(size_ * 0.90)]);
  }

  void deallocate() { assert(munmap(data_, capacity_) == 0); }

 private:
  const uint64_t max_size_ = 1UL << 30;
  uint64_t capacity_ = max_size_ * sizeof(double);
  bool sorted_;

  uint64_t size_;
  uint64_t cursor_;
  double* data_;
  std::string describe_;
};

namespace random {
// A very simple random number generator.  Not especially good at
// generating truly random bits, but good enough for our needs in this
// package.
class Random {
 private:
  enum : uint32_t {
    M = 2147483647L  // 2^31-1
  };
  enum : uint64_t {
    A = 16807  // bits 14, 8, 7, 5, 2, 1, 0
  };

  uint32_t seed_;

  static uint32_t GoodSeed(uint32_t s) { return (s & M) != 0 ? (s & M) : 1; }

 public:
  // This is the largest value that can be returned from Next()
  enum : uint32_t { kMaxNext = M };

  explicit Random(uint32_t s) : seed_(GoodSeed(s)) {}

  void Reset(uint32_t s) { seed_ = GoodSeed(s); }

  uint32_t Next() {
    // We are computing
    //       seed_ = (seed_ * A) % M,    where M = 2^31-1
    //
    // seed_ must not be zero or M, or else all subsequent computed values
    // will be zero or M respectively.  For all other values, seed_ will end
    // up cycling through every number in [1,M-1]
    uint64_t product = seed_ * A;

    // Compute (product % M) using the fact that ((x << 31) % M) == x.
    seed_ = static_cast<uint32_t>((product >> 31) + (product & M));
    // The first reduction may overflow by 1 bit, so we may need to
    // repeat.  mod == M is not possible; using > allows the faster
    // sign-bit-based test.
    if (seed_ > M) {
      seed_ -= M;
    }
    return seed_;
  }

  // Returns a uniformly distributed value in the range [0..n-1]
  // REQUIRES: n > 0
  uint32_t Uniform(int n) { return Next() % n; }

  // Randomly returns true ~"1/n" of the time, and false otherwise.
  // REQUIRES: n > 0
  bool OneIn(int n) { return (Next() % n) == 0; }

  // Skewed: pick "base" uniformly from range [0,max_log] and then
  // return "base" random bits.  The effect is to pick a number in the
  // range [0,2^max_log-1] with exponential bias towards smaller numbers.
  uint32_t Skewed(int max_log) { return Uniform(1 << Uniform(max_log + 1)); }

  // Returns a Random instance for use by the current thread without
  // additional locking
  static Random* GetTLSInstance() {
    static __thread Random* tls_instance;
    static __thread std::aligned_storage<sizeof(Random)>::type
        tls_instance_bytes;
    auto rv = tls_instance;
    if (UNLIKELY(rv == nullptr)) {
      size_t seed = std::hash<std::thread::id>()(std::this_thread::get_id());
      rv = new (&tls_instance_bytes) Random((uint32_t)seed);
      tls_instance = rv;
    }
    return rv;
  }
};

}  // namespace random

}  // namespace utils
