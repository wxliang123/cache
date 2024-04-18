//
//  ycsbc.cc
//  YCSB-C
//
//  Created by Jinglei Ren on 12/19/14.
//  Copyright (c) 2014 Jinglei Ren <jinglei@ren.systems>.
//

#include <stdint.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <future>
#include <iostream>
#include <string>
#include <vector>

#include "core/client.h"
#include "core/core_workload.h"
#include "core/timer.h"
#include "core/utils.h"
#include "db/db_factory.h"

using namespace std;

void UsageMessage(const char *command);
bool StrStartWith(const char *str, const char *pre);
string ParseCommandLine(int argc, const char *argv[], utils::Properties &props);

std::atomic<uint64_t> op_count = {0};
uint64_t start_micros = 0;

uint64_t NowMicros() {
  static constexpr uint64_t kUsecondsPerSecond = 1000000;
  struct ::timeval tv;
  ::gettimeofday(&tv, nullptr);
  return static_cast<uint64_t>(tv.tv_sec) * kUsecondsPerSecond + tv.tv_usec;
}

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

int DelegateClient(int core_id, ycsbc::DB *db, ycsbc::CoreWorkload *wl,
                   const int num_ops, bool is_loading) {
  SetCPUAffinity(core_id);
  ycsbc::Client client(*db, *wl);
  int oks = 0;
  for (int i = 0; i < num_ops; ++i) {
    if (is_loading) {
      oks += client.DoInsert();
    } else {
      oks += client.DoTransaction();
    }
    auto c = op_count++;
    if ((c + 1) % 1000000 == 0) {
      auto curr = NowMicros();
      printf("finished %ld ops, kops: %.2lf\n", (c + 1),
             1.0 * 1000 * 1e6 / (curr - start_micros));
      fflush(stdout);
      start_micros = curr;
    }
  }
  return oks;
}

int main(const int argc, const char *argv[]) {
  utils::Properties props;
  string file_name = ParseCommandLine(argc, argv, props);

  ycsbc::DB *db = ycsbc::DBFactory::CreateDB(props);
  if (!db) {
    cout << "Unknown database name " << props["dbname"] << endl;
    exit(0);
  }

  ycsbc::CoreWorkload wl;
  wl.Init(props);

  const int num_threads = stoi(props.GetProperty("threadcount", "1"));

  auto cache_type = props.GetProperty("cache_type");
  const uint64_t cache_size =
      (1ull << 30) * stoi(props.GetProperty("cache_size", "1024"));
  db->Init(cache_type, cache_size);
  vector<future<int>> actual_ops;
  int sum = 0;
  int total_ops = 0;

  auto phase = props.GetProperty("phase", "load");
  if (phase.compare("load") == 0) {
    // Loads data
    total_ops = stoi(props[ycsbc::CoreWorkload::RECORD_COUNT_PROPERTY]);
    start_micros = NowMicros();
    for (int i = 0; i < num_threads; ++i) {
      /**
       * NUMA node0 CPU(s): 0-27, 56-83
       * NUMA node1 CPU(s): 28-55, 84-111
       */
      int core_id = 28 + i;
      if (core_id > 55) {
        core_id += 28;
      }
      actual_ops.emplace_back(async(launch::async, DelegateClient, core_id, db,
                                    &wl, total_ops / num_threads, true));
    }
    assert((int)actual_ops.size() == num_threads);

    sum = 0;
    for (auto &n : actual_ops) {
      assert(n.valid());
      sum += n.get();
    }
    cout << "# Loading records:\t" << sum << endl;
  } else if (phase.compare("run") == 0) {
    // Peforms transactions
    actual_ops.clear();
    total_ops = stoi(props[ycsbc::CoreWorkload::OPERATION_COUNT_PROPERTY]);
    utils::Timer<double> timer;
    timer.Start();
    start_micros = NowMicros();
    for (int i = 0; i < num_threads; ++i) {
      /**
       * NUMA node0 CPU(s): 0-27, 56-83
       * NUMA node1 CPU(s): 28-55, 84-111
       */
      int core_id = 28 + i;
      if (core_id > 55) {
        core_id += 28;
      }
      actual_ops.emplace_back(async(launch::async, DelegateClient, core_id, db,
                                    &wl, total_ops / num_threads, false));
    }
    assert((int)actual_ops.size() == num_threads);

    sum = 0;
    for (auto &n : actual_ops) {
      assert(n.valid());
      sum += n.get();
    }
    double duration = timer.End();
    cout << "# Transaction throughput (KTPS)" << endl;
    cout << props["dbname"] << '\t' << file_name << '\t' << num_threads << '\t';
    cout << total_ops / duration / 1000 << endl;
  }

  db->Print();
  db->Close();
}

string ParseCommandLine(int argc, const char *argv[],
                        utils::Properties &props) {
  int argindex = 1;
  string filename;
  while (argindex < argc && StrStartWith(argv[argindex], "-")) {
    if (strcmp(argv[argindex], "-phase") == 0) {
      argindex++;
      if (argindex >= argc) {
        UsageMessage(argv[0]);
        exit(0);
      }
      props.SetProperty("phase", argv[argindex]);
      argindex++;
    } else if (strcmp(argv[argindex], "-cache_type") == 0) {
      argindex++;
      if (argindex >= argc) {
        UsageMessage(argv[0]);
        exit(0);
      }
      props.SetProperty("cache_type", argv[argindex]);
      argindex++;
    } else if (strcmp(argv[argindex], "-cache_size") == 0) {
      argindex++;
      if (argindex >= argc) {
        UsageMessage(argv[0]);
        exit(0);
      }
      props.SetProperty("cache_size", argv[argindex]);
      argindex++;
    } else if (strcmp(argv[argindex], "-threads") == 0) {
      argindex++;
      if (argindex >= argc) {
        UsageMessage(argv[0]);
        exit(0);
      }
      props.SetProperty("threadcount", argv[argindex]);
      argindex++;
    } else if (strcmp(argv[argindex], "-db") == 0) {
      argindex++;
      if (argindex >= argc) {
        UsageMessage(argv[0]);
        exit(0);
      }
      props.SetProperty("dbname", argv[argindex]);
      argindex++;
    } else if (strcmp(argv[argindex], "-host") == 0) {
      argindex++;
      if (argindex >= argc) {
        UsageMessage(argv[0]);
        exit(0);
      }
      props.SetProperty("host", argv[argindex]);
      argindex++;
    } else if (strcmp(argv[argindex], "-port") == 0) {
      argindex++;
      if (argindex >= argc) {
        UsageMessage(argv[0]);
        exit(0);
      }
      props.SetProperty("port", argv[argindex]);
      argindex++;
    } else if (strcmp(argv[argindex], "-slaves") == 0) {
      argindex++;
      if (argindex >= argc) {
        UsageMessage(argv[0]);
        exit(0);
      }
      props.SetProperty("slaves", argv[argindex]);
      argindex++;
    } else if (strcmp(argv[argindex], "-P") == 0) {
      argindex++;
      if (argindex >= argc) {
        UsageMessage(argv[0]);
        exit(0);
      }
      filename.assign(argv[argindex]);
      ifstream input(argv[argindex]);
      try {
        props.Load(input);
      } catch (const string &message) {
        cout << message << endl;
        exit(0);
      }
      input.close();
      argindex++;
    } else {
      cout << "Unknown option '" << argv[argindex] << "'" << endl;
      exit(0);
    }
  }

  if (argindex == 1 || argindex != argc) {
    UsageMessage(argv[0]);
    exit(0);
  }

  return filename;
}

void UsageMessage(const char *command) {
  cout << "Usage: " << command << " [options]" << endl;
  cout << "Options:" << endl;
  cout << "  -threads n: execute using n threads (default: 1)" << endl;
  cout << "  -db dbname: specify the name of the DB to use (default: basic)"
       << endl;
  cout << "  -P propertyfile: load properties from the given file. Multiple "
          "files can"
       << endl;
  cout << "                   be specified, and will be processed in the order "
          "specified"
       << endl;
}

inline bool StrStartWith(const char *str, const char *pre) {
  return strncmp(str, pre, strlen(pre)) == 0;
}
