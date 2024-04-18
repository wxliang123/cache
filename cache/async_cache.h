
#ifndef KVCACHE_ASYNC_CACHE_H
#define KVCACHE_ASYNC_CACHE_H

#include "cache.h"
#include "options.h"

namespace kvcache {

template <class Key, class Value>
class AsyncCache : public Cache<Key, Value> {
 private:
  class FastHash {};

 public:
  AsyncCache(uint32_t capacity) : capacity_(capacity), usage_(0) {}
  ~AsyncCache() {}

  bool Lookup(Key key, Value& value) override { return true; }

  bool Insert(Key key, const Value& value) override { return true; }

  bool Erase(Key key) override { return true; }

  uint64_t get_size() { return usage_.load(); }

  bool is_full() {
    uint64_t size = usage_.load();
    return size >= capacity_;
  }

 private:
  struct FastBuffer {};

  struct ListNode {
    Key key;
    ListNode* next;
    ListNode* prev;

    Value value;
    int refs;
    ListNode() : next(nullptr), prev(nullptr), refs(0) {}
  };

  const uint64_t capacity_;
  std::atomic<uint64_t> usage_;
};

}  // namespace kvcache

#endif