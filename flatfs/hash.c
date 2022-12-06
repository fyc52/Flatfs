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

    printk("key: %lx, hash: %lx\n", hash_key, (hash & mask));
	return (hash & mask);
}


sector_t hashfs_get_data_lba(struct super_block *sb, ino_t ino, sector_t iblock)
{
    __u64 hash_key = (ino<<32) | iblock;
    unsigned long value = BKDRHash(hash_key, (MAX_BLOCK_NUM - 1));
    unsigned meta_block, meta_offset;
    unsigned long data_block;
    struct buffer_head * bh = NULL;
    __u64 *meta_entry;

    meta_block = value >> (FFS_BLOCK_SIZE_BITS - HASHFS_META_SIZE_BITS);
    meta_offset = (value << HASHFS_META_SIZE_BITS) & (FFS_BLOCK_SIZE - 1);
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
        value = ((value >= MAX_BLOCK_NUM) ? 0 : (value + 1));
        meta_offset += HASHFS_META_SIZE;
        if (meta_offset == PAGE_SIZE) {
            meta_offset = 0;
            meta_block++;
            brelse(bh);
            bh = sb_bread(sb, meta_block);
        }
        goto linear_detection;
    }   

got:
    data_block = HASHFS_DATA_START + value;
    brelse(bh);
	return data_block;
}


sector_t hashfs_set_data_lba(struct inode *inode, sector_t iblock)
{
    struct super_block *sb = inode->i_sb;
    __u64 hash_key = (inode->i_ino<<32) | iblock;
    unsigned long value = BKDRHash(hash_key, (MAX_BLOCK_NUM - 1));
    unsigned meta_block, meta_offset;
    unsigned long data_block;
    struct buffer_head * bh = NULL;
    __u64 *meta_entry;

    meta_block = value >> (FFS_BLOCK_SIZE_BITS - HASHFS_META_SIZE_BITS);
    meta_offset = (value << HASHFS_META_SIZE_BITS) & (FFS_BLOCK_SIZE - 1);
    bh = sb_bread(sb, meta_block);
    printk("value = %ld\n", value);

linear_detection:
    if (!bh) {
        return INVALID_LBA;
    }
    meta_entry = (__u64 *)(bh->b_data + meta_offset);
    if (*meta_entry == 0) {
        printk("set value = %ld\n", value);
        goto set;
    }
    else {
        value = ((value >= MAX_BLOCK_NUM) ? 0 : (value + 1));
        meta_offset += HASHFS_META_SIZE;
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
    data_block = HASHFS_DATA_START + value;
    mark_buffer_dirty(bh);
    brelse(bh);
	return data_block;
}


void hashfs_remove_inode(struct inode *inode)
{
    struct super_block *sb = inode->i_sb;
    sector_t iblock = 0;
    sector_t tt_blocks = inode->i_blocks;
    __u64 hash_key;
    unsigned long value;
    unsigned meta_block, meta_offset;
    struct buffer_head * bh = NULL;
    __u64 *meta_entry;

linear_detection:
    hash_key = (inode->i_ino<<32) | iblock;
    value = BKDRHash(hash_key, (MAX_BLOCK_NUM - 1));
    meta_block = value >> (FFS_BLOCK_SIZE_BITS - HASHFS_META_SIZE_BITS);
    meta_offset = (value << HASHFS_META_SIZE_BITS) & (FFS_BLOCK_SIZE - 1);
    if (meta_offset == 0 || !bh) {    
        if(bh) {
            mark_buffer_dirty(bh); 
            brelse(bh);
            printk("bh brelse ok\n");
        }
        bh = sb_bread(sb, meta_block);
    }
    printk("ttblocks: %ld\n", tt_blocks);
    iblock++; 
    if (!bh || iblock > tt_blocks) {
        return;
    }
    meta_entry = (__u64 *)(bh->b_data + meta_offset);
    *meta_entry = 0;
    goto linear_detection;
}