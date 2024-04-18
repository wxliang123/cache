
#include "segment_cache.h"

#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "gtest/gtest.h"

class SegmentCacheTest : public testing::Test {
 protected:
  void SetUp() override {
    segment_cache_ = new kvcache::SegmentCache<uint64_t, uint64_t>(capacity);
  }

 public:
  bool Lookup(uint64_t key, uint64_t& value) {
    return segment_cache_->Lookup(key, value);
  }

  bool Insert(uint64_t key, uint64_t value) {
    return segment_cache_->Insert(key, value);
  }

  bool Erase(uint64_t key) { return segment_cache_->Erase(key); }

 private:
  uint64_t capacity = 200;
  kvcache::SegmentCache<uint64_t, uint64_t>* segment_cache_;
};

TEST_F(SegmentCacheTest, DISABLED_HitAndMiss) {
  uint64_t ret_value = 0;
  for (uint64_t i = 0; i < 300; i++) {
    Insert(i, i);
  }

  ASSERT_EQ(true, Lookup(150, ret_value));
  ASSERT_EQ(150, ret_value);

  ASSERT_EQ(true, Lookup(200, ret_value));
  ASSERT_EQ(200, ret_value);

  ASSERT_EQ(false, Lookup(100, ret_value));
  ASSERT_EQ(false, Lookup(400, ret_value));
}

TEST_F(SegmentCacheTest, Concurrency) {
  auto func = [&](int start, int num) {
    for (int i = 0; i < num; i++) {
      Insert(start + i, start + i);
    }
  };
  std::vector<std::thread> client_vtc;
  int num_clients = 4;
  int ops_per_client = 10;
  for (int i = 0; i < num_clients; i++) {
    int start = i * ops_per_client;
    client_vtc.emplace_back(func, start, ops_per_client);
  }
  for (int i = 0; i < num_clients; i++) {
    client_vtc[i].join();
  }
  for (int i = 0; i < num_clients * ops_per_client; i++) {
    uint64_t ret_value = 0;
    ASSERT_EQ(true, Lookup(i, ret_value));
    ASSERT_EQ(i, ret_value);
  }
}

int main(int argc, char* argv[]) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}