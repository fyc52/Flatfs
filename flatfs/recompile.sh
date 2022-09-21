#!/bin/bash
sudo insmod flatfs.ko
sudo mount -t flatfs /dev/loop8 /mnt/bbssd
cat /proc/mounts | grep flatfs
