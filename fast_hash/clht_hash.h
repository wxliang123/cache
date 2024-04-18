
#ifndef CLHT_TABLE_H
#define CLHT_TABLE_H

#include <string>

#include "clht.h"
#include "fast_hash.h"

namespace fast_hash {

// value is std::shared_ptr<std::string>

template <class Value>
class CLHT_Hash : public FastHash<Value> {
 public:
  CLHT_Hash(int size, int exp) {
    int ex = 0;
    while (size != 0) {
      ex++;
      size >>= 1;
    }
    num_buckets_ = (uint32_t)(1 << (ex + exp));
    printf("Number of buckets: %u\n", num_buckets_);
    hash_table_ = clht_create(num_buckets_);
  }

  ~CLHT_Hash() { clht_gc_destroy(hash_table_); }

  virtual void thread_init(uint32_t tid) override {
    clht_gc_thread_init(hash_table_, tid);
  }

  virtual bool find(uint64_t key, Value& value) override {
    auto v = clht_get(hash_table_->ht, (clht_addr_t)key);
    if (v == 0) return false;
    value.reset((std::string*)v, [](auto p) {});
    return true;
  }

  virtual bool insert(uint64_t key, Value value) override {
    return clht_put(hash_table_, (clht_addr_t)key, (clht_val_t)(value.get())) !=
           0;
  };

  virtual void clear() override { clht_clear(hash_table_->ht); }

 private:
  clht_t* hash_table_;
  uint32_t num_buckets_;
};

}  // namespace fast_hash

#endif