
cmake_minimum_required(VERSION 3.22)
project(cache)

if(CMAKE_BUILD_TYPE STREQUAL "debug")
    set(CMAKE_CXX_FLAGS "-g")
else()
    set(CMAKE_CXX_FLAGS "-O3")
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20 -pthread -Wno-volatile")

message("${CMAKE_CXX_FLAGS}")

option(KVCACHE_BUILD_TESTS "Build KVCache's unit tests" ON)

include_directories(.)

# spdlog
add_subdirectory("third_party/spdlog")

# clht
include_directories(./CLHT/include)
include_directories(./CLHT/external/include)
find_library(LIBCLHT clht PATHS ./CLHT/)
message("${LIBCLHT}")
find_library(LIBSSMEM ssmem PATHS ./CLHT/external/ssmem)
message("${LIBSSMEM}")

add_executable(main "")

target_sources(main
    PRIVATE
    "cache/async_cache.h"
    "cache/cache.h"
    "cache/fifo_cache.h"
    "cache/group_cache.h"
    "cache/lru_cache.h"
    "cache/lru_cache_shared_hash.h"
    "cache/options.h"
    "cache/scalable_cache.h"
    "cache/segment_cache.h"
    "cache/statistics.cc"
    "cache/statistics.h"
    "fast_hash/clht_hash.h"
    "fast_hash/fast_hash.h"
    "origin_frozenhot/FHCache.h"
    "origin_frozenhot/hhvm_lru_cache_FH.h"
    "origin_frozenhot/hhvm_scalable_cache.h"
    "origin_frozenhot/util.h"
    "benchmark.h"
    "properties.h"
    "trace.h"
    "main.cc")

target_link_libraries(main tbb spdlog ${LIBCLHT} ${LIBSSMEM})


if(KVCACHE_BUILD_TESTS)
  enable_testing()

  # This project is tested using GoogleTest
  add_subdirectory("third_party/googletest")

  function(kvcache_test test_file)
    get_filename_component(test_target_name "${test_file}" NAME_WE)

    add_executable("${test_target_name}" "")
    target_sources("${test_target_name}" 
        PRIVATE
          "cache/statistics.cc"
          "cache/statistics.h"
          "cache/cache.h"
          "${test_file}")
    target_link_libraries("${test_target_name}" tbb gmock gtest)
    
  endfunction(kvcache_test test_file)

  kvcache_test("cache/segment_cache_test.cc")
  kvcache_test("cache/lru_cache_test.cc")

endif(KVCACHE_BUILD_TESTS)

add_executable(key_generator "key_generator/key_generator.cc")
target_sources(key_generator 
    PRIVATE
      "key_generator/zipfian.h")