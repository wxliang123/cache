
#ifndef SCALABLE_CACHE_H
#define SCALABLE_CACHE_H

#include "async_cache.h"
#include "fifo_cache.h"
#include "group_cache.h"
#include "lru_cache.h"
#include "lru_cache_shared_hash.h"
#include "segment_cache.h"
#include "statistics.h"

namespace kvcache {

const uint32_t check_sleep_internal_us = 100000;  // 0.1s
uint32_t sleep_threshold = 2;
const uint32_t wait_stable_sleep_interval_us = 500000;  // 0.5s
const uint32_t wait_stable_threshold = 2;
const double fast_performance_threshold = 0.2;
const uint32_t pass_threshold = 3;
const uint32_t drop_threshold = 2;
const uint32_t frozen_threshold = 100;

// Used in 'FrozenMonitor'
utils::MySet request_latency_set[16];

utils::MySet hit_latency_set;
utils::MySet other_latency_set;
uint64_t time_cursor = 0;
size_t print_step_counter = 0;

enum class CacheType : uint8_t {
  ASYNC = 1,
  LRU = 2,
  FIFO = 3,
  FROZENHOT = 4,
  GROUP = 5,
  SEGMENT = 6,
};

template <class Key, class Value>
class ConcurrentScalableCache {
  using Shard = Cache<Key, Value>;

 public:
  explicit ConcurrentScalableCache(uint64_t capacity, uint32_t num_shards,
                                   CacheType type);
  ~ConcurrentScalableCache() { Stop(); }

 public:
  bool Lookup(Key key, Value& value) {
    return get_shard(key).Lookup(key, value);
  }

  bool Insert(Key key, const Value& value) {
    return get_shard(key).Insert(key, value);
  }

  bool Erase(Key key) { return get_shard(key).Erase(key); }

  double get_size();

 public:
  void PrintMissRatio();
  void PrintMissRatio(double& miss_ratio);

  void PrintFrozenStat();

  uint64_t GetStepSize();

  double PrintStepLat();
  double PrintStepLat(uint64_t& total_num);
  double PrintStepLat(double& avg_hit, double& avg_other);

  void PrintGlobalLat();

  void PrintStatus();

  void Monitor();
  void FrozenMonitor() { printf("FrozenMoniter is not implemented!\n"); };
  void Stop();

  // A flag to switch on/off the sampling of inner counters
  bool stop_sample_stat = true;

 private:
  inline bool double_is_equal(double left, double right) {
    return (abs(left - right) < 0.0001);
  }

  /**
   * Get the child container for a given key
   */
  Shard& get_shard(const Key& key) {
    size_t h = key % num_shards_;
    return *shards_.at(h);
  }

  const uint32_t num_shards_;
  using ShardPtr = std::shared_ptr<Shard>;
  std::vector<ShardPtr> shards_;

  const uint64_t max_size_;
  double baseline_performance;
  bool should_stop_;
  bool beginning_flag_;

  tbb::concurrent_hash_map<uint64_t, void*> shared_hash_;
};

template <class Key, class Value>
ConcurrentScalableCache<Key, Value>::ConcurrentScalableCache(
    uint64_t capacity, uint32_t num_shards, CacheType type)
    : num_shards_(num_shards),
      max_size_(capacity),
      baseline_performance(0),
      should_stop_(false),
      beginning_flag_(true) {
  for (uint32_t i = 0; i < num_shards_; i++) {
    size_t s = max_size_ / num_shards_;

    if (CacheType::FIFO == type) {
      shards_.emplace_back(std::make_shared<FifoCache<Key, Value>>(s));
    } else if (CacheType::LRU == type) {
      shards_.emplace_back(std::make_shared<LruCache<Key, Value>>(s));
      // shards_.emplace_back(
      //     std::make_shared<LruCacheSharedHash<Key, Value>>(shared_hash_, s));
    } else if (CacheType::GROUP == type) {
      shards_.emplace_back(std::make_shared<GroupCache<Key, Value>>(s));
    } else if (CacheType::ASYNC == type) {
      shards_.emplace_back(std::make_shared<AsyncCache<Key, Value>>(s));
    } else if (CacheType::SEGMENT == type) {
      shards_.emplace_back(std::make_shared<SegmentCache<Key, Value>>(s));
    }
  }
}

template <class Key, class Value>
double ConcurrentScalableCache<Key, Value>::get_size() {
  uint64_t size = 0;
  for (uint32_t i = 0; i < num_shards_; i++) {
    size += shards_[i]->get_size();
  }
  return size;
}

template <class Key, class Value>
void ConcurrentScalableCache<Key, Value>::PrintMissRatio() {
  uint64_t total_hit = 0, total_miss = 0;
  for (uint32_t i = 0; i < num_shards_; i++) {
    uint64_t fast_cache_hit = 0, o_hit = 0, miss = 0;
    shards_[i]->get_stats()->GetStat(fast_cache_hit, o_hit, miss);
    total_hit += (fast_cache_hit + o_hit);
    total_miss += miss;
  }
  double temp = 1;
  if (total_hit + total_miss != 0) {
    temp = 1.0 * total_miss / (total_hit + total_miss);
    printf("total miss ratio: %.4lf, hit num: %lu, miss num: %lu\n", temp,
           total_hit, total_miss);
    fflush(stdout);
  }
}

template <class Key, class Value>
void ConcurrentScalableCache<Key, Value>::PrintMissRatio(double& miss_ratio) {
  uint64_t total_hit = 0, total_miss = 0;
  for (uint32_t i = 0; i < num_shards_; i++) {
    uint64_t fast_cache_hit = 0, o_hit = 0, miss = 0;
    shards_[i]->get_stats()->GetStat(fast_cache_hit, o_hit, miss);
    total_hit += (fast_cache_hit + o_hit);
    total_miss += miss;
  }
  if (total_hit + total_miss != 0) {
    miss_ratio = 1.0 * total_miss / (total_hit + total_miss);
    printf("total miss ratio: %.4lf, hit num: %lu, miss num: %lu\n", miss_ratio,
           total_hit, total_miss);
    fflush(stdout);
  }
}

template <class Key, class Value>
void ConcurrentScalableCache<Key, Value>::PrintFrozenStat() {
  uint64_t total_fc_hit = 0, total_o_hit = 0, total_miss = 0;
  for (uint32_t i = 0; i < num_shards_; i++) {
    uint64_t fast_cache_hit = 0, o_hit = 0, miss = 0;
    shards_[i]->get_stats()->GetStat(fast_cache_hit, o_hit, miss);
    total_fc_hit += fast_cache_hit;
    total_o_hit += o_hit;
    total_miss += miss;
  }

  double temp = 0, miss_ratio = 0;
  uint64_t total = total_fc_hit + total_o_hit + total_miss;
  if (total == 0) {
    temp = 1;
    miss_ratio = 1;
  } else {
    temp = 1 - 1.0 * total_fc_hit / total;
    miss_ratio = 1.0 * total_miss / total;
  }
  printf("miss ratio: %.4f / %.4f\n", temp, miss_ratio);
  printf("fast cache hit: %lu, o hit: %lu, miss: %lu\n", total_fc_hit,
         total_o_hit, total_miss);
  fflush(stdout);
}

template <class Key, class Value>
uint64_t ConcurrentScalableCache<Key, Value>::GetStepSize() {
  return hit_latency_set.size_from_last_end() +
         other_latency_set.size_from_last_end();
}

template <class Key, class Value>
double ConcurrentScalableCache<Key, Value>::PrintStepLat() {
  uint64_t num_hit = 0, num_other = 0;
  auto curr_time = utils::NowMicros();
  printf(" -hit ");
  auto avg_hit = hit_latency_set.print_from_last_end(num_hit);
  printf(" -other ");
  auto avg_other = other_latency_set.print_from_last_end(num_other);

  auto total_num = num_hit + num_other;
  auto temp = (avg_hit * num_hit + avg_other * num_other) / total_num;
  printf(
      "total avg Lat: %.3lf (size: %lu, duration: %.5lf s, approx miss rate: "
      "%.4lf)\n",
      temp, total_num, 1.0 * (curr_time - time_cursor) / 1000 / 1000,
      1.0 * num_other / total_num);
  time_cursor = curr_time;
  return temp;
}

template <class Key, class Value>
double ConcurrentScalableCache<Key, Value>::PrintStepLat(uint64_t& total_num) {
  uint64_t num_hit = 0, num_other = 0;
  auto curr_time = utils::NowMicros();
  printf(" -hit ");
  auto avg_hit = hit_latency_set.print_from_last_end(num_hit);
  printf(" -other ");
  auto avg_other = other_latency_set.print_from_last_end(num_other);

  total_num = num_hit + num_other;
  auto temp = (avg_hit * num_hit + avg_other * num_other) / total_num;
  printf(
      "total avg Lat: %.3lf (size: %lu, duration: %.5lf s, approx miss rate: "
      "%.4lf)\n",
      temp, total_num, 1.0 * (curr_time - time_cursor) / 1000 / 1000,
      num_other * 1.0 / total_num);
  time_cursor = curr_time;
  return temp;
}

template <class Key, class Value>
double ConcurrentScalableCache<Key, Value>::PrintStepLat(double& avg_hit,
                                                         double& avg_other) {
  uint64_t num_hit = 0, num_other = 0;
  auto curr_time = utils::NowMicros();
  printf(" -hit ");
  avg_hit = hit_latency_set.print_from_last_end(num_hit);
  printf(" -other ");
  avg_other = other_latency_set.print_from_last_end(num_other);

  auto total_num = num_hit + num_other;
  auto temp = (avg_hit * num_hit + avg_other * num_other) / total_num;
  printf(
      "total avg lat: %.3lf (size: %lu, duration: %.5lf s, approx miss rate: "
      "%.4lf)\n",
      temp, total_num, 1.0 * (curr_time - time_cursor) / 1000 / 1000,
      1.0 * num_other / total_num);
  time_cursor = curr_time;
  return temp;
}

template <class Key, class Value>
void ConcurrentScalableCache<Key, Value>::PrintGlobalLat() {
  size_t num_hit = 0, num_other = 0;
  printf(" -hit ");
  auto avg_hit = hit_latency_set.print_tail(num_hit);
  printf(" -other ");
  auto avg_other = other_latency_set.print_tail(num_other);

  auto total_num = num_hit + num_other;
  printf("total avg lat: %.3lf (size: %lu, miss ratio: %.6lf)\n",
         (avg_hit * num_hit + avg_other * num_other) / total_num, total_num,
         1.0 * num_other / total_num);
  fflush(stdout);

  time_cursor = utils::NowMicros();
  hit_latency_set.reset();
  other_latency_set.reset();
}

template <class Key, class Value>
void ConcurrentScalableCache<Key, Value>::PrintStatus() {
  printf("cache status: \n");
  for (uint32_t i = 0; i < num_shards_; i++) {
    shards_[i]->PrintStatus();
  }
}

template <class Key, class Value>
void ConcurrentScalableCache<Key, Value>::Monitor() {
  printf("start monitoring ...\n");
  printf("wait stable interval: %d us (%.3lf s)\n",
         wait_stable_sleep_interval_us,
         1.0 * wait_stable_sleep_interval_us / 1000 / 1000);

  auto start_wait_stable = utils::NowMicros();
  double last_miss_ratio = 1;
  double miss_ratio = 0;
  size_t last_size = 0, size = 0;
  uint32_t wait_count = 0;

  // warm up
  while (!should_stop_) {
    printf("\ndata pass %lu\n", print_step_counter++);
    PrintMissRatio(miss_ratio);
    PrintStepLat();
    if (last_size >= size) {
      if (last_miss_ratio <= miss_ratio) {
        wait_count++;
      }
      if (wait_count >= wait_stable_threshold) {
        printf("- miss ratio = %.5lf -> %.5lf, with m_size = %lu (max = %lu)\n",
               last_miss_ratio, miss_ratio, size, max_size_);
        fflush(stdout);
        break;
      }
    }
    last_size = size;
    size = get_size();
    printf("- miss ratio = %.5lf -> %.5lf, with m_size = %lu (max = %lu)\n",
           last_miss_ratio, miss_ratio, size, max_size_);
    fflush(stdout);
    last_miss_ratio = miss_ratio;
    usleep(wait_stable_sleep_interval_us);
  }

  printf("\nfirst wait stable\n");
  printf("clear stat for next stage:\n");
  PrintGlobalLat();  // with clear inside

  auto wait_stable_duration = utils::NowMicros() - start_wait_stable;
  printf("\nwait stable spend time: %lf s\n",
         1.0 * wait_stable_duration / 1000 / 1000);

  while (!should_stop_) {
    printf("\ndata pass %lu\n", print_step_counter++);
    sleep(1);
    PrintMissRatio();
    PrintStepLat();
  }
  return;
}

// template <class Key, class Value>
// void ConcurrentScalableCache<Key, Value>::FrozenMonitor() {
//   printf("start monitoring ...\n");
//   printf("wait stable internal: %d us (%.1lf s)\n",
//          wait_stable_sleep_interval_us,
//          wait_stable_sleep_interval_us * 1.0 / 1000 / 1000);
//   printf("add fast performance threshold %.2lf for construction & frozen\n",
//          fast_performance_threshold);

// WAIT_STABLE:
//   auto start_wait_stable_time = utils::NowMicros();
//   double last_miss_ratio = 1, miss_ratio = 0;

//   double performance = 0;
//   size_t throughput_step = 0;
//   size_t last_size = 0, size = 0;
//   stop_sample_stat = true;
//   int wait_count = 0;

//   printf("\n* start observation *\n");
//   while (!should_stop_) {
//     printf("data pass %lu\n", print_step_counter++);
//     PrintMissRatio(miss_ratio);
//     PrintStepLat();

//     if (last_size >= size) {                // Cache is full
//       if (last_miss_ratio <= miss_ratio) {  // The miss ratio is
//       non-decreasing
//         wait_count++;
//         if (wait_count >=
//             wait_stable_threshold) {  // Waiting too long, stop filling
//             process
//           printf("miss ratio = %.5lf -> %.5lf, with m_size = %lu (max =
//           %lu)\n",
//                  last_miss_ratio, miss_ratio, size, max_size_);
//           fflush(stdout);
//           break;
//         }
//       }
//     }

//     // Update size & miss ratio
//     last_size = size;
//     size = get_size();
//     printf("- miss ratio = %.5lf -> %.5lf, with m_size = %lu (max = %lu)\n",
//            last_miss_ratio, miss_ratio, size, max_size_);
//     fflush(stdout);
//     last_miss_ratio = miss_ratio;  // Update miss ratio
//     usleep(wait_stable_sleep_interval_us);
//   }
//   printf("\ncache is stable\n");

//   // Message for end filling
//   if (beginning_flag_) {
//     printf("first wait stable && clear stat for next stage\n");
//     PrintGlobalLat();
//     beginning_flag_ = false;
//   }

//   // cache is stable
//   printf("wait stable spend time: %.4lf s\n",
//          1.0 * (utils::NowMicros() - start_wait_stable_time) / 1e6);

//   printf("end observation\n");

//   printf("start to search\n");
//   auto start_search_time = utils::NowMicros();

//   // Get curve
//   if (!should_stop_) {
//     shards_[0]->GetCurve(should_stop_);
//   }

//   // calculate optimal fast_cache's size ratio
//   double best_avg = 1000, best_size = 0;
//   double FC_hit_lat = 0, DC_hit_lat = 0, miss_lat = 0, disk_lat = 0;
//   double frozen_avg = 0, frozen_miss = 0, best_option_miss = 0;

//   while (other_latency_set.size_from_last_end() < 5 && !should_stop_) {
//     usleep(wait_stable_sleep_interval_us);
//   }
//   printf("draw curve\n");
//   printf("data pass %lu\n", print_step_counter++);

//   PrintMissRatio();

//   // become aware of DC hit latency and DC miss lat + disk latency
//   PrintStepLat(DC_hit_lat, miss_lat);

//   // first do 100% FC
//   for (uint32_t i = 0; i < num_shards_; i++) {
//     shards_[i]->ConstructTier();
//   }

//   PrintMissRatio();  // clear stat
//   PrintStepLat();    // clear latency

//   usleep(wait_stable_sleep_interval_us);
//   printf("data pass %lu\n", print_step_counter++);

//   // get one endpoint of FC miss ratio in curve -- when 100% FC but not used
//   // (only printed)
//   frozen_miss = PrintMissRatio();

//   // become awaare of FC latency and disk latency
//   frozen_avg = PrintStepLat(FC_hit_lat, disk_lat);
//   for (uint32_t i = 0; i < num_shards_; i++) {
//     shards_[i]->DeleteFastCache();
//   }

//   printf("FC hit lat: %.3lf us, frozen avg: %.3lf us, frozen miss: %.3lf\n",
//          FC_hit_lat, frozen_avg, frozen_miss);
//   fflush(stdout);

//   // calculate the best FC size ratio
//   auto container = shards_[0]->get_container();
//   for (int i = 0; i < container.size(); i++) {
//     auto t_size = container[i].size;
//     auto t_FC_hit = container[i].FC_hit;
//     auto t_miss = container[i].miss;
//     double avg = 0;

//     if (t_size < 0.01) {
//       avg = t_miss * miss_lat + (1 - t_miss) * DC_hit_lat;
//       t_size = 0;
//       printf("when baseline, avg from %.3lf to %.3lf\n", avg,
//              avg / (1 + fast_performance_threshold));
//       avg = avg / (1 + fast_performance_threshold);
//     } else {
//       // when t_size is large, we regard it as 100% FC
//       if (i == container.size() - 1 && t_size > 0.65) {
//         printf("regard t_size from %.3lf to %d\n", t_size, 1);
//         t_size = 1;
//       }
//       avg = t_FC_hit * FC_hit_lat + t_miss * (miss_lat + FC_hit_lat) +
//             (1 - t_FC_hit - t_miss) * (FC_hit_lat + DC_hit_lat);
//     }

//     if (avg < best_avg) {
//       best_avg = avg;
//       best_size = t_size;
//       best_option_miss = t_miss;
//       printf(
//           "(update) best avg: %.31f us, best size: %3.1f (w. FC_hit: %3.1f, "
//           "miss: %3.1f)\n",
//           best_avg, best_size, t_FC_hit, best_option_miss);
//     }
//   }

//   // Compare best "partially" frozen [0, 100%) with 100% Frozen
//   if (best_avg > frozen_avg) {
//     printf("(update) best avg: %.31f us, best size: %3.1f\n",
//     frozen_avg, 1.0); best_avg = frozen_avg; best_size = 1.0;
//   }

//   shards_[0]->get_container().clear();
//   printf("\n search spend time: %lf s\n",
//          1.0 * (utils::NowMicros() - start_search_time) / 1e6);
//   printf("profiling best size: %.3lf\n", best_size);

//   printf("end search\n");

//   if (!should_stop_ && best_size < 0.05 /* 0.05 is a magic number*/) {
//     // not suitable for fast cache.
//     sleep_threshold *= 8;  // 8 is a magic number
//     printf("sleep threshold increase to %u\n", sleep_threshold);
//     printf("sleep %u s\n", sleep_threshold);
//     fflush(stdout);
//     for (int i = 0; i < sleep_threshold; i++) {
//       if (should_stop_) break;
//       sleep(1);
//       printf("data pass %lu\n", print_step_counter++);
//       PrintMissRatio();
//       PrintStepLat();
//     }
//     if (!should_stop_) goto WAIT_STABLE;
//   }

// CONSTRUCT:
//   auto start_construct_time = utils::NowMicros();
//   printf("start construct fast cache\n");
//   request_latency_set.reset();

//   printf("find median avg lat of baseline:");
//   while (!should_stop_ && GetStepSize() < 100) {
//     sleep(1);
//   }
//   printf("data pass %lu\n", print_step_counter++);
//   PrintMissRatio();

//   uint64_t construct_step = 0;
//   baseline_performance = PrintStepLat(construct_step);
//   double baseline_performance_with_threshold =
//       baseline_performance / (1 + fast_performance_threshold);

//   printf("FC compare with baseline metric: %.3lf\n", baseline_performance);
//   printf("FC compare with baseline metric with threshould: %.3lf\n",
//          baseline_performance_with_threshold);

//   // enable request_latency_set
//   stop_sample_stat = false;

//   usleep(wait_stable_sleep_interval_us);
//   double baseline_metric = request_latency_set.print_tail();

//   // call the construct function for each shard
//   for(uint32_t i = 0; i <
//   if (double_is_equal(best_size, 1)) {
//     shard_->ConstructTier();
//   } else {
//     shard_->ConstructFastCache(best_size);
//   }
//   shard_->get_stats()->ResetCursor();
//   request_latency_set.reset();

//   printf("try query\n");
//   fflush(stdout);
//   double monitor_time = 500;
//   usleep(monitor_time);

//   // check if it is not suitable for FH and DeleteFastCache
//   shard_->get_stats()->PrintStep();
//   performance = request_latency_set.print_tail();

//   // If the latency is larger than the baseline plus threshold, then
//   // DeleteFastCache(fail)
//   uint32_t pass_count = 0;
//   bool failed = false;
//   while (!should_stop_ && !failed) {
//     if (pass_count >= pass_threshold) {
//       break;
//     }

//     if (performance > baseline_metric / (1 + fast_performance_threshold)) {
//       shard_->DeleteFastCache();
//       request_latency_set.reset();
//       usleep(monitor_time);
//       printf("baseline metric (update):\n");
//       baseline_metric = request_latency_set.print_tail();
//       failed = true;
//     }

//     printf("pass %d end\n", pass_count++);
//     fflush(stdout);

//     printf("data pass %lu\n", print_step_counter++);
//     PrintMissRatio();
//     uint64_t temp_step = 0;
//     PrintStepLat(temp_step);
//     construct_step += temp_step;
//   }

//   printf("construct step: %lu\n", construct_step);
//   if (failed) {
//     goto WAIT_STABLE;
//   }

//   printf("construct time:%lf s\n",
//          1.0 * (utils::NowMicros() - start_construct_time) / (1e6));
//   printf("end construct\n");

//   // disable request_latency_set
//   stop_sample_stat = true;

//   // start of frozen run after construction, and monitor it.
//   auto start_query_time = utils::NowMicros();
//   printf("start frozen\n");

//   double check_time = check_sleep_internal_us;
//   printf("check interval: %.3lf\n", check_time / (1e6));

//   double performance_depletion = drop_threshold;
//   bool first_flag = true;
//   uint64_t sum_step = 0;
//   uint64_t now_step = 0;

//   uint64_t baseline_step = 0;
//   uint64_t temp_step = 0;
//   while (!should_stop_) {
//     do {
//       usleep(check_time);
//     } while (GetStepSize() < 50 && !should_stop_);

//     printf("data pass %lu\n", print_step_counter++);
//     PrintFrozenStat();
//     performance = PrintStepLat(temp_step);
//     if (first_flag) {
//       baseline_step = temp_step;
//       first_flag = false;
//     }

//     auto delta = (baseline_performance_with_threshold - performance) /
//                  baseline_performance_with_threshold * temp_step /
//                  baseline_step;
//     performance_depletion += delta;

//     if (performance_depletion <= 0) {
//       printf("depleted: %.3lf <= 0\n", performance_depletion);
//       sleep_threshold *= 8;
//       printf("sleep threshold increase to %u\n", sleep_threshold);
//       goto DECONSTRUCT;
//     } else {
//       printf("not depleted: %.31f > 0\n", performance_depletion);
//     }

//     sum_step += temp_step;
//     now_step += temp_step;

//     if (sum_step > construct_step * frozen_threshold) {
//       // periodic reconstruction of FH
//       printf(
//           "after %lu frozen step (> %d * %lu = %lu), need periodlically "
//           "refresh!\n",
//           sum_step, frozen_threshold, construct_step,
//           construct_step * frozen_threshold);
//       shard_->DeleteFastCache();

//       sleep(1);

//       if (sleep_threshold >= 2) sleep_threshold /= 2;
//       printf("perform well, sleep threshold decrease into %u\n",
//              sleep_threshold);
//       goto CONSTRUCT;
//     } else if (now_step > construct_step) {
//       // this is a round to check whether need to deconstruct. If
//       // performance_depletion > drop_threshold, it means fast cache still
//       // outperforms baseline.
//       //
//       // so we can continue to run fast cache, but need to reset
//       // performance_depletion to drop_threshold.
//       //
//       // or we will not be able to beaware of the performance degradation
//       till
//       // deplete all benefits.
//       if (performance_depletion > drop_threshold) {
//         printf("performance depletion set to %d after %lu step for a
//         round!\n",
//                drop_threshold, now_step);
//         performance = drop_threshold;
//         now_step = 0;
//       } else {
//         printf("after %lu frozen step (~ %lu * %lu), need to refresh!\n",
//                sum_step, sum_step / construct_step, construct_step);
//         shard_->DeleteFastCache();
//         sleep(1);
//         goto CONSTRUCT;
//       }
//     }
//     fflush(stdout);
//   }

// DECONSTRUCT:
//   printf("frozen duration time: %lf\n",
//          1.0 * (utils::NowMicros() - start_query_time) / 1e6);
//   shard_->DeleteFastCache();
//   printf("end frozen\n");

//   if (!should_stop_) {
//     printf("sleep: %u s\n", sleep_threshold);
//     fflush(stdout);
//     for (int i = 0; i < sleep_threshold; i++) {
//       sleep(1);
//       printf("data pass: %lu\n", print_step_counter++);
//       PrintMissRatio();
//       PrintStepLat();

//       if (should_stop_) break;
//     }
//   }

//   if (!should_stop_) {
//     printf("go back to wait stable\n");
//     goto WAIT_STABLE;
//   }
//   printf("end mointor\n");
// }

template <class Key, class Value>
void ConcurrentScalableCache<Key, Value>::Stop() {
  should_stop_ = true;
}

}  // namespace kvcache

#endif