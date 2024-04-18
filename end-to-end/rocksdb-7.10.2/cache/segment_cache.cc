
#include "cache/segment_cache.h"

namespace ROCKSDB_NAMESPACE {

namespace segment_cache {

namespace {
// A distinct pointer value for marking "dummy" cache entries
struct DummyValue {
  char val[12] = "kDummyValue";
};
DummyValue kDummyValue{};
}  // namespace

SegmentCacheShard::SegmentCacheShard(
    size_t capacity, bool strict_capacity_limit, double high_pri_pool_ratio,
    double low_pri_pool_ratio, bool use_adaptive_mutex,
    CacheMetadataChargePolicy metadata_charge_policy, int max_upper_hash_bits,
    MemoryAllocator* allocator, SecondaryCache* secondary_cache)
    : CacheShardBase(metadata_charge_policy),
      capacity_(0),
      usage_(0),
      high_pri_pool_usage_(0),
      low_pri_pool_usage_(0),
      strict_capacity_limit_(strict_capacity_limit),
      high_pri_pool_ratio_(high_pri_pool_ratio),
      high_pri_pool_capacity_(0),
      low_pri_pool_ratio_(low_pri_pool_ratio),
      low_pri_pool_capacity_(0),
      allocator_(allocator),
      secondary_cache_(secondary_cache) {
  SetCapacity(capacity);
}

void SegmentCacheShard::SetStrictCapacityLimit(bool strict_capacity_limit) {
  strict_capacity_limit_ = strict_capacity_limit;
}

void SegmentCacheShard::SetCapacity(size_t capacity) {
  capacity_ = capacity;
  printf("SegmentCacheShard::SetCapacity: %ld\n", capacity);
  high_pri_pool_capacity_ = capacity_ * high_pri_pool_ratio_;
  printf("SegmentCacheShard::high_pri_pool_capacity_: %ld\n",
         high_pri_pool_capacity_);
  low_pri_pool_capacity_ = capacity_ * low_pri_pool_ratio_;
  printf("SegmentCacheShard::low_pri_pool_capacity_: %ld\n",
         low_pri_pool_capacity_);
}

void SegmentCacheShard::SetHighPriorityPoolRatio(double high_pri_pool_ratio) {
  printf("SegmentCacheShard::SetHighPriorityPoolRatio: %.2lf\n",
         high_pri_pool_ratio);
  high_pri_pool_ratio_ = high_pri_pool_ratio;
  high_pri_pool_capacity_ = capacity_ * high_pri_pool_ratio_;
  MaintainPoolSize();
}

void SegmentCacheShard::SetLowPriorityPoolRatio(double low_pri_pool_ratio) {
  printf("SegmentCacheShard::SetLowPriorityPoolRatio: %.2lf\n",
         low_pri_pool_ratio);
  low_pri_pool_ratio_ = low_pri_pool_ratio;
  low_pri_pool_capacity_ = capacity_ * low_pri_pool_ratio_;
  MaintainPoolSize();
}

Status SegmentCacheShard::Insert(const Slice& key, uint32_t hash,
                                 Cache::ObjectPtr value,
                                 const Cache::CacheItemHelper* helper,
                                 size_t charge, EntryHandle** handle,
                                 Cache::Priority priority) {
  assert(helper);
  assert(!strict_capacity_limit_);

  // Allocate the memory here
  HashMapAccessor accessor;
  auto entry = reinterpret_cast<EntryHandle*>(
      malloc(sizeof(EntryHandle) - 1 + key.size()));
  entry->value = value;
  entry->helper = helper;
  entry->version = 1;
  entry->belong = segment_list_.head_segment.load();
  entry->refs = 1;
  entry->key_length = key.size();
  std::memcpy(entry->key_data, key.data(), key.size());
  entry->CalcTotalCharge(charge, metadata_charge_policy_);

  // Add into hash_table
  HashMapValuePair value_pair(entry->key(), entry);
  if (!hash_map_.insert(accessor, value_pair)) {
    free(entry);
    auto e = accessor->second;
    // Update the value
    e->value = value;
    e->refs++;
    *handle = e;
    return Status::OK();
  }

  segment_list_.Add(entry, entry->version.load());
  usage_.fetch_add(charge);

  while (usage_.load() >= capacity_) {
    EvictOne();
  }
  entry->refs++;
  *handle = entry;
  return Status::OK();
}

EntryHandle* SegmentCacheShard::Lookup(const Slice& key, uint32_t hash,
                                       const Cache::CacheItemHelper* helper,
                                       Cache::CreateContext* create_context,
                                       Cache::Priority priority, bool wait,
                                       Statistics* stats) {
  EntryHandle* e = nullptr;
  HashMapConstAccessor const_accessor;
  if (hash_map_.find(const_accessor, key)) {
    e = const_accessor->second;
    if (segment_list_.get_count() < 512 &&
        e->belong != segment_list_.head_segment.load()) {
      // The head segment has been changed, and we need to add a
      // new version entry to reflect its recency.
      e->refs++;
      uint32_t old_version = e->version++;
      segment_list_.Add(e, old_version + 1);
    }
    e->refs++;
    assert(e->key().compare(key) == 0);
  }
  return e;
}

void SegmentCacheShard::Erase(const Slice& key, uint32_t hash) {
  HashMapAccessor accessor;
  if (!hash_map_.find(accessor, key)) {
    auto entry = accessor->second;
    hash_map_.erase(accessor);
    auto old_refs = entry->refs--;
    if (old_refs == 1) {
      FreeEntry(entry);
    }
  }
}

bool SegmentCacheShard::Release(EntryHandle* entry, bool useful,
                                bool erase_if_last_ref) {
  if (entry == nullptr) {
    return false;
  }
  auto old_refs = entry->refs--;
  if (old_refs == 1) {
    FreeEntry(entry);
  }
  return true;
}

bool SegmentCacheShard::Ref(EntryHandle* handle) { return false; }

void SegmentCacheShard::EvictOne() {
  auto segment = segment_list_.Evict();
  if (segment == nullptr) {
    return;
  }
  // printf("evict segment number: %d\n", segment->number);

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

        // free entry
        // auto old_version = entry->refs--;
        // if (old_version == 1) {
        //   FreeEntry(entry);
        // }
        // continue;
      }
    }

    // free entry
    auto old_version = entry->refs--;
    if (old_version == 1) {
      FreeEntry(entry);
    }
  }  // end for
  delete segment;
}

void SegmentCacheShard::FreeEntry(EntryHandle* e) {
  e->version = 0;
  usage_.fetch_sub(e->total_charge);
  e->Free(allocator_);
}

bool SegmentCacheShard::IsReady(EntryHandle* e) {
  printf("SegmentCacheShard::IsReady\n");
  return false;
}

size_t SegmentCacheShard::GetUsage() const { return usage_.load(); }

size_t SegmentCacheShard::GetPinnedUsage() const {
  printf("SegmentCacheShard::GetPinnedUsage\n");
  return 0;
}

size_t SegmentCacheShard::GetOccupancyCount() const {
  printf("SegmentCacheShard::GetOccupancyCount\n");
  return 0;
}

size_t SegmentCacheShard::GetTableAddressCount() const { return 0; }

void SegmentCacheShard::ApplyToSomeEntries(
    const std::function<void(const Slice& key, Cache::ObjectPtr value,
                             size_t charge,
                             const Cache::CacheItemHelper* helper)>& callback,
    size_t average_entries_per_lock, size_t* state) {
  *state = SIZE_MAX;
}

void SegmentCacheShard::EraseUnRefEntries() {
  printf("SegmentCacheShard::EraseUnRefEntries\n");
  return;
}

double SegmentCacheShard::GetHighPriPoolRatio() { return high_pri_pool_ratio_; }

double SegmentCacheShard::GetLowPriPoolRatio() { return low_pri_pool_ratio_; }

void SegmentCacheShard::AppendPrintableOptions(std::string& str) const {
  const int kBufferSize = 200;
  char buffer[kBufferSize];
  {
    snprintf(buffer, kBufferSize, "    high_pri_pool_ratio: %.3lf\n",
             high_pri_pool_ratio_);
    snprintf(buffer + strlen(buffer), kBufferSize - strlen(buffer),
             "    low_pri_pool_ratio: %.3lf\n", low_pri_pool_ratio_);
  }
  str.append(buffer);
}

void SegmentCacheShard::MaintainPoolSize() {
  printf("SegmentCacheShard::MaintainPoolSize\n");
  return;
}

SegmentCache::SegmentCache(size_t capacity, int num_shard_bits,
                           bool strict_capacity_limit,
                           double high_pri_pool_ratio,
                           double low_pri_pool_ratio,
                           std::shared_ptr<MemoryAllocator> allocator,
                           bool use_adaptive_mutex,
                           CacheMetadataChargePolicy metadata_charge_policy,
                           std::shared_ptr<SecondaryCache> _secondary_cache)
    : ShardedCache(capacity, num_shard_bits, strict_capacity_limit,
                   std::move(allocator)),
      secondary_cache_(std::move(_secondary_cache)) {
  size_t per_shard = GetPerShardCapacity();
  auto secondary_cache = secondary_cache_.get();
  auto alloc = memory_allocator();
  InitShards([=](SegmentCacheShard* cs) {
    new (cs) SegmentCacheShard(
        per_shard, strict_capacity_limit, high_pri_pool_ratio,
        low_pri_pool_ratio, use_adaptive_mutex, metadata_charge_policy,
        /* max_upper_hash_bits */ 32 - num_shard_bits, alloc, secondary_cache);
  });
  printf("Segment Cache - num. shards: %d\n", GetNumShards());
}

Cache::ObjectPtr SegmentCache::Value(Handle* handle) {
  auto h = reinterpret_cast<const EntryHandle*>(handle);
  assert(h->value != &kDummyValue);
  return h->value;
}

size_t SegmentCache::GetCharge(Handle* handle) const {
  return reinterpret_cast<const EntryHandle*>(handle)->GetCharge(
      GetShard(0).metadata_charge_policy_);
}

const Cache::CacheItemHelper* SegmentCache::GetCacheItemHelper(
    Handle* handle) const {
  auto h = reinterpret_cast<const EntryHandle*>(handle);
  return h->helper;
}

void SegmentCache::WaitAll(std::vector<Handle*>& handles) {
  printf("SegmentCacheShard::WaitAll\n");
  return;
}

void SegmentCache::AppendPrintableOptions(std::string& str) const {
  ShardedCache::AppendPrintableOptions(str);  // options from shard
  if (secondary_cache_) {
    str.append("  secondary_cache:\n");
    str.append(secondary_cache_->GetPrintableOptions());
  }
}

}  // namespace segment_cache

std::shared_ptr<Cache> NewSegmentCache(
    size_t capacity, int num_shard_bits, bool strict_capacity_limit,
    double high_pri_pool_ratio,
    std::shared_ptr<MemoryAllocator> memory_allocator, bool use_adaptive_mutex,
    CacheMetadataChargePolicy metadata_charge_policy,
    const std::shared_ptr<SecondaryCache>& secondary_cache,
    double low_pri_pool_ratio) {
  if (num_shard_bits >= 20) {
    return nullptr;  // The cache cannot be sharded into too many fine pieces.
  }
  if (high_pri_pool_ratio < 0.0 || high_pri_pool_ratio > 1.0) {
    // Invalid high_pri_pool_ratio
    return nullptr;
  }
  if (low_pri_pool_ratio < 0.0 || low_pri_pool_ratio > 1.0) {
    // Invalid low_pri_pool_ratio
    return nullptr;
  }
  if (low_pri_pool_ratio + high_pri_pool_ratio > 1.0) {
    // Invalid high_pri_pool_ratio and low_pri_pool_ratio combination
    return nullptr;
  }
  if (num_shard_bits < 0) {
    num_shard_bits = GetDefaultCacheShardBits(capacity);
  }
  return std::make_shared<SegmentCache>(
      capacity, num_shard_bits, strict_capacity_limit, high_pri_pool_ratio,
      low_pri_pool_ratio, std::move(memory_allocator), use_adaptive_mutex,
      metadata_charge_policy, secondary_cache);
}

std::shared_ptr<Cache> NewSegmentCache(const SegmentCacheOptions& cache_opts) {
  return NewSegmentCache(
      cache_opts.capacity, cache_opts.num_shard_bits,
      cache_opts.strict_capacity_limit, cache_opts.high_pri_pool_ratio,
      cache_opts.memory_allocator, cache_opts.use_adaptive_mutex,
      cache_opts.metadata_charge_policy, cache_opts.secondary_cache,
      cache_opts.low_pri_pool_ratio);
}

std::shared_ptr<Cache> NewSegmentCache(
    size_t capacity, int num_shard_bits, bool strict_capacity_limit,
    double high_pri_pool_ratio,
    std::shared_ptr<MemoryAllocator> memory_allocator, bool use_adaptive_mutex,
    CacheMetadataChargePolicy metadata_charge_policy,
    double low_pri_pool_ratio) {
  return NewSegmentCache(capacity, num_shard_bits, strict_capacity_limit,
                         high_pri_pool_ratio, memory_allocator,
                         use_adaptive_mutex, metadata_charge_policy, nullptr,
                         low_pri_pool_ratio);
}

}  // namespace ROCKSDB_NAMESPACE