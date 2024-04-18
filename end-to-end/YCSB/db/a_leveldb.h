
#ifndef API_LEVELDB_H
#define API_LEVELDB_H

#include <iostream>
#include <vector>

#include "core/db.h"
#include "leveldb/cache.h"
#include "leveldb/db.h"
#include "leveldb/filter_policy.h"

namespace ycsbc {

class LevelDB : public DB {
 public:
  virtual void Init(std::string cache_type, uint64_t cache_size) override {
    const char *db_path =
        "/home/wxl/Projects/KVCache/cache/end-to-end/data/leveldb";
    options_.create_if_missing = true;
    options_.write_buffer_size = 16ul << 20;  // 16MB
    options_.max_file_size = 16ul << 20;      // 16MB
    options_.max_open_files = 10000;

    std::cout << "init database:" << std::endl;
    std::cout << "write buffer size: " << options_.write_buffer_size
              << std::endl;
    std::cout << "max file size: " << options_.max_file_size << std::endl;
    std::cout << "cache type: " << cache_type << std::endl;
    std::cout << "cache size: " << cache_size << std::endl;

    if (cache_type.compare("lru_cache") == 0) {
      options_.block_cache = leveldb::NewLRUCache(cache_size);
    } else if (cache_type.compare("segment_cache") == 0) {
      options_.block_cache = leveldb::NewSegmentCache(cache_size);
    } else {
      std::cout << "wrong cache type!" << std::endl;
      exit(0);
    }

    options_.compression = leveldb::kNoCompression;

    options_.filter_policy = leveldb::NewBloomFilterPolicy(10);
    auto s = leveldb::DB::Open(options_, db_path, &db_);
    if (!s.ok()) {
      std::cout << "leveldb:: failed to open " << db_path << std::endl;
      exit(0);
    }

    read_options_.stats = new stats::Statistics();
  }

  virtual void Close() override { free(db_); }

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

  virtual void Print() override { read_options_.stats->PrintStat(); }

 private:
  leveldb::DB *db_;
  leveldb::Options options_;
  leveldb::ReadOptions read_options_;
  leveldb::WriteOptions write_options_;
};

}  // namespace ycsbc

#endif