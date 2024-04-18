
#ifndef FH_CACHE_H
#define FH_CACHE_H

#include <tbb/concurrent_hash_map.h>

#include <vector>

#include "util.h"

namespace tstarling {

struct curve_data_node {
  double size_, FC_hit_, miss_;
  curve_data_node(double size = 0, double FC_hit = 0, double miss = 1)
      : size_(size), FC_hit_(FC_hit), miss_(miss) {}
};

template <class TKey, class TValue, class THash = tbb::tbb_hash_compare<TKey>>
class FHCache {
 public:
  virtual ~FHCache() {}

  virtual void thread_init(int tid) {}

  virtual void clear() {}

  virtual void snapshotKeys(std::vector<TKey>& keys) {}

  virtual size_t size() const = 0;

  virtual void evict_key() {}

  virtual void ResetStat() {}

  virtual void GetStat(size_t& hit, size_t& miss) {
    hit = fast_find_hit.load() + end_to_end_find_succ.load();
    miss = tbb_find_miss.load();

    if (!sample_flag) {
      hit *= sample_percentage;
      miss *= sample_percentage;
    }
    fast_find_hit = 0;
    tbb_find_miss = 0;
    end_to_end_find_succ = 0;
  }

  virtual void GetStat(size_t& FH_hit, size_t& O_hit, size_t& miss) {
    FH_hit = fast_find_hit.load();
    miss = tbb_find_miss.load();
    O_hit = end_to_end_find_succ.load();

    fast_find_hit = 0;
    tbb_find_miss = 0;
    end_to_end_find_succ = 0;
  }

  virtual bool find(TValue& ac, const TKey& key) = 0;

  virtual bool insert(const TKey& key, const TValue& value) = 0;

  virtual void delete_key(const TKey& key) {}

  // fast_find_hit_latency_ fh hit
  // tbb_find_miss_latency_ global miss
  // end_to_end_find_succ_latency_ fh miss, global hit
  virtual double _get_miss_ratio(size_t& total_access) {
    // global miss ratio
    total_access = fast_find_hit.load() + tbb_find_miss.load() +
                   end_to_end_find_succ.load();
    double temp;
    if (total_access == 0)
      temp = 1;
    else
      temp = tbb_find_miss.load() * 1.0 / total_access;
    fast_find_hit = 0;
    tbb_find_miss = 0;
    end_to_end_find_succ = 0;
    return temp;
  }

  virtual void reset_stat() {
    fast_find_hit = 0;
    tbb_find_miss = 0;
    end_to_end_find_succ = 0;
    insert_count = 0;

    fast_hit_cursor = 0;
    miss_cursor = 0;
    o_hit_cursor = 0;

    insert_cursor = 0;
    return;
  }

  virtual void reset_cursor() {
    fast_hit_cursor = fast_find_hit.load();
    miss_cursor = tbb_find_miss.load();
    o_hit_cursor = end_to_end_find_succ.load();
    insert_cursor = insert_count.load();
  }

  virtual void print_step() {
    auto fast_hit = fast_find_hit.load() - fast_hit_cursor;
    auto miss = tbb_find_miss.load() - miss_cursor;
    auto o_hit = end_to_end_find_succ.load() - o_hit_cursor;
    auto insert_num = insert_count.load() - insert_cursor;

    double temp, global_miss, total;
    total = fast_hit + insert_num + o_hit;
    if (total == 0) {
      temp = 1;
      global_miss = 1;
    } else {
      temp = 1 - fast_hit * 1.0 / total;
      global_miss = insert_num * 1.0 / total;
    }
    printf("miss ratio: %.5f / %.5f\n", temp, global_miss);
    printf(
        "fast find hit: %ld, global hit: %ld, global miss: %ld, total "
        "insert: "
        "%ld\n",
        fast_hit, o_hit, miss, insert_num);
    fflush(stdout);

    fast_hit_cursor = fast_hit;
    miss_cursor = miss;
    o_hit_cursor = o_hit;
    insert_cursor = insert_num;
  }

  virtual void print_step(double& FC_hit, double& global_miss) {
    auto fast_hit = fast_find_hit.load() - fast_hit_cursor;
    auto miss = tbb_find_miss.load() - miss_cursor;
    auto o_hit = end_to_end_find_succ.load() - o_hit_cursor;
    auto insert_num = insert_count.load() - insert_cursor;

    double temp, total;
    total = fast_hit + insert_num + o_hit;
    if (total == 0) {
      temp = 1;
      global_miss = 1;
    } else {
      temp = 1 - fast_hit * 1.0 / total;
      global_miss = insert_num * 1.0 / total;
    }
    printf("miss ratio: %.5f / %.5f\n", temp, global_miss);
    printf(
        "fast find hit: %ld, global hit: %ld, global miss: %ld, total "
        "insert: "
        "%ld\n",
        fast_hit, o_hit, miss, insert_num);
    fflush(stdout);

    FC_hit = 1 - temp;
  }

  virtual void get_step(double& FC_hit, double& global_miss) {
    auto fast_hit = fast_find_hit.load() - fast_hit_cursor;
    auto miss = tbb_find_miss.load() - miss_cursor;
    auto o_hit = end_to_end_find_succ.load() - o_hit_cursor;
    auto insert_num = insert_count.load() - insert_cursor;

    double temp, total;
    total = insert_num + fast_hit + o_hit;
    if (total == 0) {
      temp = 1;
      global_miss = 0;
    } else {
      temp = 1 - fast_hit * 1.0 / total;
      global_miss = miss * 1.0 / total;
    }

    FC_hit = 1 - temp;
  }

  virtual double print_reset_fast_hash() {
    printf("fast find hit: %ld, global hit: %ld, global miss: %ld\n",
           fast_find_hit.load(), end_to_end_find_succ.load(),
           tbb_find_miss.load());
    double temp, global_miss, total;
    total = fast_find_hit.load() + tbb_find_miss.load() +
            end_to_end_find_succ.load();
    if (total == 0) {
      temp = 1;
      global_miss = 1;
    } else {
      temp = 1 - fast_find_hit.load() * 1.0 / total;
      global_miss = tbb_find_miss.load() * 1.0 / total;
    }
    printf("miss ratio: %.5f / %.5f\n", temp, global_miss);
    fast_find_hit = 0;
    tbb_find_miss = 0;
    end_to_end_find_succ = 0;
    return temp;
  }

  virtual double print_fast_hash() {
    double temp, global_miss, total;
    total = fast_find_hit.load() + tbb_find_miss.load() +
            end_to_end_find_succ.load();
    if (total == 0) {
      temp = 1;
      global_miss = 1;
    } else {
      temp = 1 - fast_find_hit.load() * 1.0 / total;
      global_miss = tbb_find_miss.load() * 1.0 / total;
    }
    printf("miss ratio: %.5f / %.5f\n", temp, global_miss);
    printf("fast find hit: %ld, global hit: %ld, global miss: %ld\n",
           fast_find_hit.load(), end_to_end_find_succ.load(),
           tbb_find_miss.load());
    fflush(stdout);
    return temp;
  }

  /*
   * This is the construction function for the FH cache.
   * It will construct the cache with the given ratio.
   * DO NOT NEED to stop insert(), NEED to stop promotion
   * The way to achieve this goal is to think of the movement:
   * 1. ONE insert() will cause ONE evict(), use 'eviction counter' to count
   *    so the list will look like 'new_dc -> fc start -> ... -> fc end -> dc
   * start -> ... -> dc end' w/o fc part, dc is actually a FIFO list in this
   * stage
   * 2. if counter < DC size, but fc scan to end (with scan counter == FC
   * size), then cutoff from fc start and fc end; link new dc end <-> (old) dc
   * start
   * 3. if the eviction counter == DC size, then cutoff from fc start
   *    since it looks like 'new dc start -> ... -> new dc end -> fc start ->
   * ... -> fc end' now
   */

  virtual bool construct_ratio(double FC_ratio) { return false; }

  /*
   * This function only used for 100% FC construction
   * only one case, but also frozen the cache (not only metadata)
   * even after delete() happens, it will not permit any insert()
   */
  virtual bool construct_tier() { return false; }

  virtual void deconstruct() {}

  /*
   * It will fill 'curve_container' with {FC ratio, {FC hit, DC hit, DC miss}}
   * in order to plot the hit/miss ratio versus FC ratio graph
   * It is implemented differently
   * FIFO/LRU as an example:
   * 1. insert the marker, with the last access time = current time
   * 2. keep checking the movement counter to track marker's position
   *    and print FC hit, DC hit, DC miss
   * 3. movement counter is added by 1 ONLY WHEN
   *    a node older than marker get accessed
   *    and update this node's last access time
   */
  virtual bool get_curve(bool& should_stop) { return false; }
  virtual bool is_full() { return false; }

  inline bool sample_generator() {
    if (!sample_flag)
      return true;
    else
      return ((double)(ssdlogging::random::Random::GetTLSInstance()->Next()) /
              (double)(RAND_MAX)) < sample_percentage;
  }

  std::atomic<size_t> fast_find_hit{0};
  std::atomic<size_t> tbb_find_miss{0};
  std::atomic<size_t> end_to_end_find_succ{0};
  std::atomic<size_t> insert_count{0};

  size_t fast_hit_cursor{0}, miss_cursor{0}, o_hit_cursor{0}, insert_cursor{0};

  const double sample_percentage = 1.0 / 100;

  bool sample_flag = true;

  std::vector<curve_data_node> curve_container;

  std::atomic<int> promotion_counter{0};

  // Added by xiaoliang
  std::atomic<size_t> failed_count{0};
  std::atomic<size_t> update_count{0};
};

}  // namespace tstarling

#endif  // incl_FH_CACHE_H