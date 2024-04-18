
cmake_minimum_required(VERSION 3.22)
project(ycsb)

if(CMAKE_BUILD_TYPE STREQUAL "debug")
    set(CMAKE_CXX_FLAGS "-g")
else()
    set(CMAKE_CXX_FLAGS "-O3")
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20 -lpthread -fopenmp -Wno-volatile")

include_directories(.)

# leveldb
set(LEVELDB_DIR "/home/wxl/Projects/KVCache/cache/end-to-end/leveldb-1.23")
include_directories(${LEVELDB_DIR}/include)
set(LEVELDB_LIB "${LEVELDB_DIR}/build/libleveldb.a")
message("${LIBLEVELDB}")

# rocksdb
set(ROCKSDB_DIR "/home/wxl/Projects/KVCache/cache/end-to-end/rocksdb-8.10.2")
include_directories(${ROCKSDB_DIR}/include)
set(ROCKSDB_LIB "${ROCKSDB_DIR}/build/librocksdb.a")
message("${ROCKSDB_LIB}")

add_executable(ycsb "")
target_sources(ycsb
    PRIVATE
    "core/client.h"
    "core/const_generator.h"
    "core/core_workload.cc"
    "core/core_workload.h"
    "core/counter_generator.h"
    "core/db.h"
    "core/discrete_generator.h"
    "core/properties.h"
    "core/scrambled_zipfian_generator.h"
    "core/timer.h"
    "core/uniform_generator.h"
    "core/utils.h"
    "core/zipfian_generator.h"
    "db/a_leveldb.h"
    "db/a_rocksdb.h"
    "db/basic_db.h"
    "db/db_factory.cc"
    "db/db_factory.h"
    "ycsb.cc")
target_link_libraries(ycsb pthread ${LIBLEVELDB} ${ROCKSDB_LIB} snappy tbb lz4 bz2 zstd uring z)