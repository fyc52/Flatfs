sudo umount /mnt/bbssd
sudo rmmod flatfs
sudo insmod flatfs.ko
sudo mount -t flatfs /dev/nvme0n1 /mnt/bbssd
cat /proc/mounts | grep flatfs