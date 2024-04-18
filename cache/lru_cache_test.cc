
#include "lru_cache.h"

#include "gtest/gtest.h"

class LruCacheTest : public testing::Test {
 protected:
  void SetUp() override {
    lru_cache_ = new kvcache::LruCache<uint64_t, uint64_t>(capacity);
  }

 public:
  bool Insert(uint64_t key, uint64_t value) {
    return lru_cache_->Insert(key, value);
  }

  bool Lookup(uint64_t key, uint64_t& value) {
    return lru_cache_->Lookup(key, value);
  }

  bool Erase(uint64_t key) { return lru_cache_->Erase(key); }

 private:
  uint64_t capacity = 200;
  kvcache::LruCache<uint64_t, uint64_t>* lru_cache_;
};

TEST_F(LruCacheTest, HitAndMiss) {
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

int main(int argc, char* argv[]) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}