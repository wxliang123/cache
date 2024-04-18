import os

cache_type_list = [
    # "lru_cache",
    "segment_cache"
]

num_threads = [
    # 16,
    32
]

traces = [
    # "workloada.spec",
    # "workloadb.spec",
    # "workloadc.spec",
    "workloadd.spec",
    # "workloade.spec",
    "workloadf.spec",
]

for thread in num_threads:
    for cache_type in cache_type_list:
        for trace in traces:
            output_file = (
                "/home/wxl/Projects/KVCache/cache/end-to-end/experiments/"
                + cache_type
                + "_th"
                + str(thread)
                + "_"
                + trace
            )
            file_path = (
                "/home/wxl/Projects/KVCache/cache/end-to-end/YCSB/workloads/" + trace
            )
            command = (
                "numactl --cpubind=1 --membind=1 /home/wxl/Projects/KVCache/cache/end-to-end/YCSB/build/ycsb -db rocksdb"
                + " -threads "
                + str(thread)
                + " -cache_type "
                + str(cache_type)
                + " -cache_size 10 -phase run"
                + " -P "
                + file_path
                + " > "
                + output_file
            )
            os.system("/home/wxl/Projects/KVCache/cache/end-to-end/scripts/reload.sh")
            print(command)
            os.system(command)
