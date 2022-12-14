sudo mkfs -t ext4 /dev/nvme0n1
sudo mount -t ext4 /dev/nvme0n1 /mnt/bbssd
sudo bash -c "echo 0 > /proc/sys/kernel/randomize_va_space"