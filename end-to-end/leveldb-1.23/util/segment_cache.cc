
#include <atomic>
#include <memory>
#include <mutex>

#include "leveldb/cache.h"

#include "port/port.h"
#include "util/hash.h"
#include "util/mutexlock.h"

#include "tbb/concurrent_hash_map.h"

namespace leveldb {

namespace segment_cache {

constexpr static uint64_t kNumSlotsPerSegment = 16384;

struct Segment;

struct EntryHandle {
  void* value;
  void (*deleter)(const Slice&, void* value);

  size_t charge;
  std::atomic<uint32_t> refs;
  std::atomic<uint32_t> version;
  Segment* belong;

  bool is_indexed;  // Whether entry can be indexed by hash table.

  size_t key_length;
  char key_data[1];

  Slice key() const { return Slice(key_data, key_length); }
};

struct Slot {
  EntryHandle* entry;
  std::atomic<uint32_t> version;
  Slot() : entry(nullptr), version(0) {}
};

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
    auto victim = tail_segment.load();
    auto new_tail = victim->prev;
    tail_segment.store(new_tail);
    tail_lock.unlock();
    count--;
    return victim;
  }

  uint64_t get_count() { return count.load(); }
};

// A single shard of sharded cache.
class SegmentCache {
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

 public:
  SegmentCache() : capacity_(0), usage_(0){};
  ~SegmentCache() { Prune(); }

  // Separete from constructor so caller can easily make an array of
  // SegmentCache
  void SetCapacity(uint64_t capacity) { capacity_ = capacity; }

  Cache::Handle* Insert(const Slice& key, void* value, size_t charge,
                        void (*deleter)(const Slice& key, void* value));
  Cache::Handle* Lookup(const Slice& key);
  void Release(Cache::Handle* handle);
  void Erase(const Slice& key);
  void Prune();
  uint64_t TotalCharge() const { return usage_.load(); }

 private:
  void EvictOne();
  void FreeEntry(EntryHandle* entry);

 private:
  SegmentList segment_list_;
  HashMap hash_map_;

  // Intialized before use.
  uint64_t capacity_;
  std::atomic<uint64_t> usage_;
};

Cache::Handle* SegmentCache::Insert(const Slice& key, void* value,
                                    size_t charge,
                                    void (*deleter)(const Slice& key,
                                                    void* value)) {
  HashMapAccessor accessor;
  auto entry = reinterpret_cast<EntryHandle*>(
      malloc(sizeof(EntryHandle) - 1 + key.size()));
  entry->value = value;
  entry->version = 1;
  entry->deleter = deleter;
  entry->belong = segment_list_.head_segment.load();
  entry->charge = charge;
  entry->refs = 1;
  entry->key_length = key.size();
  std::memcpy(entry->key_data, key.data(), key.size());

  // Add into hash_table
  HashMapValuePair value_pair(entry->key(), entry);
  if (!hash_map_.insert(accessor, value_pair)) {
    free(entry);
    auto temp = accessor->second;
    // Update the value
    assert(temp->is_indexed);
    temp->value = value;
    temp->refs++;
    return reinterpret_cast<Cache::Handle*>(temp);
  }

  entry->is_indexed = true;

  segment_list_.Add(entry, entry->version.load());
  usage_.fetch_add(charge);

  while (usage_.load() >= capacity_) {
    EvictOne();
  }
  entry->refs++;
  return reinterpret_cast<Cache::Handle*>(entry);
}

Cache::Handle* SegmentCache::Lookup(const Slice& key) {
  HashMapConstAccessor const_accessor;
  if (hash_map_.find(const_accessor, key)) {
    auto temp = const_accessor->second;
    if (temp->belong != segment_list_.head_segment.load()) {
      // The head segment has been changed, and we need to add a
      // new version entry to reflect its recency.
      temp->refs++;
      uint32_t old_version = temp->version++;
      segment_list_.Add(temp, old_version + 1);
#ifdef SELF_STAT
      version_count_++;
#endif
    }
    temp->refs++;
    assert(temp->key().compare(key) == 0);
    return reinterpret_cast<Cache::Handle*>(temp);
  }
  return nullptr;
}

void SegmentCache::Release(Cache::Handle* handle) {
  auto entry = reinterpret_cast<EntryHandle*>(handle);
  auto old_refs = entry->refs--;
  if (old_refs == 1) {
    FreeEntry(entry);
  }
}

void SegmentCache::Erase(const Slice& key) {
  HashMapAccessor accessor;
  if (!hash_map_.find(accessor, key)) {
    auto entry = accessor->second;
    hash_map_.erase(accessor);
    entry->is_indexed = false;
    auto old_refs = entry->refs--;
    if (old_refs == 1) {
      FreeEntry(entry);
    }
  }
}

void SegmentCache::Prune() {
  while (segment_list_.get_count()) {
    EvictOne();
  }
}

void SegmentCache::EvictOne() {
  auto segment = segment_list_.Evict();
  // printf("evict segment number: %d\n", segment->number);
#ifdef SELF_STAT
  evicted_segment_count_++;
#endif
  for (int i = 0; i < kNumSlotsPerSegment; i++) {
    auto entry = segment->slot_array[i].entry;
    auto slot_version = segment->slot_array[i].version.load();
    if (entry->version.load() == slot_version) {
      HashMapAccessor accessor;
      if (!hash_map_.find(accessor, entry->key())) {
        printf("key: %s Presumably unreachable\n", entry->key().data());
        continue;
      }

      // At this time, entry is exclusively occupied by the accessor, so
      // there won't be any readers accessing it.
      if (entry->version.load() == slot_version) {
        hash_map_.erase(accessor);
        entry->is_indexed = false;

        // free entry
        auto old_version = entry->refs--;
        if (old_version == 1) {
          FreeEntry(entry);
        }
#ifdef SELF_STAT
        evicted_entry_count_++;
#endif
        continue;
      }
    }

    // free entry
    auto old_version = entry->refs--;
    if (old_version == 1) {
      FreeEntry(entry);
    }
  }  // end for
}

void SegmentCache::FreeEntry(EntryHandle* entry) {
  assert(!entry->is_indexed);
  entry->version = 0;
  (*entry->deleter)(entry->key(), entry->value);
  usage_.fetch_sub(entry->charge);
  free(entry);
}

static const int kNumShardBits = 1;
static const int kNumShards = 1 << kNumShardBits;

class ShardedSegmentCache : public Cache {
 private:
  SegmentCache shard_[kNumShards];
  port::Mutex id_mutex_;
  uint64_t last_id_;

  static inline uint64_t HashSlice(const Slice& s) {
    return Hash(s.data(), s.size(), 0);
  }

  static uint64_t Shard(uint64_t hash) { return hash >> (32 - kNumShardBits); }

 public:
  explicit ShardedSegmentCache(size_t capacity) : last_id_(0) {
    const size_t per_shard = (capacity + (kNumShards - 1)) / kNumShards;
    for (int s = 0; s < kNumShards; s++) {
      shard_[s].SetCapacity(per_shard);
    }
  }
  ~ShardedSegmentCache() {}
  Handle* Insert(const Slice& key, void* value, size_t charge,
                 void (*deleter)(const Slice& key, void* value)) override {
    const uint64_t hash = HashSlice(key);
    return shard_[Shard(hash)].Insert(key, value, charge, deleter);
  }
  Handle* Lookup(const Slice& key) override {
    const uint64_t hash = HashSlice(key);
    return shard_[Shard(hash)].Lookup(key);
  }
  void Release(Handle* handle) override {
    EntryHandle* h = reinterpret_cast<EntryHandle*>(handle);
    const uint64_t hash = HashSlice(h->key());
    shard_[Shard(hash)].Release(handle);
  }
  void Erase(const Slice& key) override {
    const uint64_t hash = HashSlice(key);
    shard_[Shard(hash)].Erase(key);
  }
  void* Value(Handle* handle) override {
    return reinterpret_cast<EntryHandle*>(handle)->value;
  }
  uint64_t NewId() override {
    MutexLock l(&id_mutex_);
    return ++(last_id_);
  }
  void Prune() override {
    for (int s = 0; s < kNumShards; s++) {
      shard_[s].Prune();
    }
  }
  size_t TotalCharge() const override {
    size_t total = 0;
    for (int s = 0; s < kNumShards; s++) {
      total += shard_[s].TotalCharge();
    }
    return total;
  }
};

}  // namespace segment_cache

Cache* NewSegmentCache(size_t capacity) {
  return new segment_cache::ShardedSegmentCache(capacity);
}

}  // namespace leveldb