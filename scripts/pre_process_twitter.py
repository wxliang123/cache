def process_file(in_fname, out_fname):
    try:
        with open(in_fname, "r") as source_file, open(
            out_fname, "w"
        ) as destination_file:
            count = 0
            for line in source_file:
                columns = line.strip().split(",")
                if len(columns) == 7:
                    destination_file.write(columns[1] + "," + columns[5] + "\n")
                else:
                    print(count, "Skipping line:", line.strip())
                count = count + 1
    except Exception as e:
        print("An error occurred:", str(e))


process_file(
    "/home/wxl/Projects/KVCache/cache/twitter/cluster17_0",
    "/home/wxl/Projects/KVCache/cache/twitter/cluster17_0_v3",
)

# process_file("/home/wxl/Projects/KVCache/cache/twitter/cluster17_1", "/home/wxl/Projects/KVCache/cache/twitter/cluster17_1_v2")

# process_file("/home/wxl/Projects/KVCache/cache/twitter/cluster31_1", "/home/wxl/Projects/KVCache/cache/twitter/cluster31_1_v2")
