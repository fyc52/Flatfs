#!/bin/bash
sudo umount /mnt/bbssd
sudo rmmod flatfs
sudo insmod flatfs.ko
sudo mount -t flatfs /dev/loop8 /mnt/bbssd
cat /proc/mounts | grep flatfs
