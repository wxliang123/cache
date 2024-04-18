
#include <string.h>

#include <iostream>

#include "benchmark.h"
#include "properties.h"

void ParseCommandLine(int argc, const char* argv[], kvcache::Properties& props);
void UsageMessage(const char* command);
bool StringStartWith(const char* str1, const char* str2);

int main(const int argc, const char* argv[]) {
  kvcache::Properties props;
  ParseCommandLine(argc, argv, props);
  kvcache::Benchmark bench(props);
  bench.Print();
  bench.Run();
  bench.Print();
  return 0;
}

void ParseCommandLine(int argc, const char* argv[],
                      kvcache::Properties& props) {
  int index = 1;
  while (index < argc && StringStartWith(argv[index], "-")) {
    if (strcmp(argv[index], "-name") == 0) {
      index++;
      if (index >= argc) {
        break;
      }
      props.SetProperty("name", argv[index]);
      index++;

    } else if (strcmp(argv[index], "-capacity") == 0) {
      index++;
      if (index >= argc) {
        break;
      }
      props.SetProperty("capacity", argv[index]);
      index++;

    } else if (strcmp(argv[index], "-shards") == 0) {
      index++;
      if (index >= argc) {
        break;
      }
      props.SetProperty("shards", argv[index]);
      index++;

    } else if (strcmp(argv[index], "-requests") == 0) {
      index++;
      if (index >= argc) {
        break;
      }
      props.SetProperty("requests", argv[index]);
      index++;

    } else if (strcmp(argv[index], "-threads") == 0) {
      index++;
      if (index >= argc) {
        break;
      }
      props.SetProperty("threads", argv[index]);
      index++;

    } else if (strcmp(argv[index], "-disk_latency") == 0) {
      index++;
      if (index >= argc) {
        break;
      }
      props.SetProperty("disk_latency", argv[index]);
      index++;

    } else if (strcmp(argv[index], "-trace") == 0) {
      index++;
      if (index >= argc) {
        break;
      }
      props.SetProperty("trace", argv[index]);
      index++;

    } else if (strcmp(argv[index], "-path") == 0) {
      index++;
      if (index >= argc) {
        break;
      }
      props.SetProperty("path", argv[index]);
      index++;

    } else {
      break;
    }
  }

  if (index == 1 || index != argc) {
    UsageMessage(argv[0]);
    exit(0);
  }
}

void UsageMessage(const char* command) {
  std::cout << "Usage:" << command << " [options]" << std::endl;
  std::cout << "Options:" << std::endl;
  std::cout << " -name " << std::endl;
  std::cout << " -capacity" << std::endl;
  std::cout << " -requests" << std::endl;
  std::cout << " -threads" << std::endl;
  std::cout << " -disk_latency" << std::endl;
  std::cout << " -path" << std::endl;
}

bool StringStartWith(const char* str1, const char* str2) {
  return strncmp(str1, str2, strlen(str2)) == 0;
}
