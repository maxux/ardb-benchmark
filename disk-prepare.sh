#!/bin/bash
echo "[+] loading disks list"
disks=$(lsblk -n -o KNAME,TYPE | grep disk | egrep -v 'sda|sdb' | awk '{ print $1 }' | xargs)

echo "[+] checking disks"
for disk in $disks; do
    if grep -q /dev/$disk /proc/mounts; then
        echo "[-] /dev/$disk: still mounted"
        exit 1
    fi
done

echo "[+] cleaning disks"
for disk in $disks; do
    echo "[+] $disk: wiping"
    dd if=/dev/zero of=/dev/$disk bs=1M count=1 2> /dev/null
done

echo "[+] creating filesystems"
for disk in $disks; do
    echo -n "[+] $disk: "
    mkfs.btrfs -K /dev/$disk | grep UUID: | awk '{ print $2 }'
done

echo "[+] mounting disks"

ssd=0
hdd=0
smr=0
nvme=0

for disk in $disks; do
    type=$(lsblk -n -d -o MODEL /dev/$disk)
    if [[ "$type" == *"Maximus"* ]]; then
        target=/mnt/ssd-$ssd
        echo "[+] $disk: mounting to: $target"
        mkdir -p $target
        mount /dev/$disk $target
        ssd=$(($ssd + 1))
    fi

    if [[ "$type" == *"ST3100"* ]] || [[ "$type" == *"WDC WD"* ]]; then
        target=/mnt/hdd-$hdd
        echo "[+] $disk: mounting to: $target"
        mkdir -p $target
        mount /dev/$disk $target
        hdd=$(($hdd + 1))
    fi

    if [[ "$type" == *"ST8000"* ]]; then
        target=/mnt/smr-$smr
        echo "[+] $disk: mounting to: $target"
        mkdir -p $target
        mount /dev/$disk $target
        smr=$(($smr + 1))
    fi

    if [[ "$type" == *"INTEL"* ]]; then
        target=/mnt/nvme-$nvme
        echo "[+] $disk: mounting to: $target"
        mkdir -p $target
        mount /dev/$disk $target
        nvme=$(($nvme + 1))
    fi
done
