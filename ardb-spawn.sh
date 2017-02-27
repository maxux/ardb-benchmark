#!/bin/bash
echo "[+] disabling huge pages"
echo never > /sys/kernel/mm/transparent_hugepage/enabled

echo "[+] cleaning previous session"
redis-cli -p 16379 shutdown 2> /dev/null || true
rm -rf /tmp/ardb-home/*

for engine in rocksdb leveldb lmdb wiredtiger perconaft forestdb; do
    echo "[+] starting ardb-server-$engine"
    tmux new-sess -d -s ardb /opt/ardb-bin/ardb-server-$engine /opt/ardb-bin/ardb.conf

    while ! redis-cli -p 16379 ping > /dev/null 2>&1; do
        sleep 0.2
    done

    echo "[+] ardb-server-$engine looks ready, run benchmark"
    # ...

    echo "[+] stopping ardb-$engine"
    redis-cli -p 16379 shutdown
    sleep 1
done
