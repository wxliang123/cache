
#ifndef FIFO_CACHE_H
#define FIFO_CACHE_H

#include <atomic>
#include <mutex>

#include "cache.h"
#include "options.h"
#include "tbb/concurrent_hash_map.h"

namespace kvcache {

// FifoCache is a thread-safe hashtable with a limited size. When it is full,
// insert() evicts the item that first enters the cache.
//
// The find() fills a ConstAccessor object, which is a smart pointer. After
// eviction, destruction of the value is deferred until all ConstAccessor
// objects are destroyed.
//
// Write performance was observed to degrade rapidly when there is a heavy
// concurrent Put/Evict load, mostly due to locks in the underlying TBB::CHM. So
// if that is a possibility for your workload, ThreadSafeScalableCache is
// recommended insteaded.

template <class Key, class Value>
class FifoCache : public Cache<Key, Value> {
 private:
  struct ListNode {
    Key m_key;
    ListNode* m_prev;
    ListNode* m_next;

    ListNode() : m_prev(out_of_list_marker_), m_next(nullptr) {}
    ListNode(const Key& key)
        : m_key(key), m_prev(out_of_list_marker_), m_next(nullptr) {}

    bool is_in_list() const { return m_prev != out_of_list_marker_; }
  };

  static ListNode* const out_of_list_marker_;

  // Bind the value and the pointer to the node together. Thus, we can reduce
  // one pointer dereference when updating/finding the value. Because there are
  // not linked list adjustment operations.
  struct HashMapValue {
    HashMapValue(const Value& value, ListNode* node)
        : m_value(value), m_list_node(node) {}
    Value m_value;
    ListNode* m_list_node;
  };

  using HashMap = tbb::concurrent_hash_map<Key, HashMapValue>;
  using HashMapConstAccessor = HashMap::const_accessor;
  using HashMapAccessor = HashMap::accessor;
  using HashMapValuePair = HashMap::value_type;

 public:
  explicit FifoCache(uint64_t capacity);
  FifoCache(const FifoCache&) = delete;
  FifoCache& operator=(const FifoCache&) = delete;
  virtual ~FifoCache() {}

  virtual bool Lookup(Key key, Value& value) override;

  virtual bool Insert(Key key, const Value& value) override;

  virtual bool Erase(Key key) override;

  virtual uint64_t get_size() override { return usage_.load(); }

  virtual bool is_full() override { return usage_.load() >= capacity_; }

 private:
  void ListRemove(ListNode* node);
  void ListPushFront(ListNode* node);
  void EvictOne();

 private:
  const uint64_t capacity_;
  std::atomic<uint64_t> usage_;

  HashMap m_map;

  ListNode m_head;
  ListNode m_tail;

  std::mutex m_list_mtx;
};

template <class Key, class Value>
typename FifoCache<Key, Value>::ListNode* const
    FifoCache<Key, Value>::out_of_list_marker_ =
        reinterpret_cast<ListNode*>(-1);

template <class Key, class Value>
FifoCache<Key, Value>::FifoCache(uint64_t capacity)
    : capacity_(capacity), usage_(0) {
  m_head.m_next = &m_tail;
  m_tail.m_prev = &m_head;
}

template <class Key, class Value>
bool FifoCache<Key, Value>::Lookup(Key key, Value& value) {
  HashMapConstAccessor hash_accessor;
  if (!m_map.find(hash_accessor, key)) {
    Cache<Key, Value>::stats.RecordTick(Tickers::CACHE_MISS);
    return false;
  }

  value = hash_accessor->second.m_value;
  Cache<Key, Value>::stats.RecordTick(Tickers::CACHE_HIT);
  return true;
}

template <class Key, class Value>
bool FifoCache<Key, Value>::Insert(Key key, const Value& value) {
  if (Cache<Key, Value>::sample_generator()) {
    Cache<Key, Value>::stats.RecordTick(Tickers::INSERT);
  }

  auto node = new ListNode(key);
  HashMapAccessor hash_accessor;
  HashMapValuePair value_pair(key, HashMapValue(value, node));
  if (!m_map.insert(hash_accessor, value_pair)) {
    // update value
    hash_accessor->second.m_value = value;
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

  // Note that we have to update the lru list before we increase 'usage_', so
  // that other threads don't attempt to evict list items.
  std::unique_lock list_lock(m_list_mtx);
  ListPushFront(node);
  list_lock.unlock();

  if (!done) {
    usage_++;
    s = usage_.load();
  }
  if (s > capacity_) {
    // It is possible for the size to temporarily exceed the maximum if there is
    // a heavy-insert workload, once only as the cache fills. In this situation,
    // we have to be careful not to have every thread simulatneously attempt to
    // evict the extra entries, since we could end up underfilled. Instead we do
    // a compare-and-exchange to acquire an exclusive right to reduce the size
    // to a particular value.
    //
    // We could continue to evict in a loop, but if there are a lot of threads
    // here at the same time, that could lead to spinning. So we will just evict
    // one extra element per insert() until the overfill is rectified.
    if (usage_.compare_exchange_strong(s, s - 1)) {
      EvictOne();
    }
  }
  return true;
}

template <class Key, class Value>
bool FifoCache<Key, Value>::Erase(Key key) {
  HashMapAccessor accessor;
  if (!m_map.find(accessor, key)) {
    return false;
  }

  // Remove target node from list.
  std::unique_lock list_lock(m_list_mtx);
  auto node = reinterpret_cast<ListNode*>(accessor->second.m_list_node);
  ListRemove(node);
  list_lock.unlock();

  m_map.erase(accessor);
  delete node;
  usage_--;
  return true;
}

template <class Key, class Value>
void FifoCache<Key, Value>::EvictOne() {
  std::unique_lock list_lock(m_list_mtx);
  ListNode* node = m_tail.m_prev;
  if (node == &m_head) {
    printf("List is empty!\n");
    return;
  }
  ListRemove(node);
  list_lock.unlock();

  HashMapAccessor hash_accessor;
  if (!m_map.find(hash_accessor, node->m_key)) {
    printf("m_key: %ld Presumably unreachable\n", node->m_key);
    return;
  }
  m_map.erase(hash_accessor);
  delete node;
  return;
}

template <class Key, class Value>
void FifoCache<Key, Value>::ListPushFront(ListNode* node) {
  ListNode* old_real_head = m_head.m_next;
  node->m_prev = &m_head;
  m_head.m_next = node;
  node->m_next = old_real_head;
  old_real_head->m_prev = node;
}

template <class Key, class Value>
void FifoCache<Key, Value>::ListRemove(ListNode* node) {
  ListNode* prev = node->m_prev;
  ListNode* next = node->m_next;
  prev->m_next = next;
  next->m_prev = prev;
  // Update node
  node->m_prev = out_of_list_marker_;
}

}  // namespace kvcache

#endif