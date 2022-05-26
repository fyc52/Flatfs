#!bin/bash
sudo umount /mnt/flatfs
sudo rmmod flatfs
sudo make clean
sudo make
