
#ifndef KVCACHE_OPTIONS_H
#define KVCACHE_OPTIONS_H

#include <stdint.h>

#include <memory>

#include "statistics.h"

namespace kvcache {

struct Options {
  uint64_t capacity;
  std::unique_ptr<Statistics> stats;

  Options() : capacity(0), stats(nullptr) {}
};

}  // namespace kvcache

#endif