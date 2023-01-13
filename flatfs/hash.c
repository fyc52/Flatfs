#include <linux/buffer_head.h>
#include "flatfs_d.h"

static int get_num;
static int get_linear_detection_num[10000 + 10];
static int get_page_num[10000 + 10];
static int set_num;
static int set_linear_detection_num[10000 + 10];
static int set_page_num[10000 + 10];

// BKDR Hash Function
static inline unsigned long BKDRHash(unsigned long hash_key, unsigned long mask)
{
	unsigned long seed = 4397;
	unsigned long hash = 0;
    int count = 0;
    char *str = (char *)&hash_key;

	while (count < 8) {
		hash = hash * seed + (*str++);
        count ++;
	}

    // printk("key: %lx, hash: %lx\n", hash_key, (hash & mask));
	return (hash & mask);
}

sector_t hashfs_get_data_lba(struct super_block *sb, ino_t ino, sector_t iblock)
{
    __u64 hash_key = (ino << 32) | iblock;
    unsigned long value = BKDRHash(hash_key, (MAX_BLOCK_NUM - 1));
    unsigned meta_block, meta_offset;
    unsigned long data_block;
    struct buffer_head * bh = NULL;
    __u64 *meta_entry;

    meta_block = value >> (FFS_BLOCK_SIZE_BITS - HASHFS_META_SIZE_BITS);
    meta_offset = (value << HASHFS_META_SIZE_BITS) & (FFS_BLOCK_SIZE - 1);
    bh = sb_bread(sb, meta_block);
    // int get_pages = 0;
    // int get_meta_times = 0;
    // get_pages ++;

linear_detection:
    //get_meta_times ++;
    if (!bh) {
        return INVALID_LBA;
    }
    meta_entry = (__u64 *)(bh->b_data + meta_offset);
    if (*meta_entry == hash_key || *meta_entry == 0) {
        goto got;
    }
    else {
        value += 1;
        if (value >= MAX_BLOCK_NUM) {
            value = 0;
            meta_block = -1;
            meta_offset = PAGE_SIZE;
        }
        else {
            meta_offset += HASHFS_META_SIZE;
        }
        if (meta_offset == PAGE_SIZE) {
            meta_offset = 0;
            meta_block++;
            brelse(bh);
            bh = sb_bread(sb, meta_block);
            //get_pages ++;
        }
        goto linear_detection;
    }   

got:
    // get_linear_detection_num[get_num / 3000] += get_meta_times;
    // get_page_num[get_num / 3000] += get_pages;
    // get_num ++;
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
    // int set_pages = 0;
    // int set_meta_times = 0;
    // set_pages ++;
    //printk("value = %ld\n", value);

linear_detection:
   // set_meta_times ++;
    if (!bh) {
        return INVALID_LBA;
    }
    meta_entry = (__u64 *)(bh->b_data + meta_offset);
    if (*meta_entry == 0) {
       // printk("set value = %ld\n", value);
        goto set;
    }
    else {
        value += 1;
        if (value >= MAX_BLOCK_NUM) {
            value = 0;
            meta_block = -1;
            meta_offset = PAGE_SIZE;
        }
        else {
            meta_offset += HASHFS_META_SIZE;
        }
        if (meta_offset == PAGE_SIZE) {
            meta_offset = 0;
            meta_block++;
            brelse(bh);
            bh = sb_bread(sb, meta_block);
            //set_pages ++;
        }
        goto linear_detection;
    }   

set:
    // set_page_num[set_num / 3000] += set_pages;
    // set_linear_detection_num[set_num / 3000] += set_meta_times;
    // set_num ++;
    *meta_entry = hash_key;
    data_block = HASHFS_DATA_START + value;
    mark_buffer_dirty(bh);
    brelse(bh);
    //"ino:%ld, iblock:%lld, inode->i_blocks:%lld\n", inode->i_ino, iblock, inode->i_blocks);
	return data_block;
}


void hashfs_remove_inode(struct inode *inode)
{
    struct super_block *sb = inode->i_sb;
    sector_t iblock = 0;
    sector_t tt_blocks = inode->i_blocks >> (FFS_BLOCK_SIZE_BITS - 9);
    tt_blocks ++;
    //printk("hashfs_remove_inode, tt_blocks:%lld\n", tt_blocks);
    __u64 hash_key;
    unsigned long value;
    unsigned meta_block, meta_offset;
    struct buffer_head * bh = NULL;
    __u64 *meta_entry;

delete_next:
    //"iblock = %lld\n", iblock);
    hash_key = (inode->i_ino<<32) | iblock;
    value = BKDRHash(hash_key, (MAX_BLOCK_NUM - 1));
    meta_block = value >> (FFS_BLOCK_SIZE_BITS - HASHFS_META_SIZE_BITS);
    meta_offset = (value << HASHFS_META_SIZE_BITS) & (FFS_BLOCK_SIZE - 1);
    //printk("value = %ld\n", value); 
    if(bh) {
        mark_buffer_dirty(bh); 
        brelse(bh);
        //printk("bh brelse ok\n");
    }
    bh = sb_bread(sb, meta_block);
    if (!bh)
        return;
    //printk("sb_bread ok\n");
linear_detection:
    meta_entry = (__u64 *)(bh->b_data + meta_offset);
    if (*meta_entry != hash_key) {
        if(*meta_entry == 0) goto meta_entry_0;
        value += 1;
        if (value >= MAX_BLOCK_NUM) {
            value = 0;
            meta_block = -1;
            meta_offset = PAGE_SIZE;
        }
        else {
            meta_offset += HASHFS_META_SIZE;
        }
        if (meta_offset == PAGE_SIZE) {
            meta_offset = 0;
            meta_block++;
            brelse(bh);
            bh = sb_bread(sb, meta_block);
        }
        goto linear_detection;
    }
meta_entry_0:
   // printk("remove value = %ld\n", value);
    iblock++;
    *meta_entry = 0;
    if (iblock <= tt_blocks)
        goto delete_next;
}

void print_hash_info(void)
{
    int i;
    printk("get_1000  get_linear_detection_num  get_page_num\n");
    for(i = 0; i < 10000; i ++)
    {
        if(get_linear_detection_num[i] == 0) break;
        printk("%d %d %d\n", i, get_linear_detection_num[i], get_page_num[i]);
    }

    printk("set_1000  set_linear_detection_num  set_page_num\n");
    for(i = 0; i < 10000; i ++)
    {
        if(set_linear_detection_num[i] == 0) break;
        printk("%d %d %d\n", i, set_linear_detection_num[i], set_page_num[i]);
    }
}