

#ifndef KVCACHE_FAST_HASH_H
#define KVCACHE_FAST_HASH_H

namespace fast_hash {

template <class Value>
class FastHash {
 public:
  virtual void thread_init(uint32_t tid) = 0;

  virtual bool find(uint64_t key, Value& value) = 0;

  virtual bool insert(uint64_t key, Value value) = 0;

  virtual void clear() = 0;
};

}  // namespace fast_hash

#endif