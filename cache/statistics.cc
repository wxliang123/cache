
#include "statistics.h"

#include <string>
#include <vector>

namespace kvcache {

const std::vector<std::pair<Tickers, std::string>> TickersNameMap = {
    {FAST_CACHE_HIT, "fast.cache.hit"},
    {CACHE_HIT, "cache.hit"},
    {CACHE_MISS, "cache.miss"},
    {INSERT, "insert"}};

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
  for (int i = 0; i < (int)Tickers::TICKER_ENUM_MAX; i++) {
    tickers_[i] = 0;
    cursors_[i] = 0;
  }
}

void Statistics::PrintStat() {}

void Statistics::GetStat(uint64_t& fast_cache_hit, uint64_t& o_hit,
                         uint64_t& miss) {
  fast_cache_hit = tickers_[Tickers::FAST_CACHE_HIT].load();
  o_hit = tickers_[Tickers::CACHE_HIT].load();
  miss = tickers_[Tickers::CACHE_MISS].load();
  ResetStat();
}

void Statistics::GetAndPrintStat(uint64_t& fast_cache_hit, uint64_t& o_hit,
                                 uint64_t& miss) {}

void Statistics::ResetCursor() {
  for (int i = 0; i < (int)Tickers::TICKER_ENUM_MAX; i++) {
    cursors_[i] = tickers_[i].load();
  }
}

void Statistics::PrintStep() {
  auto t_fast_cache_hit = tickers_[Tickers::FAST_CACHE_HIT].load() -
                          cursors_[Tickers::FAST_CACHE_HIT];
  auto t_o_hit =
      tickers_[Tickers::CACHE_HIT].load() - cursors_[Tickers::CACHE_HIT];
  auto t_miss =
      tickers_[Tickers::CACHE_MISS].load() - cursors_[Tickers::CACHE_MISS];
  auto t_insert = tickers_[Tickers::INSERT].load() - cursors_[Tickers::INSERT];

  double total = t_fast_cache_hit + t_o_hit + t_insert;
  double temp = 0, global_miss = 0;
  if (total == 0) {
    temp = 1;
    global_miss = 1;
  } else {
    temp = 1 - 1.0 * t_fast_cache_hit / total;
    global_miss = 1.0 * t_insert / total;
  }

  printf("miss ratio: %.5f / %.5f\n", temp, global_miss);
  printf("fast cache hit: %ld, o hit: %ld, miss: %ld, insert: %ld\n",
         t_fast_cache_hit, t_o_hit, t_miss, t_insert);

  ResetCursor();
}

void Statistics::GetStep(double& FC_hit_ratio, double& miss_ratio) {
  auto t_fast_cache_hit = tickers_[Tickers::FAST_CACHE_HIT].load() -
                          cursors_[Tickers::FAST_CACHE_HIT];
  auto t_o_hit =
      tickers_[Tickers::CACHE_HIT].load() - cursors_[Tickers::CACHE_HIT];
  // auto t_miss =
  //     tickers_[Tickers::CACHE_MISS].load() - cursors_[Tickers::CACHE_MISS];
  auto t_insert = tickers_[Tickers::INSERT].load() - cursors_[Tickers::INSERT];

  uint64_t total = t_fast_cache_hit + t_o_hit + t_insert;
  double temp = 0;
  if (total == 0) {
    temp = 1;
    miss_ratio = 1;
  } else {
    temp = 1 - 1.0 * t_fast_cache_hit / total;
    miss_ratio = 1.0 * t_insert / total;
  }

  FC_hit_ratio = 1 - temp;
}

void Statistics::GetAndPrintStep(double& FC_hit_ratio, double& miss_ratio) {
  auto t_fast_cache_hit = tickers_[Tickers::FAST_CACHE_HIT].load() -
                          cursors_[Tickers::FAST_CACHE_HIT];
  auto t_o_hit =
      tickers_[Tickers::CACHE_HIT].load() - cursors_[Tickers::CACHE_HIT];
  auto t_miss =
      tickers_[Tickers::CACHE_MISS].load() - cursors_[Tickers::CACHE_MISS];
  auto t_insert = tickers_[Tickers::INSERT].load() - cursors_[Tickers::INSERT];

  uint64_t total = t_fast_cache_hit + t_o_hit + t_insert;
  double temp = 0;
  if (total == 0) {
    temp = 1;
    miss_ratio = 1;
  } else {
    temp = 1 - 1.0 * t_fast_cache_hit / total;
    miss_ratio = 1.0 * t_insert / total;
  }

  FC_hit_ratio = 1 - temp;

  printf("miss ratio: %.5f / %.5f\n", temp, miss_ratio);
  printf("fast cache hit: %ld, o hit: %ld, miss: %ld, insert: %ld\n",
         t_fast_cache_hit, t_o_hit, t_miss, t_insert);

  ResetCursor();
}

}  // namespace kvcache
