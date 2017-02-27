#!/bin/bash
echo "[+] loading disks list"
disks=$(lsblk -n -o KNAME,SIZE,MODEL,TYPE | grep disk | egrep -v 'sda|sdb')

echo "[+] erasing disks"
for disk in $(echo $disks | awk '{ print $1 }'); do
    echo "cleaning $disk"
done
