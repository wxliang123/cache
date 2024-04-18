
#ifndef KVCACHE_GROUP_CACHE_H
#define KVCACHE_GROUP_CACHE_H

#include "cache.h"

namespace kvcache {

template <class Key, class Value>
class GroupCache : public Cache<Key, Value> {
 public:
  GroupCache(uint64_t capacity) {}
  ~GroupCache() {}

  bool Lookup(Key key, Value& value) override { return true; }
  bool Insert(Key key, const Value& value) override { return true; }
  bool Erase(Key key) override { return true; }
};

}  // namespace kvcache

#endif