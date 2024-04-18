
#ifndef KVCACHE_LRU_CACHE_H
#define KVCACHE_LRU_CACHE_H

#include <atomic>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>

#include "cache.h"
#include "options.h"
#include "statistics.h"
#include "tbb/concurrent_hash_map.h"

namespace kvcache {

template <class Key, class Value>
class LruCache : public Cache<Key, Value> {
  struct ListNode;
  using HashMap = tbb::concurrent_hash_map<Key, ListNode*>;
  using HashMapConstAccessor = HashMap::const_accessor;
  using HashMapAccessor = HashMap::accessor;
  using HashMapValuePair = HashMap::value_type;

 public:
  LruCache(uint64_t capacity)
      : capacity_(capacity),
        usage_(0),
        hash_map_(std::thread::hardware_concurrency() * 4) {
    head_.next = &tail_;
    tail_.prev = &head_;
  }

  ~LruCache() {}

  bool Lookup(Key key, Value& value) override {
    bool stat_yes = Cache<Key, Value>::sample_generator();
    HashMapConstAccessor const_accessor;
    if (!hash_map_.find(const_accessor, key)) {
      if (stat_yes) {
        Cache<Key, Value>::stats.RecordTick(Tickers::CACHE_MISS);
      }
      return false;
    }
    auto node = const_accessor->second;
    // Acquire the lock, but don't block if it is already held.
    std::unique_lock list_lock(list_mtx_, std::try_to_lock);
    if (list_lock) {
      value = node->value;
      if (node->is_in_list()) {
        LruRemove(node);
        LruAppend(node);
      }
      list_lock.unlock();
    }
    if (stat_yes) {
      Cache<Key, Value>::stats.RecordTick(Tickers::CACHE_HIT);
    }
    return true;
  }

  bool Insert(Key key, const Value& value) override {
    if (Cache<Key, Value>::sample_generator()) {
      Cache<Key, Value>::stats.RecordTick(Tickers::INSERT);
    }

    auto node = new ListNode();
    node->key = key;
    node->value = value;
    node->charge = 1;

    HashMapAccessor accessor;
    HashMapValuePair value_pair(key, node);
    if (!hash_map_.insert(accessor, value_pair)) {
      // update value
      accessor->second->value = value;
      delete node;
      return false;
    }

    // Evict if necessary
    uint64_t s = usage_.load();
    bool done = false;
    if (s >= capacity_) {
      EvictOne();
      done = true;
    }

    std::unique_lock list_lock(list_mtx_);
    LruAppend(node);
    list_lock.unlock();

    if (!done) {
      usage_++;
      s = usage_.load();
    }
    if (s > capacity_) {
      if (usage_.compare_exchange_strong(s, s - 1)) {
        EvictOne();
      }
    }

    return true;
  }

  bool Erase(Key key) override {
    HashMapAccessor accessor;
    if (!hash_map_.find(accessor, key)) {
      return false;
    }

    // Remove from list.
    std::unique_lock list_lock(list_mtx_);
    auto node = reinterpret_cast<ListNode*>(accessor->second);
    LruRemove(node);
    list_lock.unlock();

    hash_map_.erase(accessor);
    delete node;
    usage_--;
    return true;
  }

  virtual uint64_t get_size() override { return usage_.load(); }

  virtual bool is_full() override { return usage_.load() >= capacity_; }

 private:
  void EvictOne() {
    std::unique_lock list_lock(list_mtx_);
    auto node = tail_.prev;
    LruRemove(node);
    list_lock.unlock();

    HashMapAccessor accessor;
    if (!hash_map_.find(accessor, node->key)) {
      printf("key: %ld Presumably unreachable\n", node->key);
      return;
    }
    hash_map_.erase(accessor);
    delete node;
    return;
  }

 private:
  struct ListNode {
    Key key;
    Value value;

    ListNode* prev;
    ListNode* next;

    int charge;

    ListNode() : prev(out_of_list_marker), next(nullptr), charge(0) {}

    bool is_in_list() { return prev != out_of_list_marker; }
  };

  void LruAppend(ListNode* node) {
    auto old_real_head = head_.next;
    node->prev = &head_;
    node->next = old_real_head;
    old_real_head->prev = node;
    head_.next = node;
  }

  void LruRemove(ListNode* node) {
    auto prev_node = node->prev;
    auto next_node = node->next;
    prev_node->next = next_node;
    next_node->prev = prev_node;

    node->prev = out_of_list_marker;
  }

 private:
  static ListNode* const out_of_list_marker;

  ListNode head_;
  ListNode tail_;

  const uint64_t capacity_;
  std::atomic<uint64_t> usage_;

  HashMap hash_map_;

  std::mutex list_mtx_;
};

template <class Key, class Value>
LruCache<Key, Value>::ListNode* const LruCache<Key, Value>::out_of_list_marker =
    (ListNode*)(-1);

}  // namespace kvcache

#endif