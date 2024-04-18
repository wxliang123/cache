
#ifndef KVCACHE_LRU_CACHE_SHARED_HASH_H
#define KVCACHE_LRU_CACHE_SHARED_HASH_H

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
class LruCacheSharedHash : public Cache<Key, Value> {
  using HashMap = tbb::concurrent_hash_map<uint64_t, void*>;
  using HashMapConstAccessor = HashMap::const_accessor;
  using HashMapAccessor = HashMap::accessor;
  using HashMapValuePair = HashMap::value_type;

 public:
  LruCacheSharedHash(tbb::concurrent_hash_map<uint64_t, void*>& hash_map,
                     uint64_t capacity)
      : hash_map_(hash_map), capacity_(capacity), usage_(0) {
    head_.next = &tail_;
    tail_.prev = &head_;
  }

  ~LruCacheSharedHash() {
    printf("point_time: %.2lf\n", 1.0 * point_time / (1e6));
  }

  uint64_t point_time = 0;
  bool Lookup(Key key, Value& value) override {
    bool stat_yes = Cache<Key, Value>::sample_generator();
    HashMapConstAccessor const_accessor;
    uint64_t start_time = utils::NowMicros();
    if (!hash_map_.find(const_accessor, key)) {
      point_time += (utils::NowMicros() - start_time);
      if (stat_yes) {
        Cache<Key, Value>::stats.RecordTick(Tickers::CACHE_MISS);
      }
      return false;
    }
    point_time += (utils::NowMicros() - start_time);
    auto node = reinterpret_cast<ListNode*>(const_accessor->second);
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

    HashMapAccessor accessor;
    auto node = new ListNode();
    node->key = key;
    node->value = value;
    node->charge = 1;

    HashMapValuePair value_pair(key, node);
    if (!hash_map_.insert(accessor, value_pair)) {
      // update value
      accessor->second = node;
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
  // void Evict() {
  //   while (usage_ > capacity_) {
  //     auto node = tail_.prev;
  //     LruRemove(node);

  //     HashMapAccessor accessor;
  //     if (!hash_map_.find(accessor, node->key)) {
  //       printf("key: %ld Presumably unreachable\n", node->key);
  //       continue;
  //     }
  //     hash_map_.erase(accessor);
  //     usage_ -= node->charge;
  //     delete node;
  //   }
  // }

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

  HashMap& hash_map_;

  const uint64_t capacity_;
  std::atomic<uint64_t> usage_;

  std::mutex list_mtx_;
};

template <class Key, class Value>
LruCacheSharedHash<Key, Value>::ListNode* const
    LruCacheSharedHash<Key, Value>::out_of_list_marker = (ListNode*)(-1);

}  // namespace kvcache

#endif