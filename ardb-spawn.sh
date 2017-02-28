#!/bin/bash
set -e

if [ "$1" == "" ]; then
    echo "[-] missing argument: disk type (will look to /mnt/[type]-X)"
    exit 1
fi

echo "[+] testing type target"
if ! ls /mnt/$1-* > /dev/null 2>&1; then
    echo "[-] nothing related to '$1' found in /mnt"
    exit 1
fi

type=$1

echo "[+] disabling huge pages"
echo never > /sys/kernel/mm/transparent_hugepage/enabled

echo "[+] killing previous sessions"
for engine in rocksdb leveldb lmdb wiredtiger perconaft forestdb; do
    pkill ardb-server-$engine || true
done
while ps aux | grep ardb-server | grep -v grep > /dev/null; do
    sleep 0.1
done

echo "[+] cleaning previous session"
rm -rf /mnt/${type}-*/*

# port 31000 -> rocksdb
# port 32000 -> leveldb
# port 33000 -> lmdb
# port 34000 -> wiredtiger
# port 35000 -> perconaft
# port 36000 -> forestdb

portbase=30000
for engine in rocksdb leveldb lmdb wiredtiger perconaft forestdb; do
    portbase=$(($portbase + 1000))
    port=$portbase

    for target in /mnt/${type}-*; do
        home=${target}/${engine}
        port=$(($port + 1))

        mkdir -p ${home}
        sed "s#__HOME__#${home}#g;s#__PORT__#${port}#g" ardb-benchmark.conf > ${home}/ardb.conf

        echo "[+] starting ardb-server-$engine"
        tmux new-sess -d -s ardb-${engine}-${target} /opt/ardb-bin/ardb-server-$engine ${home}/ardb.conf

        while ! redis-cli -p ${port} ping > /dev/null 2>&1; do
            sleep 0.2
        done

        echo "[+] ardb-server-$engine on ${target} looks ready"
    done
done
