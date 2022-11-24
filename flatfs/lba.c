#include <linux/types.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/version.h>
#include <linux/nls.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/backing-dev.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/parser.h>
#include <linux/magic.h>
#include <linux/uaccess.h>
#include <linux/namei.h>
#include <linux/buffer_head.h>
#include <linux/vmalloc.h>
#ifndef _TEST_H_
#define _TEST_H_
#include "flatfs_d.h"
#include "lba.h"
#endif

void init_file_ht(struct HashTable **file_ht, int tag)
{
	int bkt, slt;
	int bkt_num;

	(*file_ht) = (struct HashTable *)kzalloc(sizeof(struct HashTable), GFP_KERNEL);
	if (tag == 0) {
		bkt_num == L_BUCKET_NUM;
		(*file_ht)->dtype = small;
		(*file_ht)->buckets = (struct bucket *)kzalloc(sizeof(struct bucket) * bkt_num, GFP_KERNEL);
	}
	else {	
		bkt_num == S_BUCKET_NUM;
		(*file_ht)->dtype = large;
		(*file_ht)->buckets = (struct bucket *)vmalloc(sizeof(struct bucket) * bkt_num);
	}
	
	for (bkt = 0; bkt < bkt_num; bkt++)
	{
		bitmap_zero((*file_ht)->buckets[bkt].slot_bitmap, SLOT_NUM);
		/* 第一个slot固定用来存放该bucket下所有文件的inode信息 */
		(*file_ht)->buckets[bkt].valid_slot_count = 0;
	}
}

void free_file_ht(struct HashTable **file_ht, int tag)
{
	if (tag == 0) {
		kfree((*file_ht)->buckets);
	}
	else {
		vfree((*file_ht)->buckets);
	}
	kfree(*file_ht);
	*file_ht = NULL;
}

// BKDR Hash Function
unsigned int BKDRHash(char *str)
{
	unsigned int seed = 131;
	unsigned int hash = 0;

	while (*str)
	{
		hash = hash * seed + (*str++);
	}

	return (hash & 0x7FFFFFFF);
}

/**
 * insert into file_hash
 * return file_seg
 */
static inline ffs_ino_t insert_file(struct HashTable *file_ht, int parent_dir, char *filename)
{
	unsigned long hashcode = BKDRHash(filename);
	unsigned long mask;
	unsigned long bucket_id;
	__u8 slt;
	struct ffs_ino ino;
	ino.ino = 0;

	switch (file_ht->dtype) {
		case small:
			mask = (S_BUCKET_NUM - 1LU);
			break;
		case large:
			mask = (L_BUCKET_NUM - 1LU);
			break;
		default: return INVALID_INO;
	}
	bucket_id = (unsigned long)hashcode & mask;

	/* lock for multiple create file, unlock after mark inode dirty */
	spin_lock(&(file_ht->buckets[bucket_id].bkt_lock));

	slt = find_first_zero_bit(file_ht->buckets[bucket_id].slot_bitmap, SLOT_NUM);
	if (slt == (1 << SLOT_BITS))
		return INVALID_LBA;
	bitmap_set(file_ht->buckets[bucket_id].slot_bitmap, slt, 1);
	file_ht->buckets[bucket_id].valid_slot_count++;
	
	switch (file_ht->dtype) {
		case small:
			ino.s_file_seg.slot = slt;
			ino.s_file_seg.bkt = bucket_id;
			ino.s_file_seg.dir = parent_dir;
			ino.s_file_seg.xtag = 0;
			break;
		case large:
			ino.l_file_seg.slot = slt;
			ino.l_file_seg.bkt = bucket_id;
			ino.l_file_seg.dir = parent_dir;
			ino.l_file_seg.xtag = 1;
			break;
		default: return INVALID_INO;
	}

	return ino.ino;
}

int delete_file(struct HashTable *file_ht, int bucket_id, int slot_id)
{
	int bkt_num = ((file_ht->dtype == small) ? S_BUCKET_NUM : L_BUCKET_NUM);
	if (bucket_id == -1 || bucket_id > bkt_num || slot_id > SLOT_NUM)
		return 0;
	// printk("start to delete file\n");
	spin_lock(&(file_ht->buckets[bucket_id].bkt_lock));
	if (!test_bit(slot_id, file_ht->buckets[bucket_id].slot_bitmap))
	{
		return 0;
	}

	file_ht->buckets[bucket_id].valid_slot_count--;
	bitmap_clear(file_ht->buckets[bucket_id].slot_bitmap, slot_id, 1);
	// printk("bitmap: %x\n", *(file_ht->buckets[bucket_id].slot_bitmap));
	return 1;
}

struct ffs_inode_info *FFS_I(struct inode *inode)
{
	return container_of(inode, struct ffs_inode_info, vfs_inode);
}


/**
 * 这里处理小目录文件的inode和data的lba
 * flag == 0: inode,
 * flag == 1: data.
 */
static inline lba_t compose_lba_small_hash(int dir_id, int bucket_id, int slot_id, int block_id, int flag)
{ 
	struct lba lba;
	lba.lba = 0;

	if (flag == 0) {
		lba.s_meta_seg.dir = dir_id;
		lba.s_meta_seg.bkt = bucket_id;
		if (slot_id != -1) {
			lba.s_meta_seg.slot = slot_id;
			lba.lba += FILE_META_LBA_BASE;
		}
	}
	else {
		lba.s_seg.dir = dir_id;
		lba.s_seg.bkt = bucket_id;
		lba.s_seg.slot = slot_id;
		lba.s_seg.off = block_id;
	}

	return lba;
}

/**
 * 这里处理大目录文件的inode和data的lba
 * flag == 0: inode,
 * flag == 1: data.
 */
static inline lba_t compose_lba_large_hash(int dir_id, int bucket_id, int slot_id, int block_id, int flag)
{
	struct lba lba;
	lba.lba = 0;

	if (flag == 0) {
		lba.l_meta_seg.dir = dir_id;
		lba.l_meta_seg.bkt = bucket_id;
		if (slot_id != -1) {
			lba.l_meta_seg.slot = slot_id;
			lba.lba += FILE_META_LBA_BASE;
		}
	}
	else {
		lba.l_seg.dir = dir_id;
		lba.l_seg.bkt = bucket_id;
		lba.l_seg.slot = slot_id;
		lba.l_seg.off = block_id;
	}

	lba.l_seg.xtag = 1;
	return lba;
}

/**
 * 这里处理文件的inode和data的lba
 * flag == 0: inode,
 * flag == 1: data.
 * tag == 0: small dir
 * tag == 1: large dir
 */
static inline lba_t compose_lba(int dir_id, int bucket_id, int slot_id, int block_id, int flag, int tag)
{ 
	if (tag == 0) {
		return compose_lba_large_hash(dir_id, bucket_id, slot_id, block_id, flag);
	}
	else {
		return compose_lba_small_hash(dir_id, bucket_id, slot_id, block_id, flag);
	}
}

/**
 * 文件数据lba
 */
lba_t ffs_get_data_lba(struct inode *inode, lba_t iblock, int tag)
{
	struct ffs_inode_info *fi = FFS_I(inode);
	return compose_lba(fi->dir_id, fi->bucket_id, fi->slot_id, iblock << BLOCK_SHIFT, 1, tag);
}

/**
 * 文件/目录元数据(inode)的lba
 */
lba_t ffs_get_meta_lba(struct inode *inode, int tag)
{
	struct ffs_inode_info *fi = FFS_I(inode);
	return compose_lba(fi->dir_id, fi->bucket_id, 0, 0, 0, tag);
}

/**
 * 根据文件名分配slot，并返回ino
 */
unsigned long flatfs_file_slot_alloc_by_name
(struct HashTable *hashtbl, struct inode *parent, int parent_dir_id, struct qstr *child)
{
	struct ffs_inode_info *p_fi = FFS_I(parent);
	char *name = (char *)(child->name);
	ffs_ino_t ino = insert_file(hashtbl, parent_dir_id, name);
	return ino;
}

int read_dir_files(struct HashTable *hashtbl, struct inode *inode, unsigned long ino, struct dir_context *ctx)
{
	unsigned long dir_id = inode_to_dir_id(ino);
	int bkt, slt;
	int pos = 0;
	struct super_block *sb = inode->i_sb;
	struct buffer_head *ibh = NULL;
	struct ffs_inode *raw_inode;
	struct ffs_inode_page *raw_inode_page;
	lba_t pblk;

	// printk("ctx->pos:%d\n", ctx->pos);
	if (hashtbl->pos == 0)
	{
		bkt = -1;
		goto first;
	}

	for (bkt = 0; bkt < (1 << MIN_FILE_BUCKET_BITS); bkt++)
	{
		for (slt = 0; slt < (1 << FILE_SLOT_BITS); slt++)
		{
			if (pos >= hashtbl->pos)
				break;
			if (test_bit(slt, hashtbl->buckets[bkt].slot_bitmap))
				pos++;
		}
		if (pos >= hashtbl->pos)
			break;
	}

	ino = (bkt + (dir_id << MIN_FILE_BUCKET_BITS)) << FILE_SLOT_BITS;
	ibh = sb_bread(sb, ino);
	wait_on_buffer(ibh);
	for (1; slt < (1 << FILE_SLOT_BITS); slt++)
	{
		if (test_bit(slt, hashtbl->buckets[bkt].slot_bitmap))
		{
			/* 开始传 */
			unsigned char d_type = FT_UNKNOWN;

			ino = (slt + ((bkt + (dir_id << MIN_FILE_BUCKET_BITS)) << FILE_SLOT_BITS));
			raw_inode = (struct ffs_inode *)(ibh->b_data + slt * (1 << 9));
			// printk("ino:%lx, dir_id:%x, bucket_id:%x, slot_id:%x, filename:%s\n", ino, dir_id, bkt, slt, fi->filename.name);
			dir_emit(ctx, raw_inode->filename.name, raw_inode->filename.name_len, le32_to_cpu(ino), d_type);
			__le16 dlen = 1;
			/* 上下文指针原本指向目录项文件的位置，现在我们设计变了，改成了表示第pos个子目录 */
			ctx->pos += le16_to_cpu(dlen);
			hashtbl->pos += le16_to_cpu(dlen);
		}
	}
	// printk("bkt:%d, slt:%d\n", bkt, slt);

first:
	for (bkt++; bkt < (1 << MIN_FILE_BUCKET_BITS); bkt++)
	{
		int flag = 0;
		for (slt = 0; slt < (1 << FILE_SLOT_BITS); slt++)
		{
			if (test_bit(slt, hashtbl->buckets[bkt].slot_bitmap))
			{
				flag = 1;
				break;
			}
		}
		if (flag == 0)
			continue;
		ino = (slt + ((bkt + (dir_id << MIN_FILE_BUCKET_BITS)) << FILE_SLOT_BITS));
		ibh = sb_bread(sb, ino);
		wait_on_buffer(ibh);
		// printk("ino:%ld", ino);
		for (slt = 0; slt < (1 << FILE_SLOT_BITS); slt++)
		{
			if (test_bit(slt, hashtbl->buckets[bkt].slot_bitmap))
			{
				/* 开始传 */
				unsigned char d_type = FT_UNKNOWN;

				ino = (slt + ((bkt + (dir_id << MIN_FILE_BUCKET_BITS)) << FILE_SLOT_BITS));
				raw_inode = (struct ffs_inode *)(ibh->b_data + slt * (1 << 9));
				// printk("ino:%lx, dir_id:%x, bucket_id:%x, slot_id:%x, filename:%s\n", ino, dir_id, bkt, slt, raw_inode->filename.name);
				dir_emit(ctx, raw_inode->filename.name, raw_inode->filename.name_len, le32_to_cpu(ino), d_type);
				__le16 dlen = 1;
				/* 上下文指针原本指向目录项文件的位置，现在我们设计变了，改成了表示第pos个子目录 */
				ctx->pos += le16_to_cpu(dlen);
				hashtbl->pos += le16_to_cpu(dlen);
			}
		}
	}
	brelse(ibh);
	return 0;
}

int read_big_dir_files(struct HashTable *hashtbl, struct inode *inode, unsigned long ino, struct dir_context *ctx)
{
	unsigned long dir_id = (ino - 1) >> (MIN_FILE_BUCKET_BITS + FILE_SLOT_BITS);
	int bkt, slt;
	int pos = 0;
	struct super_block *sb = inode->i_sb;
	struct buffer_head *ibh = NULL;
	struct ffs_inode *raw_inode;
	sector_t pblk;

	// printk("ctx->pos:%d\n", ctx->pos);
	if (hashtbl->pos == 0)
	{
		bkt = -1;
		goto first;
	}

	for (bkt = 0; bkt < (1 << (MIN_FILE_BUCKET_BITS + MIN_DIR_BITS)); bkt++)
	{
		for (slt = 0; slt < (1 << FILE_SLOT_BITS); slt++)
		{
			if (pos >= hashtbl->pos)
				break;
			if (test_bit(slt, hashtbl->buckets[bkt].slot_bitmap))
				pos++;
		}
		if (pos >= hashtbl->pos)
			break;
	}

	ino = (bkt + (dir_id << (MIN_FILE_BUCKET_BITS + MIN_DIR_BITS))) << FILE_SLOT_BITS;
	ibh = sb_bread(sb, ino);
	wait_on_buffer(ibh);
	for (1; slt < (1 << FILE_SLOT_BITS); slt++)
	{
		if (test_bit(slt, hashtbl->buckets[bkt].slot_bitmap))
		{
			/* 开始传 */
			unsigned char d_type = FT_UNKNOWN;

			ino = (slt + ((bkt + (dir_id << (MIN_FILE_BUCKET_BITS + MIN_DIR_BITS))) << FILE_SLOT_BITS));
			raw_inode = (struct ffs_inode *)(ibh->b_data + slt * (1 << 9));
			// printk("ino:%lx, dir_id:%x, bucket_id:%x, slot_id:%x, filename:%s\n", ino, dir_id, bkt, slt, fi->filename.name);
			dir_emit(ctx, raw_inode->filename.name, raw_inode->filename.name_len, le32_to_cpu(ino), d_type);
			__le16 dlen = 1;
			/* 上下文指针原本指向目录项文件的位置，现在我们设计变了，改成了表示第pos个子目录 */
			ctx->pos += le16_to_cpu(dlen);
			hashtbl->pos += le16_to_cpu(dlen);
		}
	}
	// printk("bkt:%d, slt:%d\n", bkt, slt);

first:
	for (bkt++; bkt < (1 << (MIN_FILE_BUCKET_BITS + MIN_DIR_BITS)); bkt++)
	{
		int flag = 0;
		for (slt = 0; slt < (1 << FILE_SLOT_BITS); slt++)
		{
			if (test_bit(slt, hashtbl->buckets[bkt].slot_bitmap))
			{
				flag = 1;
				break;
			}
		}
		if (flag == 0)
			continue;
		ino = (slt + ((bkt + (dir_id << (MIN_FILE_BUCKET_BITS + MIN_DIR_BITS))) << FILE_SLOT_BITS));
		ibh = sb_bread(sb, ino);
		wait_on_buffer(ibh);
		// printk("ino:%ld", ino);
		for (slt = 0; slt < (1 << FILE_SLOT_BITS); slt++)
		{
			if (test_bit(slt, hashtbl->buckets[bkt].slot_bitmap))
			{
				/* 开始传 */
				unsigned char d_type = FT_UNKNOWN;

				ino = (slt + ((bkt + (dir_id << (MIN_FILE_BUCKET_BITS + MIN_DIR_BITS))) << FILE_SLOT_BITS));
				raw_inode = (struct ffs_inode *)(ibh->b_data + slt * (1 << 9));
				// printk("ino:%ld, dir_id:%d, bucket_id:%d, slot_id:%x, filename:%s\n", ino, dir_id, bkt, slt, raw_inode->filename.name);
				dir_emit(ctx, raw_inode->filename.name, raw_inode->filename.name_len, le32_to_cpu(ino), d_type);
				__le16 dlen = 1;
				/* 上下文指针原本指向目录项文件的位置，现在我们设计变了，改成了表示第pos个子目录 */
				ctx->pos += le16_to_cpu(dlen);
				hashtbl->pos += le16_to_cpu(dlen);
			}
		}
	}
	brelse(ibh);
	return 0;
}


/* 根据文件名查找slot，生成文件的ino */
static inline ffs_ino_t get_file_ino
(struct inode *inode, struct HashTable *file_ht, char *filename, struct ffs_inode **raw_inode, struct buffer_head **bh)
{
	unsigned int hashcode = BKDRHash(filename);
	unsigned long bucket_id = (unsigned long)hashcode & ((1LU << MIN_FILE_BUCKET_BITS) - 1LU);
	int slt;
	int namelen = my_strlen(filename);
	struct super_block *sb = inode->i_sb;
	struct ffs_inode_info *fi = FFS_I(inode);
	lba_t pblk;
	int flag = 0;
	struct ffs_inode_page *raw_inode_page; /* 8 slots composed bukcet */
	struct ffs_ino ino;
	ino.ino = 0

	for (slt = 0; slt < SLOT_NUM; slt++) {
		if (test_bit(slt, file_ht->buckets[bucket_id].slot_bitmap)) {
			flag = 1;
			break;
		}
	}

	if (flag == 0) {
		return INVALID_INO;
	}

	pblk = compose_lba(fi->dir_id, bucket_id, 0, 0, 0, file_ht->dtype);

	(*bh) = sb_bread(sb, pblk);
	wait_on_buffer(*bh);
	if (unlikely(!(*bh))) {
		return INVALID_INO;
	}

	raw_inode_page = (struct ffs_inode_page *)(bh->b_data);
	for (slt = 0; slt < SLOT_NUM; slt++)
	{
		(*raw_inode) = &(raw_inode_page->inode[slt]);
		if (test_bit(slt, file_ht->buckets[bucket_id].slot_bitmap) && \
			!strcmp(filename, (*raw_inode)->filename.name)) {
			break;
		}
	}

	// brelse(bh);
	if (slt >= SLOT_NUM) {
		return INVALID_INO;
	}

	switch (file_ht->dtype) {
		case small:
			ino.s_file_seg.slot = slt;
			ino.s_file_seg.bkt = bucket_id;
			ino.s_file_seg.dir = fi->dir_id;
			ino.s_file_seg.xtag = 0;
			break;
		case large:
			ino.l_file_seg.slot = slt;
			ino.l_file_seg.bkt = bucket_id;
			ino.l_file_seg.dir = fi->dir_id;
			ino.l_file_seg.xtag = 1;
			break;
		default: return INVALID_INO;
	}

	return ino.ino;
}


/**
 *  根据文件名查找ino 
 */
unsigned long flatfs_file_inode_by_name
(struct HashTable *hashtbl, struct inode *parent, struct qstr *child, struct ffs_inode **raw_inode, struct buffer_head **bh)
{
	struct ffs_inode_info *p_fi = FFS_I(parent);
	return get_file_ino(parent, hashtbl, (char *)(child->name), raw_inode, bh);
}

void print2log(struct HashTable *hashtbl)
{
	int bkt, slt;
	for (bkt = 0; bkt < (1 << MIN_FILE_BUCKET_BITS); bkt++)
	{
		printk("%d %d\n", bkt, hashtbl->buckets[bkt].valid_slot_count);
	}
}

int resize_dir(struct flatfs_sb_info *sb, int dir_id)
{
	int pos;
	if (sb->big_dir_num > (1 << MIN_DIR_BITS))
	{
		printk("resize_dir failed, over max num of big dirs");
		return -1;
	}
	pos = find_first_zero_bit(sb->big_dir_bitmap, 1 << MAX_DIR_BITS);
	printk("zero big_dir pos:%d, sb->big_dir_num:%d\n", pos, sb->big_dir_num);
	init_big_file_ht(&(sb->hashtbl[pos]));
	bitmap_set(sb->big_dir_bitmap, pos, 1);
	sb->big_dir_num++;
	return pos;
}