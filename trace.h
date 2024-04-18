
#ifndef KV_CACHE_TRACE_H
#define KV_CACHE_TRACE_H

#include <assert.h>

#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace kvcache {

using key_type = uint64_t;

unsigned int string_hash(std::string const& s) {
  const int p = 31;
  const int m = 1e9 + 9;
  unsigned long hash_value = 0;
  unsigned long p_pow = 1;
  for (char c : s) {
    hash_value = (hash_value + (c - '0' + 1) * p_pow) % m;
    p_pow = (p_pow * p) % m;
  }
  return hash_value;
}

class Trace {
 public:
  enum class OpType : uint8_t {
    none,

    // following operations are used in zipf workload
    lookup,
    insert,
    erase,

    //  following operations are used in twitter workload
    set,
    add,
    replace,
    append,
    prepend,
    cas,
    get,
    gets,
    delete_,
    incr,
    decr
  };

  struct Request {
    OpType op_type;
    key_type key;
    Request() : op_type(OpType::none), key(0) {}

    Request(OpType op_type, key_type key) : op_type(op_type), key(key) {}
  };

  Trace() : num_requests_(0), requests_(nullptr) {}
  ~Trace() {}

  void LoadZipf(std::string filename, const uint64_t num) {
    num_requests_ = num;
    requests_.reset(new Request[num_requests_]);

    printf("loading workload (%lu)...\n", num_requests_);

    std::fstream fs;
    fs.open(filename, std::ios::in);
    printf("open file: %s\n", filename.c_str());
    if (!fs.is_open()) {
      printf("failed to open file!\n");
      exit(0);
    }

    uint64_t tmp_opt = 0;
    uint64_t tmp_key = 0;
    uint64_t count = 0;
    auto start = std::chrono::high_resolution_clock::now();
    while (count < num_requests_ && fs >> tmp_opt >> tmp_key) {
      if (tmp_opt == 0) {
        requests_.get()[count].op_type = OpType::lookup;
      } else if (tmp_opt == 1) {
        requests_.get()[count].op_type = OpType::insert;
      } else {
        printf("wrong operation type!");
        exit(0);
      }
      requests_.get()[count].key = tmp_key;
      count++;
      if (count % 100000000 == 0) {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration<double>(end - start).count();
        start = end;
        printf("finished %ld (100m) in %.3lf s \n", count / 100000000,
               duration);
        fflush(stdout);
      }
    }
    printf("origin data size :%ld\n", count);
    if (num_requests_ > count) {
      num_requests_ = count;
    }
  }

  void LoadTwitter(std::string filename, const uint64_t num) {
    num_requests_ = num;
    requests_.reset(new Request[num_requests_]);

    printf("loading workload (%lu)...\n", num_requests_);

    std::string str;
    std::string op = "";
    std::ifstream infile;

    infile.open(filename);
    printf("open file: %s\n", filename.c_str());

    if (!infile.is_open()) {
      printf("failed to open file!\n");
      exit(0);
    }

    // std::hash<std::string> string_hash;
    uint64_t count = 0;
    auto start = std::chrono::high_resolution_clock::now();
    while (infile.good() && !infile.eof() && count < num_requests_) {
      getline(infile, str);
      if (str == "") continue;
      std::string space_delimiter = ",";
      std::vector<std::string> words{};
      words.clear();
      size_t pos = 0;
      while ((pos = str.find(space_delimiter)) != std::string::npos) {
        words.push_back(str.substr(0, pos));
        str.erase(0, pos + space_delimiter.length());
      }
      words.push_back(str.substr(0, pos));
      op = words[1];
      uint64_t hash_value = string_hash(words[0]);
      if (op.back() == '\r') {
        op.pop_back();
      }

      OpType opt;
      if (op == "get") {
        opt = OpType::get;
      } else if (op == "gets") {
        opt = OpType::gets;
      } else if (op == "set") {
        opt = OpType::set;
      } else if (op == "add") {
        opt = OpType::add;
      } else if (op == "replace") {
        opt = OpType::replace;
      } else if (op == "cas") {
        opt = OpType::cas;
      } else if (op == "append") {
        opt = OpType::append;
      } else if (op == "prepend") {
        opt = OpType::prepend;
      } else if (op == "delete") {
        opt = OpType::delete_;
      } else if (op == "incr") {
        opt = OpType::incr;
      } else if (op == "decr") {
        opt = OpType::decr;
      } else {
        std::cout << "error parameter: " << str << std::endl;
        exit(0);
      }
      requests_.get()[count] = Request(opt, hash_value);
      count++;
      if (count % 100000000 == 0) {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration<double>(end - start).count();
        start = end;
        printf("finished %ld (100m) in %.3lf s \n", count / 100000000,
               duration);
        fflush(stdout);
      }
    }
    infile.close();
    printf("origin data size :%ld\n", count);
    num_requests_ = count;
    printf("\n");
  }

  Request Get(uint64_t index) {
    assert(index < num_requests_);
    return requests_.get()[index];
  }

  uint64_t get_size() { return num_requests_; }

 private:
  uint64_t num_requests_;
  std::unique_ptr<Request> requests_;
};

}  // namespace kvcache

#endif