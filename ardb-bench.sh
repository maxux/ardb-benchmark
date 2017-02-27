#!/bin/bash
echo "[+] compiling benchmark"
gcc -o ardb-benchmark ardb-benchmark.c -W -Wall -O2 -pthread -I/usr/include/hiredis -lhiredis -lpthread -lssl -lcrypto

for engine in rocksdb leveldb lmdb wiredtiger perconaft forestdb; do


    echo "[+] ardb-server-$engine looks ready, run benchmark"
    /tmp/ardb-benchmark/ardb-benchmark # ...

done
