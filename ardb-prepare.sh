#!/bin/bash
set -e
bintarget="/opt/ardb-bin"

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

echo "[+] building target and copying config"
mkdir -p ${bintarget}/

echo "[+] downloading ardb source code"
cd /opt

if [ ! -d ardb ]; then
    echo "[+] downloading ardb"
    git clone -b v0.9.3 https://github.com/yinqiwen/ardb.git

fi
cd ardb

for engine in rocksdb leveldb lmdb wiredtiger perconaft forestdb; do
   storage_engine=$engine make
   cp src/ardb-server ${bintarget}/ardb-server-$engine
done
