
#ifndef KVCACHE_CACHE_H
#define KVCACHE_CACHE_H

#include <stdint.h>

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <random>
#include <vector>

#include "statistics.h"
#include "utils.h"

namespace kvcache {

struct CurveDataNode {
  double size;
  double FC_hit;
  double miss;
};

template <class Key, class Value>
class Cache {
 public:
  Cache() {}
  virtual ~Cache() {}

  virtual bool Lookup(Key key, Value& value) = 0;

  virtual bool Insert(Key key, const Value& value) = 0;

  virtual bool Erase(Key key) = 0;

  virtual bool ConstructTier() { return false; }

  virtual bool ConstructFastCache(double ratio) { return false; }

  virtual void DeleteFastCache() {}

  virtual bool GetCurve(bool should_stop) { return false; }

  virtual void PrintStatus() {}

  Statistics* get_stats() { return &stats; }

  std::vector<CurveDataNode>& get_container() { return curve_container; }

  virtual uint64_t get_size() { return 0; }

  virtual bool is_full() { return false; }

  Statistics stats;

  std::vector<CurveDataNode> curve_container;

  // Used to sample stats
  bool sample_generator() {
    if (!sample_flag_) {
      return true;
    } else {
      return ((double)(utils::random::Random::GetTLSInstance()->Next()) /
              (double)(RAND_MAX)) < sample_percentage_;
    }
  }

  const double sample_percentage_ = 1.0 / 100;

  bool sample_flag_ = false;
};

}  // namespace kvcache

#endif