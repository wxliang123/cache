
#ifndef KVCACHE_SEGMENT_H
#define KVCACHE_SEGMENT_H

#include <assert.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <set>

#include "cache.h"
#include "options.h"
#include "tbb/concurrent_hash_map.h"

namespace kvcache {

// A tbb::concurrent_hash_map acts as a container of elements of type
// std::pair<const Key, T>. Typically, when accessing a container element, you
// are interested in either updating it or reading it. The template class
// support these two purposes respectively with classes accessor and
// const_accessor that acts as smart pointers.
//
// An accessor represents update(write) access. As long as it points to an
// element, all other attempts to look up that key in the table block until the
// access or is done.
//
// A const_accessor is simliar, except that is represents read-only access.
// Multiple const_accessors can point to the same element at the same time.

template <class Key, class Value>
class SegmentCache : public Cache<Key, Value> {
 private:
  struct Entry;
  using HashMap = tbb::concurrent_hash_map<Key, Entry*>;
  using HashMapConstAccessor = HashMap::const_accessor;
  using HashMapAccessor = HashMap::accessor;
  using HashMapValuePair = HashMap::value_type;

 private:
  struct Segment;

  struct Entry {
    Key key;
    Value value;

    std::atomic<uint32_t> version;
    std::atomic<uint32_t> refs;

    Segment* belong;
    uint32_t charge;

    Entry() : version(0), refs(0), belong(nullptr), charge(0) {}
  };

  struct Slot {
    Entry* entry;
    std::atomic<uint32_t> version;
    Slot() : entry(nullptr), version(0) {}
  };

  // 512
  // 1024
  // 4096
  // 16384
  // 32768
  // 65536
  // 131072
  constexpr static uint64_t kNumSlotsPerSegment = 65536;

  struct Segment {
    Slot slot_array[kNumSlotsPerSegment];
    std::atomic<uint32_t> used;

    Segment* next;
    Segment* prev;

    bool Append(Entry* entry, uint32_t new_version) {
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
      for (uint32_t i = 0; i < kNumSlotsPerSegment; i++) {
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
    void Add(Entry* entry, uint64_t new_version) {
    Retry:
      if (!head_segment.load()->Append(entry, new_version)) {
        std::unique_lock head_lock(head_segment_mtx);
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
      std::unique_lock tail_lock(tail_segment_mtx);
      if (count.load() > 4) {
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

 public:
  SegmentCache(uint64_t capacity) : capacity_(capacity) {
    printf("number of slots in one segment: %ld\n", kNumSlotsPerSegment);
    fflush(stdout);
  }

  ~SegmentCache() {}

  virtual bool Lookup(Key key, Value& value) override {
    bool stat_yes = Cache<Key, Value>::sample_generator();
    HashMapConstAccessor const_accessor;
    if (hash_map_.find(const_accessor, key)) {
      auto entry = const_accessor->second;
      value = entry->value;
      if (entry->belong != segment_list_.head_segment.load()) {
        // The head segment is changed, and we need to add a new version slot to
        // reflect its recency. Thus, the reference of entry is incremental.
        entry->refs++;
        uint32_t old_version = entry->version++;
        segment_list_.Add(entry, old_version + 1);
      }
      if (stat_yes) {
        Cache<Key, Value>::stats.RecordTick(Tickers::CACHE_HIT);
      }
      return true;
    } else {
      if (stat_yes) {
        Cache<Key, Value>::stats.RecordTick(Tickers::CACHE_MISS);
      }
      return false;
    }
  }

  virtual bool Insert(Key key, const Value& value) override {
    if (Cache<Key, Value>::sample_generator()) {
      Cache<Key, Value>::stats.RecordTick(Tickers::INSERT);
    }

    HashMapAccessor accessor;
    auto entry = new Entry();
    entry->key = key;
    entry->value = value;
    entry->version = 1;
    entry->belong = segment_list_.head_segment.load();
    entry->charge = 1;
    entry->refs = 1;  // referred by hash_map

    // Add into hash_table
    HashMapValuePair value_pair(key, entry);
    if (!hash_map_.insert(accessor, value_pair)) {
      // Update the value
      accessor->second->value = value;
      delete entry;
      return false;
    }

    entry->refs++;  // referred by slot
    segment_list_.Add(entry, entry->version.load());
    usage_.fetch_add(entry->charge);
    accessor.release();

    while (usage_.load() > capacity_) {
      EvictOne();
    }

    return true;
  }

  virtual bool Erase(Key key) override {
    HashMapAccessor accessor;
    if (!hash_map_.find(accessor, key)) {
      return false;
    }

    auto entry = reinterpret_cast<Entry*>(accessor->second);
    TryFreeEntry(entry);
    hash_map_.erase(accessor);
    return true;
  }

  virtual void PrintStatus() override {
    uint64_t num_segments = segment_list_.get_count();
    uint64_t size = num_segments * sizeof(Segment) / (1 << 20);
    printf("num segments: %ld (%ld MB)\n", num_segments, size);
    printf("entry size: %ld\n", sizeof(Entry));
  }

  virtual uint64_t get_size() override { return usage_.load(); }

  virtual bool is_full() override {
    uint64_t usage = usage_.load();
    return usage >= capacity_;
  }

 private:
  void EvictOne() {
    auto segment = segment_list_.Evict();
    // printf("evict segment number: %d\n", segment->number);
    if (!segment) return;

    for (uint64_t i = 0; i < kNumSlotsPerSegment; i++) {
      auto entry = segment->slot_array[i].entry;
      auto slot_version = segment->slot_array[i].version.load();
      assert(entry->refs.load() > 0);
      if (entry->version.load() == slot_version) {
        HashMapAccessor accessor;
        if (!hash_map_.find(accessor, entry->key) ||
            accessor->second != entry) {
          // The entry is only refered by slots.
          TryFreeEntry(entry);
          continue;
        }

        if (entry->version.load() == slot_version) {
          // The entry is exclusively occupied, so there won't be any readers
          // accessing it. And, we remove it from hash_map.
          assert(entry->refs.load() > 1);
          entry->refs--;
          hash_map_.erase(accessor);
        }
      }
      TryFreeEntry(entry);
    }
    delete segment;
  }

  void TryFreeEntry(Entry* entry) {
    auto old_ref = entry->refs--;
    if (old_ref == 1) {
      assert(entry->refs == 0);
      // entry->version = 0;
      usage_.fetch_sub(entry->charge);
      delete entry;
    }
  }

 private:
  SegmentList segment_list_;
  HashMap hash_map_;

  const uint64_t capacity_;
  std::atomic<uint64_t> usage_;
};

}  // namespace kvcache

#endif