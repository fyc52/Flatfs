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

void init_file_ht(struct HashTable **file_ht)
{
	int bkt;

	(*file_ht) = (struct HashTable *)kzalloc(sizeof(struct HashTable), GFP_KERNEL);	
	(*file_ht)->total_slot_count = 0;
	(*file_ht)->buckets = (struct bucket *)vmalloc(sizeof(struct bucket) * FILE_BUCKET_NUM);
	
	for (bkt = 0; bkt < FILE_BUCKET_NUM; bkt++)
	{
		bitmap_zero((*file_ht)->buckets[bkt].slot_bitmap, SLOT_NUM);
		/* 第一个slot固定用来存放该bucket下所有文件的inode信息 */
		(*file_ht)->buckets[bkt].valid_slot_count = 0;
	}
}

void free_file_ht(struct HashTable **file_ht)
{

	vfree((*file_ht)->buckets);
	kfree(*file_ht);
	*file_ht = NULL;
}

// BKDR Hash Function
unsigned int BKDRHash(char *str, int len)
{
	unsigned int seed = 4397;
	unsigned int hash = 0;

	while (len > 0)
	{
		hash = hash * seed + (*str++);
		len--;
	}

	return (hash & 0x7FFFFFFF);
}

/**
 * insert into file_hash
 * return file_seg
 */
static inline struct ffs_ino insert_file(struct HashTable *file_ht, int parent_dir, struct qstr *filename)
{
	unsigned long hashcode = BKDRHash(filename->name, filename->len);
	unsigned long mask;
	unsigned long bucket_id;
	__u8 slt;
	struct ffs_ino ino;
	unsigned long flags;
	ino.ino = INVALID_INO;
	mask = (FILE_BUCKET_NUM - 1LU);

	bucket_id = (unsigned long)hashcode & mask;

	/* lock for multiple create file, unlock after mark inode dirty */
	spin_lock_irqsave(&(file_ht->buckets[bucket_id].bkt_lock), flags);

	slt = find_first_zero_bit(file_ht->buckets[bucket_id].slot_bitmap, SLOT_NUM);
	if (slt == SLOT_NUM)
	{
		spin_unlock_irqrestore(&(file_ht->buckets[bucket_id].bkt_lock), flags);
		return ino;
	}
	bitmap_set(file_ht->buckets[bucket_id].slot_bitmap, slt, 1);
	file_ht->buckets[bucket_id].valid_slot_count++;
	file_ht->total_slot_count++;
	
	ino.file_seg.slot = slt;
	ino.file_seg.bkt = bucket_id;
	ino.file_seg.dir = parent_dir;
	spin_unlock_irqrestore(&(file_ht->buckets[bucket_id].bkt_lock), flags);
	return ino;
}

int delete_file(struct HashTable *file_ht, int bucket_id, int slot_id)
{
	unsigned long flags;
	int bkt_num = FILE_BUCKET_NUM;
	if (bucket_id == -1 || bucket_id > bkt_num || slot_id > SLOT_NUM)
		return 0;
	//printk("start to delete file\n");
	//if(bucket->bkt_lock) spin_lock(&(file_ht->buckets[bucket_id].bkt_lock));
	spin_lock_irqsave(&(file_ht->buckets[bucket_id].bkt_lock), flags);
	if (!test_bit(slot_id, file_ht->buckets[bucket_id].slot_bitmap))
	{
		spin_unlock_irqrestore(&(file_ht->buckets[bucket_id].bkt_lock), flags);
		return 0;
	}

	file_ht->buckets[bucket_id].valid_slot_count--;
	file_ht->total_slot_count--;
	bitmap_clear(file_ht->buckets[bucket_id].slot_bitmap, slot_id, 1);
	// printk("bitmap: %x\n", *(file_ht->buckets[bucket_id].slot_bitmap));
	spin_unlock_irqrestore(&(file_ht->buckets[bucket_id].bkt_lock), flags);
	return 1;
}

struct ffs_inode_info *FFS_I(struct inode *inode)
{
	return container_of(inode, struct ffs_inode_info, vfs_inode);
}

/**
 * 这里处理大目录文件的inode和data的lba
 * flag == 0: inode,
 * flag == 1: data.
 */
lba_t compose_file_lba(int dir_id, int bucket_id, int slot_id, int block_id, int flag)
{
	struct lba lba;
	lba.lba = 0;

	if (flag == 0) {
		lba.file_meta_seg.dir = dir_id;
		//printk("finode, dir_id: %d\n", dir_id);
		lba.file_meta_seg.bkt = bucket_id;
	}
	else {
		lba.data_seg.dir = dir_id;
		lba.data_seg.bkt = bucket_id;
		lba.data_seg.slot = slot_id;
		lba.data_seg.iblk = block_id;
	}

	return lba.lba;
}

lba_t compose_dir_lba(int dir_id)
{
	struct lba lba;
	lba.lba = 0;
	lba.dir_meta_seg.dir = dir_id;
	return lba.lba;
}

/**
 * flag == 0: dir,
 * flag == 1: file.
 */
ffs_ino_t compose_ino(int dir_id, int bucket_id, int slot_id, int flag)
{
	struct ffs_ino ino;
	ino.ino = 0;

	if (flag == 0) {
		ino.dir_seg.dir = dir_id;
	}
	else {
		ino.file_seg.bkt = bucket_id;
		ino.file_seg.dir = dir_id;
		ino.file_seg.slot = slot_id;
	}

	return ino.ino;
}

/**
 * 文件数据lba
 */
lba_t ffs_get_data_lba(struct inode *inode, lba_t iblock)
{
	struct ffs_ino ffs_ino;
	ffs_ino.ino = inode->i_ino;
	return compose_file_lba(ffs_ino.file_seg.dir, ffs_ino.file_seg.bkt, ffs_ino.file_seg.slot, iblock, 1);
}

/**
 * 根据文件名分配slot，并返回ino
 */
struct ffs_ino flatfs_file_slot_alloc_by_name(struct HashTable *hashtbl, struct inode *parent, int parent_dir_id, struct qstr *child)
{
	struct ffs_ino ino;
	if(child->len <= 0 || child->len > FFS_MAX_FILENAME_LEN) {
		ino.ino = INVALID_INO;
	}
	else {
		ino = insert_file(hashtbl, parent_dir_id, child);
	}
	return ino;
}

int read_dir_files(struct HashTable *hashtbl, struct inode *inode, ffs_ino_t ino, struct dir_context *ctx)
{
	int bkt, slt;
	int pos = 0;
	struct super_block *sb = inode->i_sb;
	struct buffer_head *ibh = NULL;
	struct ffs_inode *raw_inode;
	struct ffs_ino ffs_ino;
	struct ffs_inode_page *raw_inode_page;
	int bucket_num;
	lba_t pblk;
	bucket_num = TT_BUCKET_NUM;
	ffs_ino.ino = ino;
	
	for (bkt = 0; bkt < bucket_num; bkt++)
	{
		int flag = 0;
		for (slt = 0; slt < FILE_SLOT_NUM; slt++)
		{
			if (test_bit(slt, hashtbl->buckets[bkt].slot_bitmap) && test_bit(slt, hashtbl->buckets[bkt].ls_slot_bitmap))
			{
				flag = 1;
				break;
			}
		}
		if (flag == 0) continue;
		pblk = compose_file_lba(ffs_ino.dir_seg.dir, bkt, 0, 0, 0);
		ibh = sb_bread(sb, pblk);
		raw_inode_page = (struct ffs_inode_page *)(ibh->b_data);
		// printk("ino:%ld", ino);
		for (slt = 0; slt < FILE_SLOT_NUM; slt++)
		{
			if (test_bit(slt, hashtbl->buckets[bkt].slot_bitmap) && test_bit(slt, hashtbl->buckets[bkt].ls_slot_bitmap))
			{
				/* 开始传 */
				unsigned char d_type = FT_UNKNOWN;

				ino = compose_ino(ffs_ino.dir_seg.dir, bkt, slt, 1);
				raw_inode = &(raw_inode_page->inode[slt]);
				//printk("bucket_id:%x, slot_id:%x, filename:%s\n", bkt, slt, raw_inode->filename.name);
				if(dir_emit(ctx, raw_inode->filename.name, raw_inode->filename.name_len, le64_to_cpu(ino), d_type))
				{
					bitmap_clear(hashtbl->buckets[bkt].ls_slot_bitmap, slt, 1);
					ctx->pos ++;
					//printk("bkt:%d, slt:%d\n", bkt, slt);
				}
				/* 上下文指针原本指向目录项文件的位置，现在我们设计变了，改成了表示第pos个子目录 */
			}
		}
		if(ibh) brelse(ibh);
	}
	
	return 0;
}


/* 根据文件名查找slot，生成文件的ino */
static inline ffs_ino_t get_file_ino
(struct inode *inode, struct HashTable *file_ht, struct qstr *filename, struct ffs_inode **raw_inode, struct buffer_head **bh)
{
	unsigned long hashcode = BKDRHash(filename->name, filename->len);
	unsigned long bucket_id;
	int slt;
	struct super_block *sb = inode->i_sb;
	lba_t pblk;
	int flag = 0;
	unsigned long flags;
	struct ffs_inode_page *raw_inode_page; /* 8 slots composed bukcet */
	struct ffs_ino ino;
	struct ffs_ino dir_ino;
	dir_ino.ino = inode->i_ino;
	ino.ino = INVALID_INO;
	unsigned long mask;

	mask = (FILE_BUCKET_NUM - 1LU);
	bucket_id = (unsigned long)hashcode & mask;
	spin_lock_irqsave(&(file_ht->buckets[bucket_id].bkt_lock), flags);
	//("get_file_ino, file_ht type:%d, bkt_id:%d\n", file_ht->dtype, bucket_id);
	for (slt = 0; slt < SLOT_NUM; slt ++) {
		//printk("get_file_ino, slt:%d\n", slt);
		if (test_bit(slt, file_ht->buckets[bucket_id].slot_bitmap)) {
			flag = 1;
			break;
		}
	}
	spin_unlock_irqrestore(&(file_ht->buckets[bucket_id].bkt_lock), flags);

	if (flag == 0) {
		return INVALID_INO;
	}
	pblk = compose_file_lba(dir_ino.dir_seg.dir, bucket_id, 0, 0, 0);
	//printk("get_file_ino, pblk:%d\n", pblk);
	(*bh) = sb_bread(sb, pblk);
	if (unlikely(!(*bh))) {
		return INVALID_INO;
	}
	lock_buffer(*bh);
	raw_inode_page = (struct ffs_inode_page *)((*bh)->b_data);
	spin_lock_irqsave(&(file_ht->buckets[bucket_id].bkt_lock), flags);
	for (slt = 0; slt < SLOT_NUM; slt++)
	{
		(*raw_inode) = &(raw_inode_page->inode[slt]);
		if (test_bit(slt, file_ht->buckets[bucket_id].slot_bitmap)) {
			if((*raw_inode)->filename.name_len == filename->len && !strcmp(filename->name, (*raw_inode)->filename.name))
				break;
		}
	}
	spin_unlock_irqrestore(&(file_ht->buckets[bucket_id].bkt_lock), flags);
	unlock_buffer(*bh);
	///printk("get_file_ino, slt:%d\n", slt);
	if (slt >= SLOT_NUM) {
		return INVALID_INO;
	}

	ino.file_seg.slot = slt;
	ino.file_seg.bkt = bucket_id;
	ino.file_seg.dir = dir_ino.dir_seg.dir;	
	return ino.ino;
}

/**
 *  根据文件名查找ino 
 */
unsigned long flatfs_file_inode_by_name
(struct HashTable *hashtbl, struct inode *parent, struct qstr *child, struct ffs_inode **raw_inode, struct buffer_head **bh)
{
	if(child->len > FFS_MAX_FILENAME_LEN || child->len <= 0) {
		return INVALID_INO;
	}
	return get_file_ino(parent, hashtbl, child, raw_inode, bh);
}
