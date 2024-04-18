
#include "leveldb/statistics.h"

#include <string>
#include <vector>

namespace stats {

const std::vector<std::pair<Tickers, std::string>> TickersNameMap = {
    {INSERT, "blockcache.insert"},
    {HIT, "blockcache.hit"},
    {MISS, "blockcache.miss"}};

uint64_t Statistics::GetTickerCount(Tickers ticker_type) const {
  return tickers_[static_cast<int>(ticker_type)].load();
}

void Statistics::RecordTick(Tickers ticker_type, uint64_t count) {
  tickers_[static_cast<int>(ticker_type)].fetch_add(count);
}

void Statistics::SetTickerCount(Tickers ticker_type, uint64_t count) {
  tickers_[static_cast<int>(ticker_type)].store(count);
}

void Statistics::ResetStat() {
  for (int i = 0; i < Tickers::TICKER_ENUM_MAX; i++) {
    tickers_[i] = 0;
  }
}

void Statistics::PrintStat() {
  printf("block cache insert: %ld\n", tickers_[Tickers::INSERT].load());
  printf("block cache hit: %ld\n", tickers_[Tickers::HIT].load());
  printf("block cache miss: %ld\n", tickers_[Tickers::MISS].load());
}

}  // namespace stats
