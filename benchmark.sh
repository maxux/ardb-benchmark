#!/bin/bash
set -e

LASTTIME=$(stat /var/lib/apt/periodic/update-success-stamp | grep Modify | cut -b 9-)
LASTUNIX=$(date --date "$LASTTIME" +%s)
echo "[+] last apt-get update: $LASTTIME"

if [ $LASTUNIX -gt $(($(date +%s) - (3600 * 6))) ]; then
	echo "[+] skipping system update"
else
    echo "[+] updating system"
    apt-get update
fi

echo "[+] installing dependancies"
apt-get install -y build-essential git wget bzip2 cmake libsnappy-dev libbz2-dev unzip libhiredis-dev libssl-dev redis-tools

echo "[+] compiling benchmark"
gcc -o ardb-benchmark ardb-benchmark.c -W -Wall -O2 -pthread -I/usr/include/hiredis -lhiredis -lpthread -lssl -lcrypto

echo "[+] downloading ardb source code"
cd /opt
if [ ! -d ardb ]; then
    git clone -b v0.9.3 https://github.com/yinqiwen/ardb.git
fi

cd ardb
mkdir -p /opt/ardb-bin/

# cp ardb.conf /opt/ardb-bin/
# sed home /tmp/...

# for engine in rocksdb leveldb lmdb wiredtiger perconaft forestdb; do
#    storage_engine=$engine make
#    cp src/ardb-server /opt/ardb-bin/ardb-server-$engine
# done

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
    /tmp/ardb-benchmark/ardb-benchmark

    echo "[+] stopping ardb-$engine"
    redis-cli -p 16379 shutdown
    sleep 1
done
