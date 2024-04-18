import os

zipf_ratio_list = [99, 95, 90, 85, 80]

query_ratio_list = [20, 50, 80, 100]

for zipf_ratio in zipf_ratio_list:
    if zipf_ratio == 90:
        for query_ratio in query_ratio_list:
            path = "/home/wxl/Projects/KVCache/cache/trace"
            command = (
                "/home/wxl/Projects/KVCache/cache/build/key_generator "
                + path
                + " "
                + str(zipf_ratio)
                + " "
                + str(query_ratio)
            )
            print(command)
            os.system(command)
    else:
        path = "/home/wxl/Projects/KVCache/cache/trace"
        query_ratio = 80
        command = (
            "/home/wxl/Projects/KVCache/cache/build/key_generator "
            + path
            + " "
            + str(zipf_ratio)
            + " "
            + str(query_ratio)
        )
        print(command)
        os.system(command)
