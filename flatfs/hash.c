#include <linux/buffer_head.h>
#include "flatfs_d.h"

// BKDR Hash Function
static inline unsigned long BKDRHash(unsigned long hash_key, unsigned long mask)
{
	unsigned long seed = 131;
	unsigned long hash = 0;
    int count = 0;
    char *str = (char *)&hash_key;

	while (count < 8) {
		hash = hash * seed + (*str++);
        count ++;
	}

	return (hash & mask);
}


sector_t hashfs_get_data_lba(struct super_block *sb, ino_t ino, sector_t iblock)
{
    __u64 hash_key = (ino<<32) | iblock;
    unsigned long value = BKDRHash(hash_key, ~(FFS_BLOCK_SIZE - 1));
    unsigned meta_block, meta_offset;
    unsigned long data_block;
    struct buffer_head * bh = NULL;
    __u64 *meta_entry;

    meta_block = value >> (FFS_BLOCK_SIZE_BITS - hashfs_meta_size_bits);
    meta_offset = (value << hashfs_meta_size_bits) & (FFS_BLOCK_SIZE - 1);
    bh = sb_bread(sb, meta_block);

linear_detection:
    if (!bh) {
        return INVALID_LBA;
    }
    meta_entry = (__u64 *)(bh->b_data + meta_offset);
    if (*meta_entry == hash_key) {
        goto got;
    }
    else {
        value++;
        meta_offset += hashfs_meta_size;
        if (meta_offset == PAGE_SIZE) {
            meta_offset = 0;
            meta_block++;
            brelse(bh);
            bh = sb_bread(sb, meta_block);
        }
        goto linear_detection;
    }   

got:
    data_block = hashfs_data_start + value;
    brelse(bh);
	return data_block;
}


sector_t hashfs_set_data_lba(struct inode *inode, sector_t iblock)
{
    struct super_block *sb = inode->i_sb;
    __u64 hash_key = (inode->i_ino<<32)|iblock;
    unsigned long value = BKDRHash(hash_key, ~(FFS_BLOCK_SIZE - 1));
    unsigned meta_block, meta_offset;
    unsigned long data_block;
    struct buffer_head * bh = NULL;
    __u64 *meta_entry;

    meta_block = value >> (FFS_BLOCK_SIZE_BITS - hashfs_meta_size_bits);
    meta_offset = (value << hashfs_meta_size_bits) & (FFS_BLOCK_SIZE - 1);
    bh = sb_bread(sb, meta_block);

linear_detection:
    if (!bh) {
        return INVALID_LBA;
    }
    meta_entry = (__u64 *)(bh->b_data + meta_offset);
    if (*meta_entry == 0) {
        goto set;
    }
    else {
        value++;
        meta_offset += hashfs_meta_size;
        if (meta_offset == PAGE_SIZE) {
            meta_offset = 0;
            meta_block++;
            brelse(bh);
            bh = sb_bread(sb, meta_block);
        }
        goto linear_detection;
    }   

set:
    *meta_entry = hash_key;
    data_block = hashfs_data_start + value;
    mark_buffer_dirty(bh);
    brelse(bh);
	return data_block;
}