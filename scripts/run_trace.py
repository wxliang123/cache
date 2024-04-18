import os

cache_type_list = [
    # "fifo_cache",
    # "lru_cache",
    "frozenhot_cache",
    # "segment_cache",
]

num_threads = [
    # 1,
    # 8,
    16,
    # 32,
]

shards = 1

num_reqests = 1000000000

disk_latency_list = [
    5,
    # 10,
    # 20
]

capacity_list = [
    10000000,
    # 100000000
]

trace_type_list = [
    # "zipf",
    "twitter"
]

zipf_traces = [
     # "zipf_100m_z90_r100",
     # "zipf_1000m_z90_r100",
     # "zipf_1000m_z80_r80",
     # "zipf_1000m_z85_r80",
     # "zipf_1000m_z90_r20",
     # "zipf_1000m_z90_r50",
     # "zipf_1000m_z90_r80",
     # "zipf_1000m_z90_r100",
     # "zipf_1000m_z95_r80",
     # "zipf_1000m_z99_r80",
]

twitter_traces = [
    # "cluster45_0_v2",
    # "cluster29_0_v2",
    "cluster15_0_v2",
    # "cluster31_1_v2",
    # "cluster37_0_v2",
]

for thread in num_threads:
    for cache_type in cache_type_list:
        for capacity in capacity_list:
            for trace_type in trace_type_list:
                for disk_latency in disk_latency_list: 
                    if trace_type == "zipf":
                        for trace in zipf_traces:
                            output_file = (
                                "/home/wxl/Projects/KVCache/cache/experiments/"
                                + cache_type
                                + "/"
                                + trace
                                + "_th"
                                + str(thread)
                                + "_lat"
                                + str(disk_latency)
                                + "_cap"
                                + str(int(capacity / 1000000))
                                + "m"
                            )
                            file_path = "/home/wxl/Projects/KVCache/cache/trace/" + trace
                            command = (
                                "numactl --cpubind=1 --membind=1 /home/wxl/Projects/KVCache/cache/build/main"
                                + " -name "
                                + cache_type
                                + " -capacity "
                                + str(capacity)
                                + " -shards "
                                + str(shards)
                                + " -requests "
                                + str(num_reqests)
                                + " -threads "
                                + str(thread)
                                + " -disk_latency "
                                + str(disk_latency)
                                + " -trace "
                                + trace_type
                                + " -path "
                                + file_path
                                + " > "
                                + output_file
                            )
                            print(command)
                            # os.system(command)
                    elif trace_type == "twitter":
                        for trace in twitter_traces:
                            output_file = (
                                "/home/wxl/Projects/KVCache/cache/experiments/"
                                + cache_type
                                + "/twitter/"
                                + trace
                                + "_th"
                                + str(thread)
                                + "_lat"
                                + str(disk_latency)
                                + "_cap"
                                + str(int(capacity / 1000000))
                                + "m"
                            )
                            file_path = "/home/wxl/Projects/KVCache/cache/twitter/" + trace
                            command = (
                                "numactl --cpubind=1 --membind=1 /home/wxl/Projects/KVCache/cache/build/main"
                                + " -name "
                                + cache_type
                                + " -capacity "
                                + str(capacity)
                                + " -shards "
                                + str(shards)
                                + " -requests "
                                + str(num_reqests)
                                + " -threads "
                                + str(thread)
                                + " -disk_latency "
                                + str(disk_latency)
                                + " -trace "
                                + trace_type
                                + " -path "
                                + file_path
                                + " > "
                                + output_file
                            )
                            print(command)
                            os.system(command)
