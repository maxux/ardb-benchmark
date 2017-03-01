#!/bin/bash

host="127.0.0.1"
base=33000
threads=2
clients=7

args=""

for i in $(seq 1 ${clients}); do
    port=$(($base + $i))

    for x in $(seq 1 ${threads}); do
        args="$args ${host}:${port}"
    done
done

echo "[+] benchmarking ${clients} clients with ${threads} threads"
./ardb-benchmark $args
