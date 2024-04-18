

#pragma once

#include <atomic>
#include <memory>
#include <string>

#include "cache/sharded_cache.h"
#include "port/malloc.h"
#include "rocksdb/secondary_cache.h"
#include "tbb/concurrent_hash_map.h"
#include "util/autovector.h"

namespace ROCKSDB_NAMESPACE {

namespace segment_cache {

// Segment cache implementation.

// An entry is a variable length heap-allocated structure.
// Entries are referenced by cache and/or by any external entity.
// The cache keeps all its entries in a hash table.

struct Segment;

struct EntryHandle {
  Cache::ObjectPtr value;
  const Cache::CacheItemHelper* helper;

  size_t total_charge;
  size_t key_length;
  // The hash of key(). Used for fast sharding and comparisons.
  uint32_t hash;
  std::atomic<uint32_t> refs;
  std::atomic<uint32_t> version;
  Segment* belong;

  char key_data[1];

  // For HandleImpl concept
  uint32_t GetHash() const { return hash; }

  void Free(MemoryAllocator* allocator) {
    assert(refs == 0);
    assert(helper);
    if (helper->del_cb) {
      helper->del_cb(value, allocator);
    }
    free(this);
  }

  inline size_t CalcuMetaCharge(
      CacheMetadataChargePolicy metadata_charge_policy) const {
    if (metadata_charge_policy != kFullChargeCacheMetadata) {
      return 0;
    } else {
#ifdef ROCKSDB_MALLOC_USABLE_SIZE
      return malloc_usable_size(
          const_cast<void*>(static_cast<const void*>(this)));
#else
      // This is the size that is used when a new handle is created.
      return sizeof(EntryHandle) - 1 + key_length;
#endif
    }
  }

  // Calculate the memory usage by metadata.
  inline void CalcTotalCharge(
      size_t charge, CacheMetadataChargePolicy metadata_charge_policy) {
    total_charge = charge + CalcuMetaCharge(metadata_charge_policy);
  }

  inline size_t GetCharge(
      CacheMetadataChargePolicy metadata_charge_policy) const {
    size_t meta_charge = CalcuMetaCharge(metadata_charge_policy);
    assert(total_charge >= meta_charge);
    return total_charge - meta_charge;
  }

  Slice key() const { return Slice(key_data, key_length); }
};

struct Slot {
  EntryHandle* entry;
  std::atomic<uint32_t> version;
  Slot() : entry(nullptr), version(0) {}
};

constexpr static int kNumSlotsPerSegment = 65536;  // 1 MB ~ 40

struct Segment {
  Slot slot_array[kNumSlotsPerSegment];
  std::atomic<uint32_t> used;

  Segment* next;
  Segment* prev;

  bool Append(EntryHandle* entry, uint32_t new_version) {
    auto slot_id = used.fetch_add(1);
    if (slot_id < kNumSlotsPerSegment) {
      slot_array[slot_id].entry = entry;
      slot_array[slot_id].version = new_version;
      return true;
    }
    return false;
  }

  bool IsFull() { return used.load() >= kNumSlotsPerSegment; }

  explicit Segment() : used(0), next(nullptr), prev(nullptr) {
    for (size_t i = 0; i < kNumSlotsPerSegment; i++) {
      slot_array[i].entry = nullptr;
      slot_array[i].version = 0;
    }
  }
};

struct SegmentList {
  std::mutex head_segment_mtx;
  std::atomic<Segment*> head_segment;

  std::mutex tail_segment_mtx;
  std::atomic<Segment*> tail_segment;

  std::atomic<uint64_t> count;

  SegmentList() : head_segment(nullptr), tail_segment(nullptr), count(0) {
    auto segment = new Segment();
    head_segment.store(segment);
    tail_segment.store(segment);
  }

  // Protected by 'head_segment_mtx'
  void Add(EntryHandle* entry, uint64_t new_version) {
  Retry:
    if (!head_segment.load()->Append(entry, new_version)) {
      std::unique_lock<std::mutex> head_lock(head_segment_mtx);
      if (head_segment.load()->IsFull()) {
        auto segment = new Segment();
        auto temp_head = head_segment.load();
        segment->next = temp_head;
        temp_head->prev = segment;

        // update head_segment
        head_segment.store(segment);
        count++;
      }
      head_lock.unlock();
      goto Retry;
    }
  }

  // Protected by 'tail_segment_mtx'
  Segment* Evict() {
    std::unique_lock<std::mutex> tail_lock(tail_segment_mtx);
    if (count.load() > 20 /* magic number */) {
      auto victim = tail_segment.load();
      auto new_tail = victim->prev;
      tail_segment.store(new_tail);
      tail_lock.unlock();
      count--;
      return victim;
    }
    return nullptr;
  }

  uint64_t get_count() { return count.load(); }
};

// A single shard of sharded cache.
class SegmentCacheShard final : public CacheShardBase {
 public:
  SegmentCacheShard(size_t capacity, bool strict_capacity_limit,
                    double high_pri_pool_ratio, double low_pri_pool_ratio,
                    bool use_adaptive_mutex,
                    CacheMetadataChargePolicy metadata_charge_policy,
                    int max_upper_hash_bits, MemoryAllocator* allocator,
                    SecondaryCache* secondary_cache);

 public:
  struct MyHashCompare {
    static size_t hash(const Slice& s) { return Hash(s.data(), s.size(), 0); }
    static bool equal(const Slice& key1, const Slice& key2) {
      return key1.compare(key2) == 0;
    }
  };

  using HashMap = tbb::concurrent_hash_map<Slice, EntryHandle*, MyHashCompare>;
  using HashMapConstAccessor = HashMap::const_accessor;
  using HashMapAccessor = HashMap::accessor;
  using HashMapValuePair = HashMap::value_type;

 public:  // Type definitions expected as parameter to ShardedCache
  using HandleImpl = EntryHandle;
  using HashVal = uint32_t;
  using HashCref = uint32_t;

 public:  // Function definitions expected as parameter to ShardedCache
  static inline HashVal ComputeHash(const Slice& key) {
    return Lower32of64(GetSliceNPHash64(key));
  }

  // Separate from constructor so caller can easily make an array
  // of LRUCache if current usage is more than new capacity, the
  // function will attempt to free the needed space.
  void SetCapacity(size_t capacity);

  // Set the flag to reject insertion if cache if full.
  void SetStrictCapacityLimit(bool strict_capacity_limit);

  // Set percentage of capacity reserved for high-pri cache entries.
  void SetHighPriorityPoolRatio(double high_pri_pool_ratio);

  // Set percentage of capacity reserved for low-pri cache entries.
  void SetLowPriorityPoolRatio(double low_pri_pool_ratio);

  // Like Cache methods, but with an extra "hash" parameter.
  Status Insert(const Slice& key, uint32_t hash, Cache::ObjectPtr value,
                const Cache::CacheItemHelper* helper, size_t charge,
                EntryHandle** handle, Cache::Priority priority);

  EntryHandle* Lookup(const Slice& key, uint32_t hash,
                      const Cache::CacheItemHelper* helper,
                      Cache::CreateContext* create_context,
                      Cache::Priority priority, bool wait, Statistics* stats);

  void Erase(const Slice& key, uint32_t hash);

  bool Release(EntryHandle* handle, bool useful, bool erase_if_last_ref);
  bool Ref(EntryHandle* handle);

  bool IsReady(EntryHandle* /*handle*/);
  void Wait(EntryHandle* /*handle*/) {}

  // Although in some platforms the update of size_t is atomic, to make sure
  // GetUsage() and GetPinnedUsage() work correctly under any platform, we'll
  // protect them with mutex_.

  size_t GetUsage() const;
  size_t GetPinnedUsage() const;
  size_t GetOccupancyCount() const;
  size_t GetTableAddressCount() const;

  void ApplyToSomeEntries(
      const std::function<void(const Slice& key, Cache::ObjectPtr value,
                               size_t charge,
                               const Cache::CacheItemHelper* helper)>& callback,
      size_t average_entries_per_lock, size_t* state);

  void EraseUnRefEntries();

 public:  // Other function definitions
  // Retrieves high pri pool ratio
  double GetHighPriPoolRatio();

  // Retrieves low pri pool ratio
  double GetLowPriPoolRatio();

  void AppendPrintableOptions(std::string& /*str*/) const;

 private:
  friend class SegmentCache;

  void MaintainPoolSize();

  void EvictOne();

  void FreeEntry(EntryHandle* e);

  SegmentList segment_list_;
  HashMap hash_map_;

  uint64_t capacity_;

  std::atomic<uint64_t> usage_;
  std::atomic<uint64_t> high_pri_pool_usage_;
  std::atomic<uint64_t> low_pri_pool_usage_;

  bool strict_capacity_limit_;

  double high_pri_pool_ratio_;
  uint64_t high_pri_pool_capacity_;
  double low_pri_pool_ratio_;
  uint64_t low_pri_pool_capacity_;

  MemoryAllocator* allocator_;

  SecondaryCache* secondary_cache_;
};

class SegmentCache final : public ShardedCache<SegmentCacheShard> {
 public:
  SegmentCache(size_t capacity, int num_shard_bits, bool strict_capacity_limit,
               double high_pri_pool_ratio, double low_pri_pool_ratio,
               std::shared_ptr<MemoryAllocator> memory_allocator = nullptr,
               bool use_adaptive_mutex = kDefaultToAdaptiveMutex,
               CacheMetadataChargePolicy metadata_charge_policy =
                   kDontChargeCacheMetadata,
               std::shared_ptr<SecondaryCache> secondary_cache = nullptr);
  const char* Name() const override { return "SegmentCache"; }
  ObjectPtr Value(Handle* handle) override;
  size_t GetCharge(Handle* handle) const override;
  const CacheItemHelper* GetCacheItemHelper(Handle* handle) const override;
  void WaitAll(std::vector<Handle*>& handles) override;

  void AppendPrintableOptions(std::string& str) const override;

 private:
  std::shared_ptr<SecondaryCache> secondary_cache_;
};

}  // namespace segment_cache

using SegmentCache = segment_cache::SegmentCache;
using SegmentCacheShard = segment_cache::SegmentCacheShard;
using EntryHandle = segment_cache::EntryHandle;

}  // namespace ROCKSDB_NAMESPACE
