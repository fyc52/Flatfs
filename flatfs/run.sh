sudo umount /mnt/bbssd
sudo rmmod flatfs
sudo insmod flatfs.ko
sudo mount -t flatfs /dev/loop3 /mnt/bbssd
cat /proc/mounts | grep flatfs