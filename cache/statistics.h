
#ifndef KVCACHE_STATISTIC_H
#define KVCACHE_STATISTIC_H

#include <stdint.h>

#include <atomic>

namespace kvcache {

enum Tickers : uint32_t {
  FAST_CACHE_HIT,
  CACHE_HIT,
  CACHE_MISS,
  INSERT,
  TICKER_ENUM_MAX
};

class Statistics {
 public:
  Statistics() {}
  ~Statistics() {}

  uint64_t GetTickerCount(Tickers ticker_type) const;

  void RecordTick(Tickers ticker_type, uint64_t count = 1);

  void SetTickerCount(Tickers ticker_type, uint64_t count);

  void PrintStat();

  void GetStat(uint64_t& fast_cache_hit, uint64_t& o_hit, uint64_t& miss);

  void GetAndPrintStat(uint64_t& fast_cache_hit, uint64_t& o_hit,
                       uint64_t& miss);

  void ResetStat();

  void PrintStep();

  void GetStep(double& FC_hit_ratio, double& miss_ratio);

  void GetAndPrintStep(double& FC_hit_ratio, double& miss_ratio);

  void ResetCursor();

 private:
  std::atomic<uint64_t> tickers_[static_cast<int>(Tickers::TICKER_ENUM_MAX)];
  uint64_t cursors_[static_cast<int>(Tickers::TICKER_ENUM_MAX)];
};

}  // namespace kvcache

#endif