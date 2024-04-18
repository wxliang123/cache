
#ifndef STORAGE_STATISTICS_H
#define STORAGE_STATISTICS_H

#include <atomic>
#include <stdint.h>

namespace stats {

enum Tickers : uint32_t { INSERT, HIT, MISS, TICKER_ENUM_MAX };

class Statistics {
 public:
  Statistics() {}
  ~Statistics() {}

  uint64_t GetTickerCount(Tickers ticker_type) const;

  void RecordTick(Tickers ticker_type, uint64_t count = 1);

  void SetTickerCount(Tickers ticker_type, uint64_t count);

  void ResetStat();

  void PrintStat();

 private:
  std::atomic<uint64_t> tickers_[static_cast<int>(Tickers::TICKER_ENUM_MAX)];
};

}  // namespace stats

#endif