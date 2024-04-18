
#ifndef API_ROCKSDB_H
#define API_ROCKSDB_H

#include <iostream>

#include "core/db.h"
#include "rocksdb/cache.h"
#include "rocksdb/db.h"
#include "rocksdb/filter_policy.h"
#include "rocksdb/options.h"
#include "rocksdb/statistics.h"
#include "rocksdb/table.h"

namespace ycsbc {

class RocksDB : public DB {
 public:
  virtual void Init(std::string cache_type, uint64_t cache_size) override {
    const char *db_path =
        "/home/wxl/Projects/KVCache/cache/end-to-end/data/rocksdb";
    options_.create_if_missing = true;
    options_.compression = rocksdb::kNoCompression;
    options_.use_direct_io_for_flush_and_compaction = true;
    options_.use_direct_reads = true;
    // options_.write_buffer_size = 64 << 20;
    // options_.target_file_size_base = 64 << 20;

    std::cout << "cache type: " << cache_type << std::endl;
    std::cout << "cache size: " << cache_size << std::endl;

    // Table options
    rocksdb::BlockBasedTableOptions table_options;
    table_options.cache_index_and_filter_blocks = false;
    if (cache_type.compare("lru_cache") == 0) {
      rocksdb::LRUCacheOptions lru_cache_options;
      lru_cache_options.capacity = cache_size;
      lru_cache_options.num_shard_bits = 0;
      lru_cache_options.strict_capacity_limit = false;
      lru_cache_options.secondary_cache = nullptr;
      table_options.block_cache = lru_cache_options.MakeSharedCache();
    } else if (cache_type.compare("segment_cache") == 0) {
      rocksdb::SegmentCacheOptions segment_cache_options;
      segment_cache_options.capacity = cache_size;
      segment_cache_options.num_shard_bits = 0;
      segment_cache_options.strict_capacity_limit = false;
      segment_cache_options.secondary_cache = nullptr;
      table_options.block_cache = segment_cache_options.MakeSharedCache();
    }
    table_options.filter_policy.reset(rocksdb::NewBloomFilterPolicy(10, false));

    options_.table_factory.reset(
        rocksdb::NewBlockBasedTableFactory(table_options));
    options_.statistics = rocksdb::CreateDBStatistics();

    auto s = rocksdb::DB::Open(options_, db_path, &db_);
    if (!s.ok()) {
      std::cout << "rocksdb: failed to open " << db_path << std::endl;
      exit(0);
    }

    printf("Wait for compaction ...\n");
    fflush(stdout);
    rocksdb::WaitForCompactOptions wait_for_compact_options;
    s = db_->WaitForCompact(wait_for_compact_options);
    assert(s.ok());
  }

  virtual void Close() override { delete db_; }

  virtual int Read(const std::string &table, const std::string &key,
                   const std::vector<std::string> *fields,
                   std::vector<KVPair> &result) override {
    std::string value;
    auto s = db_->Get(read_options_, key, &value);
    assert(s.ok());
    return DB::kOK;
  }

  virtual int Scan(const std::string &table, const std::string &key,
                   int record_count, const std::vector<std::string> *fields,
                   std::vector<std::vector<KVPair>> &result) override {
    auto iter = db_->NewIterator(read_options_);
    iter->Seek(key);
    for (int i = 0; i < record_count; i++) {
      if (!iter->Valid()) {
        break;
      }
      iter->Next();
    }
    delete iter;
    return 0;
  };

  virtual int Update(const std::string &table, const std::string &key,
                     std::vector<KVPair> &values) override {
    return Insert(table, key, values);
  }

  virtual int Insert(const std::string &table, const std::string &key,
                     std::vector<KVPair> &values) override {
    auto s = db_->Put(write_options_, key, values[0].second);
    assert(s.ok());
    return DB::kOK;
  }

  virtual int Delete(const std::string &table, const std::string &key) {
    auto s = db_->Delete(write_options_, key);
    assert(s.ok());
    return DB::kOK;
  }

  virtual void Print() override {
    std::cout << db_->GetDBOptions().statistics.get()->ToString() << std::endl;
  }

 private:
  rocksdb::DB *db_;
  rocksdb::Options options_;
  rocksdb::ReadOptions read_options_;
  rocksdb::WriteOptions write_options_;
};

}  // namespace ycsbc

#endif