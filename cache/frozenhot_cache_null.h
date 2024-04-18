
#ifndef FROZENHOT_LRU_CACHE_H
#define FROZENHOT_LRU_CACHE_H

#include <assert.h>
#include <tbb/concurrent_hash_map.h>

#include "fast_hash/clht_table.h"
#include "options.h"

namespace kvcache {

template <class Key, class Value>
class FrozenHotCache : public Cache<Key, Value> {
 private:
  constexpr static uint64_t kNullKey = 0;
  constexpr static uint64_t kTombKey = 1;
  constexpr static uint64_t kMarkerKey = 2;

  // LRU list node.
  //
  // We make a copy of the key in the list node, allowing us to find the
  // TBB::CHM element from the list node.
  struct ListNode {
    ListNode()
        : m_key(kNullKey),
          m_prev(out_of_list_marker_),
          m_next(nullptr),
          m_time(0) {}

    ListNode(const Key& key)
        : m_key(key),
          m_prev(out_of_list_marker_),
          m_next(nullptr),
          m_time(NowMicros()) {}

    bool is_in_list() const { return m_prev != out_of_list_marker_; }

    Key m_key;
    ListNode* m_prev;
    ListNode* m_next;
    uint64_t m_time;
  };

  static ListNode* const out_of_list_marker_;

  // The value is stored in the hashtable. The list node is allocated from an
  // internal object_pool. The ListNode* is owned by the lru list.
  struct HashMapValue {
    HashMapValue() : m_list_node(nullptr) {}
    HashMapValue(const Value& value, ListNode* node)
        : m_value(value), m_list_node(node) {}

    Value m_value;
    ListNode* m_list_node;
  };

  using HashMap =
      tbb::concurrent_hash_map<Key, HashMapValue, tbb::tbb_hash_compare<Key>>;
  using HashMapConstAccessor = HashMap::const_accessor;
  using HashMapAccessor = HashMap::accessor;
  using HashMapValuePair = HashMap::value_type;
  using SnapshotValue = std::pair<const Key, Value>;

 public:
  // The proxy object for TBB::CHM::const_accessor. Provides direct access to
  // the user's value by dereferencing, thus hiding implementation details.
  class ConstAccessor {
   public:
    ConstAccessor() {}
    ~ConstAccessor() {}

    const Value& operator*() const { return *get(); }
    const Value* operator->() const { return get(); }

    const Value get() const { return m_hash_accessor->second.m_value; }

   private:
    friend class FrozenHotCache;
    HashMapConstAccessor m_hash_accessor;
  };

 public:
  FrozenHotCache(uint64_t capacity);
  virtual ~FrozenHotCache();
  // Disable copying or assigment
  FrozenHotCache(const FrozenHotCache&) = delete;
  FrozenHotCache& operator=(const FrozenHotCache&) = delete;

  bool Lookup(Key key, Value& value) override;

  bool Insert(Key key, const Value& value) override;

  bool Erase(Key key) override;

  virtual uint64_t get_size() override { return m_size.load(); }

  virtual bool is_full() override { return m_size.load() >= m_max_size; }

 private:
  // The caller must lock the list mutex while this is called
  void LruPushFront(ListNode* node) {
    ListNode* old_real_head = m_head.m_next;
    node->m_prev = &m_head;
    node->m_next = old_real_head;
    old_real_head->m_prev = node;
    m_head.m_next = node;
  }

  // The caller must lock the list mutex while this is called
  void LruRemove(ListNode* node) {
    assert(node != nullptr);
    auto prev = node->m_prev;
    auto next = node->m_next;
    prev->m_next = next;
    next->m_prev = prev;
    //
    node->m_prev = out_of_list_marker_;
  }

  // The caller must lock the list mutex while this is called
  void LruPushAfterMarker(ListNode* node) {
    assert(m_marker);
    node->m_prev = m_marker;
    node->m_next = m_marker->m_next;
    m_marker->m_next->m_prev = node;
    m_marker->m_next = node;
  }

  // Require list mutex
  bool Evict();

  virtual bool ConstructTier() override;

  virtual bool ConstructFastCache(double ratio) override;

  virtual void DeleteFastCache() override;

  virtual bool GetCurve(bool should_stop) override;

 private:
  size_t m_max_size;
  std::atomic<size_t> m_size;

  HashMap m_map;
  std::unique_ptr<fast_hash::ClhtTable<Value>> m_fast_hash;

  ListNode m_fast_head;
  ListNode m_fast_tail;
  ListNode* m_marker = nullptr;

  ListNode m_head;
  ListNode m_tail;
  std::mutex m_list_mtx;

  std::atomic<bool> fast_cache_ready = false;
  std::atomic<bool> frozen_all = false;
  std::atomic<bool> fast_cache_construct = false;
  std::atomic<bool> enable_insert = true;
  std::atomic<bool> curve_flag = false;

  std::atomic<size_t> movement_counter{0};
  std::atomic<size_t> eviction_counter{0};

 private:
  bool sample_generator() {
    // if (!sample_flag_) {
    //   return true;
    // } else {
    //   return RandomDouble() < sample_percentage_;
    // }
    return false;
  }
  const double sample_percentage_ = 1.0 / 100;
  bool sample_flag_ = false;
};

template <class Key, class Value>
typename FrozenHotCache<Key, Value>::ListNode* const
    FrozenHotCache<Key, Value>::out_of_list_marker_ =
        reinterpret_cast<ListNode*>(-1);

template <class Key, class Value>
FrozenHotCache<Key, Value>::FrozenHotCache(uint64_t capacity)
    : m_max_size(capacity),
      m_size(0),
      m_map(std::thread::hardware_concurrency() * 4) {
  m_head.m_prev = nullptr;
  m_head.m_next = &m_tail;
  m_tail.m_prev = &m_head;

  m_fast_head.m_prev = nullptr;
  m_fast_head.m_next = &m_fast_tail;
  m_fast_tail.m_prev = &m_fast_head;

  int align_len = 1 + int(log2(m_max_size));
  m_fast_hash.reset(new fast_hash::ClhtTable<Value>(0, align_len));
}

template <class Key, class Value>
FrozenHotCache<Key, Value>::~FrozenHotCache() {}

template <class Key, class Value>
bool FrozenHotCache<Key, Value>::Lookup(Key key, Value& value) {
  bool stat_yes = sample_generator();
  HashMapConstAccessor hash_accessor;

  if (fast_cache_ready || frozen_all) {
    if (m_fast_hash->Find(key, value)) {
      if (stat_yes) {
        Cache<Key, Value>::stats.RecordTick(Tickers::FAST_CACHE_HIT);
      }
      return true;
    } else {
      if (frozen_all) {
        if (stat_yes) {
          Cache<Key, Value>::stats.RecordTick(Tickers::CACHE_MISS);
        }
        return false;
      }
    }
  }

  assert(!frozen_all);
  if (!m_map.find(hash_accessor, key)) {
    if (stat_yes) {
      Cache<Key, Value>::stats.RecordTick(Tickers::CACHE_MISS);
    }
    return false;
  }

  value = hash_accessor->second.m_value;
  if (!fast_cache_construct.load()) {
    auto node = hash_accessor->second.m_list_node;
    uint64_t last_update = 0;
    if (curve_flag.load()) {
      // There is no lock, so corner case is that curve_flag turns into false,
      // m_marker might cause core dump.
      last_update = node->m_time;
      if (m_marker && last_update <= m_marker->m_time) {
        if (stat_yes) {
          Cache<Key, Value>::stats.RecordTick(Tickers::CACHE_HIT);
        }
        movement_counter++;
        node->m_time = NowMicros();

        // update node
        std::unique_lock list_lock(m_list_mtx);
        if (node->is_in_list()) {
          LruRemove(node);
          LruPushFront(node);
        }

      } else if (stat_yes) {
        Cache<Key, Value>::stats.RecordTick(Tickers::FAST_CACHE_HIT);
      }
      return true;
    }

    std::unique_lock list_lock(m_list_mtx, std::try_to_lock);
    if (list_lock) {
      // The list node may be out of the list if it is in the process of being
      // inserted or evicted. Doing this check allows us to lock the list for
      // shorter periods of time.
      if (fast_cache_construct) {
        ;
      } else if (node->is_in_list()) {
        LruRemove(node);
        LruPushFront(node);
      }
    }
    list_lock.unlock();
  }

  if (stat_yes) {
    Cache<Key, Value>::stats.RecordTick(Tickers::CACHE_HIT);
  }
  return true;
}

template <class Key, class Value>
bool FrozenHotCache<Key, Value>::Insert(Key key, const Value& value) {
  bool stat_yes = sample_generator();
  if (stat_yes) {
    Cache<Key, Value>::stats.RecordTick(Tickers::INSERT);
  }

  if (!enable_insert.load()) {
    return false;
  }

  auto node = new ListNode(key);
  HashMapAccessor hash_accessor;
  HashMapValuePair value_pair(key, HashMapValue(value, node));
  if (!m_map.insert(hash_accessor, value_pair)) {
    // update value
    hash_accessor->second = HashMapValue(value, node);
    return false;
  }

  if (fast_cache_construct) {
    eviction_counter++;
  }

  auto s = m_size.load();
  bool done = false;
  if (s >= m_max_size) {
    done = Evict();
  }

  // Note that we have to update the LRU list before we increment m_size.
  std::unique_lock list_lock(m_list_mtx);
  if (!enable_insert.load()) {
    list_lock.unlock();
    m_map.erase(hash_accessor);
    delete node;
    return false;
  }
  if (!curve_flag.load()) {
    LruPushFront(node);
  } else {
    node->m_time = m_marker->m_time;
    LruPushAfterMarker(node);
  }
  list_lock.unlock();

  hash_accessor.release();  // for deadlock

  if (!done) {
    s = m_size++;
  }
  if (s > m_max_size) {
    // It is possible for the size to temporarily exceed the maximum if there is
    // a heavy-insert workload, once only as the cache fills. In this situation,
    // we should avoid to have every thread simultaneously attempt to evict the
    // extra entries, since we could end up underfilled. Instead, we do a
    // compare-and-exchange to acquire an exclusive right to reduce the size to
    // a particular value.

    if (Evict()) {
      m_size--;
    }
  }
  return true;
}

template <class Key, class Value>
bool FrozenHotCache<Key, Value>::Erase(Key key) {
  return false;
}

template <class Key, class Value>
bool FrozenHotCache<Key, Value>::ConstructTier() {
  std::unique_lock list_lock(m_list_mtx);
  fast_cache_construct = true;
  enable_insert = false;

  assert(m_fast_head.m_next == &m_fast_tail);
  assert(m_fast_tail.m_prev == &m_fast_head);

  m_fast_head.m_next = m_head.m_next;
  m_head.m_next->m_prev = &m_fast_head;
  m_fast_tail.m_prev = m_tail.m_prev;
  m_tail.m_prev->m_next = &m_fast_tail;

  m_head.m_next = &m_tail;
  m_tail.m_prev = &m_head;

  list_lock.unlock();

  auto temp_node = m_fast_head.m_next;
  ListNode* delete_node = nullptr;
  HashMapConstAccessor temp_hash_accessor;
  uint32_t count = 0;
  while (temp_node != &m_fast_tail) {
    if (temp_node->m_key == kTombKey) {
      delete_node = temp_node;
      temp_node = temp_node->m_next;
      LruRemove(delete_node);
      delete delete_node;
      continue;
    }

    if (!m_map.find(temp_hash_accessor, temp_node->m_key)) {
      delete_node = temp_node;
      temp_node = temp_node->m_next;
      if (delete_node->is_in_list()) {
        LruRemove(delete_node);
      }
      delete delete_node;
      continue;
    }

    m_fast_hash->Insert(temp_node->m_key, temp_hash_accessor->second.m_value);
    count++;
    temp_node = temp_node->m_next;
  }
  printf("fast cache insert num: %d, m_size: %ld, (FC_ratio: %.2lf)\n", count,
         m_size.load(), 1.0 * count / m_size.load());
  fast_cache_construct = false;
  return false;
}

template <class Key, class Value>
bool FrozenHotCache<Key, Value>::ConstructFastCache(double FC_ratio) {
  assert(FC_ratio <= 1 && FC_ratio >= 0);
  assert(m_fast_head.m_next == &m_fast_tail);
  assert(m_fast_tail.m_prev == &m_fast_head);

  // clear evcition counter to start
  if (eviction_counter.load() > 0) {
    eviction_counter = 0;
  }

  m_fast_head.m_next = m_head.m_next;

  // for thread safety
  fast_cache_construct = true;

  uint64_t FC_size = FC_ratio * m_max_size;
  uint64_t DC_size = m_max_size - FC_size;
  printf("FC size: %lu, DC size: %lu\n", FC_size, DC_size);
  int fail_count = 0, count = 0;

  // "first pass flag" is used to avoid inconsistency when eliminating global
  // lock. ???
  bool first_pass_flag = true;
  ListNode* temp_node = m_fast_head.m_next;
  ListNode* deleted_node = nullptr;
  HashMapConstAccessor temp_hash_accessor;

  while (temp_node != &m_fast_tail) {
    count++;
    auto eviction_num = eviction_counter.load();
    if (!m_map.find(temp_hash_accessor, temp_node->m_key)) {
      deleted_node = temp_node;
      temp_node = temp_node->m_next;
      if (deleted_node->is_in_list()) {
        LruRemove(deleted_node);
      }
      delete deleted_node;
      fail_count++;
      continue;
    }
    m_fast_hash->Insert(temp_node->m_key, temp_hash_accessor->second.m_value);
    temp_node = temp_node->m_next;

    if (count > FC_size - 20 /* magic number */ && first_pass_flag) {
      std::unique_lock list_lock(m_list_mtx);
      // m_fast_head.m_next is right
      auto node_before = m_fast_head.m_next->m_prev;
      auto node_after = temp_node;

      // set m_fast_tail
      m_fast_tail.m_prev = temp_node->m_prev;
      temp_node->m_prev->m_next = &m_fast_tail;

      // set first node
      m_fast_head.m_next->m_prev = &m_fast_head;

      // set m_head
      node_before->m_next = node_after;
      node_after->m_prev = node_before;
      list_lock.unlock();
      break;
    } else if (eviction_num > DC_size - 20 /* magic number */ &&
               first_pass_flag) {
      // frozen all ???
      std::unique_lock list_lock(m_list_mtx);
      auto node = m_fast_head.m_next;

      // set m_fast_tail
      m_fast_tail.m_prev = m_tail.m_prev;
      m_tail.m_prev->m_next = &m_fast_tail;

      // set m_tail
      m_tail.m_prev = node->m_prev;
      m_tail.m_prev->m_next = &m_tail;

      // set first node
      node->m_prev = &m_fast_head;
      list_lock.unlock();
      first_pass_flag = false;
    }
  }

  if (fail_count > 0) {
    printf(
        "fast hash insert num: %u, fail count: %u, m_size: %ld (FC_ratio: "
        "%.2lf)\n",
        count, fail_count, m_size.load(), 1.0 * count / m_size.load());
  } else {
    printf(
        "fast hash insert num: %u, m_size: %ld (FC_ratio: "
        "%.2lf)\n",
        count, m_size.load(), 1.0 * count / m_size.load());
  }

  fast_cache_ready = true;
  fast_cache_construct = false;
  eviction_counter = 0;
  return true;
}

template <class Key, class Value>
void FrozenHotCache<Key, Value>::DeleteFastCache() {
  std::unique_lock list_lock(m_list_mtx);

  auto node = m_head.m_next;
  m_fast_head.m_next->m_prev = &m_head;
  m_fast_tail.m_prev->m_next = node;
  m_head.m_next = m_fast_head.m_next;
  node->m_prev = m_fast_tail.m_prev;

  m_fast_head.m_next = &m_fast_tail;
  m_fast_tail.m_prev = &m_fast_head;

  if (fast_cache_ready) {
    fast_cache_ready = false;
  }
  list_lock.unlock();

  m_fast_hash->Clear();
}

template <class Key, class Value>
bool FrozenHotCache<Key, Value>::GetCurve(bool should_stop) {
  assert(enable_insert.load());
  m_marker = new ListNode(kMarkerKey);
  uint64_t pass_counter = 0;

  // stop sample
  sample_flag_ = false;

  std::unique_lock list_lock(m_list_mtx);
  LruPushFront(m_marker);
  curve_flag = true;

  Cache<Key, Value>::stats.ResetCursor();
  list_lock.unlock();

  uint64_t start_time = NowMicros();
  uint64_t FC_size = 0;
  for (int i = 0; i < 45 && !should_stop; i++) {
    do {
      usleep(1000);
      double temp_fast_hit = 0, temp_miss = 1;
      Cache<Key, Value>::stats.GetStep(temp_fast_hit, temp_miss);
      FC_size = movement_counter.load();
      // a magic number to avoid too many passes
      if (temp_fast_hit + temp_miss > 0.992 /* magic number */) {
        break;
      }
    } while (FC_size <= m_max_size * i * 1.0 / 100 * 2 && !should_stop);

    printf("curve pass: %lu\n", pass_counter++);
    double FC_size_ratio = 1.0 * FC_size / m_max_size;
    printf("FC_size: %lu (FC_ratio: %.3lf)\n", FC_size, FC_size_ratio);

    double FC_hit_ratio = 0, miss_ratio = 1;
    Cache<Key, Value>::stats.GetAndPrintStep(FC_hit_ratio, miss_ratio);

    printf("duration: %.3lf ms\n", 1.0 * (NowMicros() - start_time) / 1e3);
    start_time = NowMicros();
    fflush(stdout);

    if (FC_hit_ratio + miss_ratio > 0.992 || FC_hit_ratio > 0.9) {
      break;
    }

    Cache<Key, Value>::curve_container.insert(
        Cache<Key, Value>::curve_container.end(),
        CurveDataNode(FC_size_ratio, FC_hit_ratio, miss_ratio));
  }

  // start sample
  sample_flag_ = true;

  // delete marker from list
  list_lock.lock();
  curve_flag = false;
  LruRemove(m_marker);
  list_lock.unlock();

  delete m_marker;
  m_marker = nullptr;

  movement_counter = 0;
  return true;
}

template <class Key, class Value>
bool FrozenHotCache<Key, Value>::Evict() {
  std::unique_lock list_lock(m_list_mtx);
  ListNode* node = m_tail.m_prev;

  while (node->m_key == kTombKey) {
    LruRemove(node);
    delete node;
    node = m_tail.m_prev;
  }
  if (node == &m_head) {
    return false;
  }

  LruRemove(node);
  list_lock.unlock();

  HashMapAccessor hash_accessor;
  if (!m_map.find(hash_accessor, node->m_key)) {
    return false;
  }
  m_map.erase(hash_accessor);

  delete node;
  return true;
}

}  // namespace kvcache

#endif