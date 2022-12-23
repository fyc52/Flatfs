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
	//printk("init_file_ht start\n");
	int bkt;
	int bkt_num;

	(*file_ht) = (struct HashTable *)kzalloc(sizeof(struct HashTable), GFP_KERNEL);
	if (tag == 0) {
		//printk("init_file_ht small\n");
		bkt_num = S_BUCKET_NUM;
		(*file_ht)->total_slot_count = 0;
		(*file_ht)->dtype = small;
		(*file_ht)->buckets = (struct bucket *)kzalloc(sizeof(struct bucket) * bkt_num, GFP_KERNEL);
	}
	else {	
		bkt_num = L_BUCKET_NUM;
		(*file_ht)->total_slot_count = 0;
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
static inline struct ffs_ino insert_file(struct HashTable *file_ht, int parent_dir, char *filename)
{
	unsigned long hashcode = BKDRHash(filename);
	unsigned long mask;
	unsigned long bucket_id;
	__u8 slt;
	struct ffs_ino ino;
	ino.ino = INVALID_INO;

	switch (file_ht->dtype) {
		case small:
			mask = (S_BUCKET_NUM - 1LU);
			break;
		case large:
			mask = (L_BUCKET_NUM - 1LU);
			break;
		default: return ino;
	}
	bucket_id = (unsigned long)hashcode & mask;

	/* lock for multiple create file, unlock after mark inode dirty */
	//if(bucket->bkt_lock) spin_lock(&(file_ht->buckets[bucket_id].bkt_lock));

	slt = find_first_zero_bit(file_ht->buckets[bucket_id].slot_bitmap, SLOT_NUM);
	// printk("slt id: %d\n", slt);
	if (slt == SLOT_NUM)
		return ino;
	bitmap_set(file_ht->buckets[bucket_id].slot_bitmap, slt, 1);
	file_ht->buckets[bucket_id].valid_slot_count++;
	file_ht->total_slot_count++;
	
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
		default: return ino;
	}

	return ino;
}

int delete_file(struct HashTable *file_ht, int bucket_id, int slot_id)
{
	int bkt_num = ((file_ht->dtype == small) ? S_BUCKET_NUM : L_BUCKET_NUM);
	if (bucket_id == -1 || bucket_id > bkt_num || slot_id > SLOT_NUM)
		return 0;
	// printk("start to delete file\n");
	//if(bucket->bkt_lock) spin_lock(&(file_ht->buckets[bucket_id].bkt_lock));
	if (!test_bit(slot_id, file_ht->buckets[bucket_id].slot_bitmap))
	{
		return 0;
	}

	file_ht->buckets[bucket_id].valid_slot_count--;
	file_ht->total_slot_count--;
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
lba_t compose_lba_small_hash(int dir_id, int bucket_id, int slot_id, int block_id, int flag)
{ 
	struct lba lba;
	lba.lba = 0;

	if (flag == 0) {
		if (slot_id != -1) {
			lba.s_meta_seg.dir = dir_id;
			lba.s_meta_seg.bkt = bucket_id;
			// lba.s_meta_seg.slot = slot_id;
			// lba.lba += FILE_META_LBA_BASE;
		}
		else{
			lba.d_meta_seg.dir = dir_id;
		}
	}
	else {
		lba.s_seg.dir = dir_id;
		lba.s_seg.bkt = bucket_id;
		lba.s_seg.slot = slot_id;
		lba.s_seg.off = block_id << FFS_BLOCK_SIZE_BITS;
	}
	//printk("compose_lba_small_hash, dir id:%d, bucket_id:%d, slot_id:%d, lba:%lld\n", dir_id, bucket_id, slot_id, lba.lba);
	return lba.lba;
}

/**
 * 这里处理大目录文件的inode和data的lba
 * flag == 0: inode,
 * flag == 1: data.
 */
lba_t compose_lba_large_hash(int dir_id, int bucket_id, int slot_id, int block_id, int flag)
{
	struct lba lba;
	lba.lba = 0;

	if (flag == 0) {
		
		if (slot_id != -1) {
			lba.l_meta_seg.dir = dir_id;
			lba.l_meta_seg.bkt = bucket_id;
			// lba.l_meta_seg.slot = slot_id;
			// lba.lba += FILE_META_LBA_BASE;
		}
		else{
			lba.d_meta_seg.dir = dir_id;
		}
	}
	else {
		lba.l_seg.dir = dir_id;
		lba.l_seg.bkt = bucket_id;
		lba.l_seg.slot = slot_id;
		lba.l_seg.off = block_id << FFS_BLOCK_SIZE_BITS;
	}

	lba.l_seg.xtag = 1;
	return lba.lba;
}

/**
 * 这里处理文件的inode和data的lba
 * flag == 0: inode,
 * flag == 1: data.
 * tag == 0: small dir
 * tag == 1: large dir
 */
lba_t compose_lba(int dir_id, int bucket_id, int slot_id, int block_id, int flag, int tag)
{ 
	if (tag == 1) {
		return compose_lba_large_hash(dir_id, bucket_id, slot_id, block_id, flag);
	}
	else {
		return compose_lba_small_hash(dir_id, bucket_id, slot_id, block_id, flag);
	}
}

/**
 * for ls, alias lba space
*/
lba_t compose_alias_lba(int dir_id, int off)
{
	struct lba lba;
	lba.lba = 0;
	lba.s_meta_seg.dir = dir_id;
	lba.s_meta_seg.bkt = off;
	/* dir=1 is alias space */
	lba.s_seg.dir = 1;
	return lba.lba;
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
struct ffs_ino flatfs_file_slot_alloc_by_name(struct HashTable *hashtbl, struct inode *parent, int parent_dir_id, struct qstr *child)
{
	struct ffs_inode_info *p_fi = FFS_I(parent);
	char *name = (char *)(child->name);
	struct ffs_ino ino = insert_file(hashtbl, parent_dir_id, name);
	return ino;
}

int read_dir_files(struct HashTable *hashtbl, struct inode *inode, unsigned long ino, struct dir_context *ctx)
{
	unsigned long dir_id = ino;
	int bkt, slt;
	int pos = 0;
	struct super_block *sb = inode->i_sb;
	struct buffer_head *ibh = NULL;
	struct ffs_inode *raw_inode;
	struct ffs_inode_info *fi = FFS_I(inode);
	struct ffs_inode_page *raw_inode_page;
	int bucket_num;
	lba_t pblk;

	int entry_pos;
	int page_entry_pos;
	unsigned char d_type = FT_UNKNOWN;
	struct direntPage *dirent_page;

	if(fi->is_big_dir){
		bucket_num = L_BUCKET_NUM;
	}
	else{
		bucket_num = S_BUCKET_NUM;
	}

	// printk("ctx->pos:%d\n", ctx->pos);
	if (hashtbl->pos == 0)
	{
		bkt = -1;
		goto first;
	}

	return 0;
	for (bkt = 0; bkt < bucket_num; bkt++)
	{
		for (slt = 0; slt < (1 << SLOT_BITS); slt++)
		{
			if (pos >= hashtbl->pos)
				break;
			if (test_bit(slt, hashtbl->buckets[bkt].slot_bitmap))
				pos++;
		}
		if (pos >= hashtbl->pos)
			break;
	}

	ino = compose_lba(fi->dir_id, bkt, 0, 0, 0, hashtbl->dtype);

	ibh = sb_bread(sb, ino >> FFS_BLOCK_SIZE_BITS);
	raw_inode_page = (struct ffs_inode_page *)(ibh->b_data);
	for (1; slt < (1 << SLOT_BITS); slt++)
	{
		if (test_bit(slt, hashtbl->buckets[bkt].slot_bitmap))
		{
			/* 开始传 */
			unsigned char d_type = FT_UNKNOWN;

			ino = compose_lba(fi->dir_id, bkt, slt, 0, 0, hashtbl->dtype);
			raw_inode = &(raw_inode_page->inode[slt]);
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
	/**
	 * my code
	*/
	entry_pos = 0;
	while (entry_pos < hashtbl->total_slot_count)
	{
		ino = compose_alias_lba(fi->dir_id, entry_pos / DIRENT_NUM);
		ibh = sb_bread(sb, ino >> FFS_BLOCK_SIZE_BITS);
		dirent_page = (struct direntPage *) (ibh->b_data);
		
		for (page_entry_pos = 0; page_entry_pos < DIRENT_NUM; page_entry_pos++)
		{
			if (entry_pos >= hashtbl->total_slot_count) break;
			dir_emit(ctx, dirent_page->dirents[page_entry_pos].name, dirent_page->dirents[page_entry_pos].namelen, le32_to_cpu(ino), d_type);
			__le16 dlen = 1;
			ctx->pos += le16_to_cpu(dlen);
			entry_pos++;
		}
	}
	
	/* */

	// for (bkt++; bkt < bucket_num; bkt++)
	// {
	// 	int flag = 0;
	// 	for (slt = 0; slt < (1 << SLOT_BITS); slt++)
	// 	{
	// 		if (test_bit(slt, hashtbl->buckets[bkt].slot_bitmap))
	// 		{
	// 			flag = 1;
	// 			break;
	// 		}
	// 	}
	// 	if (flag == 0)
	// 		continue;
	// 	ino = compose_lba(fi->dir_id, bkt, 0, 0, 0, hashtbl->dtype);
	// 	ibh = sb_bread(sb, ino >> FFS_BLOCK_SIZE_BITS);
	// 	raw_inode_page = (struct ffs_inode_page *)(ibh->b_data);
	// 	// printk("ino:%ld", ino);
	// 	for (slt = 0; slt < (1 << SLOT_BITS); slt++)
	// 	{
	// 		if (test_bit(slt, hashtbl->buckets[bkt].slot_bitmap))
	// 		{
	// 			/* 开始传 */
	// 			unsigned char d_type = FT_UNKNOWN;

	// 			ino = compose_lba(fi->dir_id, bkt, slt, 0, 0, hashtbl->dtype);
	// 			raw_inode = &(raw_inode_page->inode[slt]);
	// 			// printk("ino:%lx, dir_id:%x, bucket_id:%x, slot_id:%x, filename:%s\n", ino, dir_id, bkt, slt, raw_inode->filename.name);
	// 			dir_emit(ctx, raw_inode->filename.name, raw_inode->filename.name_len, le32_to_cpu(ino), d_type);
	// 			__le16 dlen = 1;
	// 			/* 上下文指针原本指向目录项文件的位置，现在我们设计变了，改成了表示第pos个子目录 */
	// 			ctx->pos += le16_to_cpu(dlen);
	// 			hashtbl->pos += le16_to_cpu(dlen);
	// 		}
	// 	}
	// }
	brelse(ibh);
	return 0;
}


/* 根据文件名查找slot，生成文件的ino */
static inline ffs_ino_t get_file_ino
(struct inode *inode, struct HashTable *file_ht, char *filename, struct ffs_inode **raw_inode, struct buffer_head **bh)
{
	unsigned int hashcode = BKDRHash(filename);
	unsigned long bucket_id;
	__u8 slt;
	
	int namelen = my_strlen(filename);
	struct super_block *sb = inode->i_sb;
	struct ffs_inode_info *fi = FFS_I(inode);
	lba_t pblk;
	int flag = 0;
	struct ffs_inode_page *raw_inode_page; /* 8 slots composed bukcet */
	struct ffs_ino ino;
	ino.ino = INVALID_INO;
	unsigned long mask;

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

	//("get_file_ino, file_ht type:%d, bkt_id:%d\n", file_ht->dtype, bucket_id);
	for (slt = 0; slt < SLOT_NUM; slt ++) {
		//printk("get_file_ino, slt:%d\n", slt);
		if (test_bit(slt, file_ht->buckets[bucket_id].slot_bitmap)) {
			flag = 1;
			break;
		}
	}
	//printk("get_file_ino, slt:%d\n", slt);

	if (flag == 0) {
		return INVALID_INO;
	}

	pblk = compose_lba(fi->dir_id, bucket_id, 0, 0, 0, file_ht->dtype);
	pblk = pblk >> FFS_BLOCK_SIZE_BITS;
	//printk("get_file_ino, pblk:%d\n", pblk);
	(*bh) = sb_bread(sb, pblk);
	wait_on_buffer(*bh);
	if (unlikely(!(*bh))) {
		return INVALID_INO;
	}

	raw_inode_page = (struct ffs_inode_page *)((*bh)->b_data);
	for (slt = 0; slt < SLOT_NUM; slt++)
	{
		(*raw_inode) = &(raw_inode_page->inode[slt]);
		if (test_bit(slt, file_ht->buckets[bucket_id].slot_bitmap) && !strcmp(filename, (*raw_inode)->filename.name)) {
			break;
		}
	}
	///printk("get_file_ino, slt:%d\n", slt);
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
	for (bkt = 0; bkt < (1 << S_BUCKET_BITS); bkt++)
	{
		printk("%d %d\n", bkt, hashtbl->buckets[bkt].valid_slot_count);
	}
}
