
#ifndef KVCACHE_BENCHMARK_H
#define KVCACHE_BENCHMARK_H

#include <stdint.h>
#include <sys/syscall.h>  // For __NR_gettid
#include <unistd.h>       // For 'syscall()'

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "cache/options.h"
#include "cache/scalable_cache.h"
#include "cache/statistics.h"
#include "origin_frozenhot/hhvm_scalable_cache.h"
#include "properties.h"
#include "trace.h"

namespace kvcache {

// std::atomic<uint64_t> op_count = {0};

void SetCPUAffinity(int core) {
  printf("client coreid: %d\n", core);
  fflush(stdout);
  cpu_set_t mask;
  CPU_ZERO(&mask);
  CPU_SET(core, &mask);
  if (sched_setaffinity(syscall(__NR_gettid), sizeof(mask), &mask) != 0) {
    printf("Failed to set thread affinity!");
    exit(0);
  }
}

void BusySleep(std::chrono::nanoseconds t) {
  auto end = std::chrono::steady_clock::now() + t;
  while (std::chrono::steady_clock::now() < end) {
    ;
  }
}

class Benchmark {
 public:
  Benchmark(kvcache::Properties& props) : cache_(nullptr), trace_(nullptr) {
    auto cache = props.GetProperty("name");
    capacity_ = atoi(props.GetProperty("capacity").c_str());
    num_shards_ = atoi(props.GetProperty("shards").c_str());

    CacheType type = CacheType::LRU;
    if (!cache.compare("frozenhot_cache")) {
      enable_frozen_hot_ = true;
      FH_cache_.reset(
          new tstarling::ConcurrentScalableCache<uint64_t,
                                                 std::shared_ptr<std::string>>(
              capacity_, num_shards_, 20 /* rebuild freq */));
    } else {
      if (!cache.compare("fifo_cache")) {
        type = CacheType::FIFO;
      } else if (!cache.compare("lru_cache")) {
        type = CacheType::LRU;
      } else if (!cache.compare("group_cache")) {
        type = CacheType::GROUP;
      } else if (!cache.compare("async_cache")) {
        type = CacheType::ASYNC;
      } else if (!cache.compare("segment_cache")) {
        type = CacheType::SEGMENT;
      } else {
        std::cout << "Wrong cache name!" << std::endl;
        exit(0);
      }
      cache_.reset(
          new ConcurrentScalableCache<uint64_t, std::shared_ptr<std::string>>(
              capacity_, num_shards_, type));
    }

    num_requests_ = atoi(props.GetProperty("requests").c_str());
    num_threads_ = atoi(props.GetProperty("threads").c_str());

    small_granularity_ = num_threads_;
    large_granularity_ = num_threads_ * 1000;

    disk_latency_ = atoi(props.GetProperty("disk_latency").c_str());

    trace_.reset(new Trace());

    auto path = props.GetProperty("path");
    auto trace = props.GetProperty("trace");
    if (!trace.compare("zipf")) {
      trace_->LoadZipf(path, num_requests_);
    } else if (!trace.compare("twitter")) {
      trace_->LoadTwitter(path, num_requests_);
    }

    num_requests_ = trace_->get_size();
  }

  void Run() {
    printf("start running...\n");
    uint64_t total_requests = num_requests_;
    printf("capaicty: %ld, num. shards: %ld\n", capacity_, num_shards_);
    printf("num. requests: %ld\n", total_requests);
    printf("small granularity: %lu and large granularity: %lu\n",
           small_granularity_, large_granularity_);

    /**
     * NUMA node0 CPU(s): 0-27, 56-83
     * NUMA node1 CPU(s): 28-55, 84-111
     */
    auto start_time = utils::NowMicros();
    std::vector<std::thread> client_vtc;
    const uint64_t num_requests_per_client = total_requests / num_threads_;
    for (uint64_t i = 0; i < num_threads_; i++) {
      uint64_t start = i * num_requests_per_client;
      uint64_t core_id = 29 /* cpu core id in node 1 */ + i;
      if (core_id > 55) {
        core_id = core_id + 28;
      }
      client_vtc.emplace_back(
          std::thread(BGWork, this, num_requests_per_client, core_id, start));
    }

    auto func = [&](int core_id) { this->StartMonitor(core_id); };
    std::thread monitor(func, 28);

    for (uint64_t i = 0; i < num_threads_; i++) {
      client_vtc[i].join();
    }

    auto running_duration = (utils::NowMicros() - start_time);

    if (enable_frozen_hot_) {
      FH_cache_->monitor_stop();
    } else {
      cache_->Stop();
    }

    monitor.join();

    printf("\n");
    printf("running time: %.4lf (s)\n", 1.0 * (running_duration) / (1e6));
    if (enable_frozen_hot_) {
      FH_cache_->PrintGlobalLat();
    } else {
      cache_->PrintGlobalLat();
      cache_->PrintStatus();
    }
  }

  void Print() {}

 private:
  static void BGWork(void* arg, uint64_t num_requests, int core_id,
                     uint64_t start) {
    reinterpret_cast<Benchmark*>(arg)->DelegateClient(num_requests, core_id,
                                                      start);
  }

  void DelegateClient(uint64_t num_requests, int core_id, uint64_t start) {
    SetCPUAffinity(core_id);
    if (enable_frozen_hot_) {
      Work_FH_Cache(num_requests, core_id, start);
    } else {
      Work_Cache(num_requests, core_id, start);
    }
  }

  void Work_FH_Cache(uint64_t num_requests, int core_id, uint64_t start) {
    FH_cache_->thread_init(core_id);
    uint64_t offset = start;

    uint64_t hit_count = 0, lookup_count = 0, insert_count = 0,
             delete_count = 0, other_count = 0;
    std::chrono::_V2::system_clock::time_point start_of_loop;
    for (uint64_t i = 0; i < num_requests; i++) {
      auto req = trace_->Get(offset);
      auto key = req.key;
      bool ret_flag = false;
      if ((!FH_cache_->stop_sample_stat && i % small_granularity_ == 0) ||
          (FH_cache_->stop_sample_stat && i % large_granularity_ == 0))
        start_of_loop = SSDLOGGING_TIME_NOW;

      // Lookup
      if (Trace::OpType::lookup == req.op_type ||
          Trace::OpType::get == req.op_type) {
        std::shared_ptr<std::string> r_value;
        if (FH_cache_->find(r_value, key)) {
          ret_flag = true;
          hit_count++;
        } else {
          BusySleep(std::chrono::microseconds(disk_latency_));
          std::shared_ptr<std::string> w_value(new std::string(1, 'a'));
          FH_cache_->insert(key, w_value);
        }
        lookup_count++;
      }

      // Insert
      else if (Trace::OpType::insert == req.op_type ||
               Trace::OpType::set == req.op_type) {
        std::shared_ptr<std::string> w_value(new std::string(1, 'a'));
        FH_cache_->insert(key, w_value);
        insert_count++;
      }

      // Delete
      else if (Trace::OpType::delete_ == req.op_type) {
        FH_cache_->delete_key(key);
        delete_count++;
      }

      // Other
      else {
        std::shared_ptr<std::string> r_value;
        if (FH_cache_->find(r_value, key)) {
          ret_flag = true;
          hit_count++;
        } else {
          BusySleep(std::chrono::microseconds(disk_latency_));
          std::shared_ptr<std::string> w_value(new std::string(1, 'a'));
          FH_cache_->insert(key, w_value);
        }
        other_count++;
      }

      // Sample stat
      if (!FH_cache_->stop_sample_stat && (i % small_granularity_ == 0)) {
        auto duration =
            SSDLOGGING_TIME_DURATION(start_of_loop, SSDLOGGING_TIME_NOW);
        tstarling::req_latency_[key % num_shards_].insert(duration);
        if (i % large_granularity_ == 0) {
          if (ret_flag)
            tstarling::total_hit_latency_.insert(duration);
          else
            tstarling::total_other_latency_.insert(duration);
        }
      } else if (FH_cache_->stop_sample_stat && (i % large_granularity_ == 0)) {
        auto duration =
            SSDLOGGING_TIME_DURATION(start_of_loop, SSDLOGGING_TIME_NOW);
        if (ret_flag)
          tstarling::total_hit_latency_.insert(duration);
        else {
          tstarling::total_other_latency_.insert(duration);
        }
      }

      offset++;
    }  // traverse requests
    printf(
        "core id: %d, lookup count: %ld, insert count:%ld, delete count: "
        "%ld, other count: %ld, hit count: %ld (%.2lf)\n",
        core_id, lookup_count, insert_count, delete_count, other_count,
        hit_count, 1.0 * hit_count / lookup_count);
  }

  void Work_Cache(uint64_t num_requests, int core_id, uint64_t start) {
    uint64_t offset = start;

    uint64_t hit_count = 0, lookup_count = 0, insert_count = 0,
             delete_count = 0, other_count = 0;
    uint64_t start_of_loop = 0;
    for (uint64_t i = 0; i < num_requests; i++) {
      auto req = trace_->Get(offset);
      auto key = req.key;
      std::shared_ptr<std::string> value;
      bool ret_flag = false;
      if ((!cache_->stop_sample_stat && i % small_granularity_ == 0) ||
          (cache_->stop_sample_stat && i % large_granularity_ == 0)) {
        start_of_loop = utils::NowMicros();
      }

      // Lookup
      if (Trace::OpType::lookup == req.op_type ||
          Trace::OpType::get == req.op_type) {
        if (cache_->Lookup(key, value)) {
          ret_flag = true;
          hit_count++;
        } else {
          BusySleep(std::chrono::microseconds(disk_latency_));
          value = std::make_shared<std::string>(std::to_string(key));
          cache_->Insert(key, value);
        }
        lookup_count++;
      }

      // Insert
      else if (Trace::OpType::insert == req.op_type ||
               Trace::OpType::set == req.op_type) {
        value = std::make_shared<std::string>(std::to_string(key));
        cache_->Insert(key, value);
        insert_count++;
      }

      // Delete
      else if (Trace::OpType::delete_ == req.op_type) {
        cache_->Erase(key);
        delete_count++;
      }

      // Other
      else {
        if (cache_->Lookup(key, value)) {
          ret_flag = true;
          hit_count++;
        } else {
          BusySleep(std::chrono::microseconds(disk_latency_));
          value = std::make_shared<std::string>(std::to_string(key));
          cache_->Insert(key, value);
        }
        other_count++;
      }

      if (!cache_->stop_sample_stat && i % small_granularity_ == 0) {
        auto duration = utils::NowMicros() - start_of_loop;
        request_latency_set[key % num_shards_].insert(duration);
        if (i % large_granularity_ == 0) {
          if (ret_flag) {
            hit_latency_set.insert(duration);
          } else {
            other_latency_set.insert(duration);
          }
        }
      } else if (cache_->stop_sample_stat && (i % large_granularity_ == 0)) {
        auto duration = utils::NowMicros() - start_of_loop;
        if (ret_flag) {
          hit_latency_set.insert(duration);
        } else {
          other_latency_set.insert(duration);
        }
      }

      offset++;
    }  // traverse requests
    printf(
        "core id: %d, lookup count: %ld, insert count: %ld, delete count: "
        "%ld, other count: %ld, hit count: %ld (%.2lf)\n",
        core_id, lookup_count, insert_count, delete_count, other_count,
        hit_count, 1.0 * hit_count / lookup_count);
  }

  void StartMonitor(int core_id) {
    SetCPUAffinity(core_id);
    if (enable_frozen_hot_) {
      FH_cache_->FastHashMonitor();
    } else {
      cache_->Monitor();
    }
  }

 private:
  std::unique_ptr<
      ConcurrentScalableCache<key_type, std::shared_ptr<std::string>>>
      cache_;

  std::unique_ptr<tstarling::ConcurrentScalableCache<
      key_type, std::shared_ptr<std::string>>>
      FH_cache_;

  std::unique_ptr<Trace> trace_;

  bool enable_frozen_hot_ = false;

  uint64_t large_granularity_;
  uint64_t small_granularity_;

  uint64_t capacity_;
  uint64_t num_shards_;
  uint64_t num_requests_;
  uint64_t num_threads_;
  uint64_t disk_latency_;
};

}  // namespace kvcache

#endif