echo "reload dataset"
rm -r /home/wxl/Projects/KVCache/cache/end-to-end/data/rocksdb/*
cp -r /home/wxl/Projects/KVCache/cache/end-to-end/backup/rocksdb_40g_directio/* /home/wxl/Projects/KVCache/cache/end-to-end/data/rocksdb
echo 3 > /proc/sys/vm/drop_caches
sleep 300