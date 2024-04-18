
#include <fstream>
#include <iostream>
#include <random>

#include "zipfian.h"

void GenerateZipfianKeys(std::string dir_path, uint64_t key_limit, uint64_t ops,
                         double zipf_ratio, double query_ratio) {
  std::string filename = dir_path + "/zipf_1000m" + "_z" +
                         std::to_string((int)(zipf_ratio * 100)) + "_r" +
                         std::to_string((int)(query_ratio * 100));
  printf("filename: %s\n", filename.c_str());
  printf("key limit: %ld\n", key_limit);
  printf("num requets: %ld\n", ops);
  printf("zipf ratio: %.2f\n", zipf_ratio);
  printf("query ratio: %.2f\n", query_ratio);

  std::fstream fs;
  fs.open(filename, std::ios::out);
  if (!fs.is_open()) {
    std::cout << "Failed to open " << filename << std::endl;
    exit(0);
  }
  ScrambledZipfianGenerator generator(1, key_limit, zipf_ratio);
  uint32_t get_count = 0;
  uint32_t put_count = 0;

  std::cout << "start to generate zipfian keys" << std::endl;
  for (uint64_t i = 0; i < ops; i++) {
    uint32_t r = rand() % 100;
    if (r < query_ratio * 100) {
      fs << "0 " << generator.Next() << std::endl;
      get_count++;
    } else {
      fs << "1 " << generator.Next() << std::endl;
      put_count++;
    }
    if ((i + 1) % 1000'000 == 0) {
      std::cout << (i + 1) / 1000'000 << " million" << std::endl;
    }
  }
  std::cout << "finished" << std::endl;
  std::cout << "get_count: " << get_count << std::endl;
  std::cout << "put_count: " << put_count << std::endl;
}

int main(int argc, char* argv[]) {
  if (argc != 4) {
    std::cerr << "Usage: " << argv[0]
              << " <output_directory_path> <zipf_ratio> <query_ratio>"
              << std::endl;
    return 1;
  }
  const char* dir_path = argv[1];
  double zipf_ratio = 1.0 * std::stoi(argv[2]) / 100;
  double query_ratio = 1.0 * std::stoi(argv[3]) / 100;
  const uint64_t key_limit = 1000'000'000ULL;  // 1000m
  const uint64_t ops = 1000'000'000ULL;

  GenerateZipfianKeys(dir_path, key_limit, ops, zipf_ratio, query_ratio);
}