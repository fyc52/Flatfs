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
	(*file_ht)->bkt_sem = (struct semaphore *)vmalloc(sizeof(struct semaphore) * TT_BUCKET_NUM);
	bitmap_zero((*file_ht)->buckets_bitmap, TT_BUCKET_NUM);
	for(bkt = 0; bkt < TT_BUCKET_NUM; bkt ++)
	{
		sema_init(&((*file_ht)->bkt_sem[bkt]), 1);
	}
}

void free_file_ht(struct HashTable **file_ht)
{

	vfree((*file_ht)->bkt_sem);
	kfree(*file_ht);
	*file_ht = NULL;
}

// BKDR Hash Function
unsigned int BKDRHash(char *str, int len)
{
	unsigned int seed = 10;
	unsigned int hash = 0;

	while (len > 0)
	{
		hash = hash * seed + *str - '0';
		str++;
		len--;
	}

	return (hash & 0x7FFFFFFF);
}

/**
 * insert into file_hash
 * return file_seg
 */
static inline struct ffs_ino insert_file(struct HashTable *file_ht, struct inode *parent_dir, struct qstr *filename)
{
	unsigned long hashcode = BKDRHash(filename->name, filename->len);
	unsigned long mask;
	unsigned long bucket_id;
	int slt;
	struct ffs_ino ino;
	struct ffs_inode *raw_inode;
	struct ffs_inode_page *raw_inode_page;
	sector_t pblk;
	struct buffer_head *ibh;

	ino.ino = INVALID_INO;
	mask = (FILE_BUCKET_NUM - 1LU);

	bucket_id = (unsigned long)hashcode & mask;
	pblk = compose_file_lba((int)(parent_dir->i_ino), bucket_id, 0, 0, 0);
	if (down_interruptible(&file_ht->bkt_sem[bucket_id])) 
	{
        printk(KERN_INFO "Semaphore acquisition failed\n");
        return ino;
    }
	ibh = sb_bread(parent_dir->i_sb, pblk);
	if (unlikely(!ibh)){
		printk(KERN_ERR "allocate bh for ffs_inode fail");
		return ino;
	}
	raw_inode_page = (struct ffs_inode_page *) (ibh->b_data);
	/* lock for multiple create file, unlock after mark inode dirty */
	if (!__test_and_set_bit_le(bucket_id, file_ht->buckets_bitmap)) 
	{
		bitmap_set(file_ht->buckets_bitmap, bucket_id, 1);
		memset(&(raw_inode_page->header), 0, sizeof(struct ffs_inode_page_header));
	}

	slt = find_first_zero_bit(raw_inode_page->header.slot_bitmap, SLOT_NUM);
	if (slt >= SLOT_NUM)
	{	
		printk(KERN_INFO "Bucket slot over\n");
		up(&file_ht->bkt_sem[bucket_id]);
		return ino;
	}
	bitmap_set(raw_inode_page->header.slot_bitmap, slt, 1);
	brelse(ibh);
	up(&file_ht->bkt_sem[bucket_id]);

	ino.file_seg.slot = slt;
	ino.file_seg.bkt = bucket_id;
	ino.file_seg.dir = (int)(parent_dir->i_ino);
	return ino;
}

int delete_file(struct HashTable *file_ht, struct inode *dir, int bucket_id, int slot_id)
{
	sector_t pblk;
	int bkt_num = FILE_BUCKET_NUM;
	struct buffer_head *ibh = NULL;
	struct ffs_inode* raw_inode;
	struct ffs_inode_page *raw_inode_page;

	if (bucket_id == -1 || bucket_id > bkt_num || slot_id >= SLOT_NUM)
		return 0;

	//printk("start to delete file\n");
	pblk = compose_file_lba(dir->i_ino, bucket_id, 0, 0, 0);
	down_interruptible(&file_ht->bkt_sem[bucket_id]);
	ibh = sb_bread(dir->i_sb, pblk);//这里不使用bread，避免读盘	
 	if (unlikely(!ibh)){
		printk(KERN_ERR "allocate bh for ffs_inode fail");
		return 0;
	}
	raw_inode_page = (struct ffs_inode_page *) (ibh->b_data);
	if (test_bit(slot_id, raw_inode_page->header.slot_bitmap)) {
		bitmap_clear(raw_inode_page->header.slot_bitmap, slot_id, 1);
		//printk("bitmap_clear ok, dir = %d, bkt = %d, slt = %d\n", dir->i_ino, bucket_id, slot_id);
	}
	// printk("bitmap: %x\n", *(file_ht->buckets[bucket_id].slot_bitmap));
	brelse(ibh);//put_bh, 对应getblk
	up(&file_ht->bkt_sem[bucket_id]);
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
struct ffs_ino flatfs_file_slot_alloc_by_name(struct HashTable *hashtbl, struct inode *parent, struct qstr *child)
{
	struct ffs_ino ino;
	if(child->len <= 0 || child->len > FFS_MAX_FILENAME_LEN) {
		printk("Name len err\n");
		ino.ino = INVALID_INO;
	}
	else {
		ino = insert_file(hashtbl, parent, child);
	}
	return ino;
}

void flatfs_debug(char *filen, struct inode *inode)
{
	unsigned long hashcode;
	unsigned long bucket_id;
	int slt;
	struct super_block *sb = inode->i_sb;
	lba_t pblk;
	int len = strlen("00000001");
	int i;
	unsigned long mask;
	printk("debug1, filen:%s\n", filen);

	mask = (FILE_BUCKET_NUM - 1LU);
	for(i = 1; i <= 1000000; i ++)
    {
		struct ffs_inode_page *raw_inode_page; /* 8 slots composed bukcet */
		struct ffs_inode *raw_inode;
		struct buffer_head *bh;
		char filename[100];
		sprintf(filename, "%08d", i);
		if(strcmp(filen, filename)) continue;
		bucket_id = i;
		//("get_file_ino, file_ht type:%d, bkt_id:%d\n", file_ht->dtype, bucket_id);

		pblk = compose_file_lba(4, i, 0, 0, 0);
		//printk("get_file_ino, pblk:%d\n", pblk);
		bh = sb_bread(sb, (4 << 21) + i);
		raw_inode_page = (struct ffs_inode_page *)(bh->b_data);

		for(slt = 0; slt < SLOT_NUM; slt ++)
		{
			raw_inode = &(raw_inode_page->inode[slt]);
			printk("slt:%d,bucket_id:%lu,filename:%s not found, raw_inode filename:%s!\n", slt, bucket_id, filen, raw_inode->filename.name);
		}
		brelse(bh);
		break;
	}
}

int read_dir_files(struct HashTable *hashtbl, struct inode *inode, ffs_ino_t ino, struct dir_context *ctx, unsigned long *ls_bitmap)
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
		if(!test_bit(bkt, hashtbl->buckets_bitmap)) continue;
		pblk = compose_file_lba(ffs_ino.dir_seg.dir, bkt, 0, 0, 0);
		ibh = sb_bread(sb, pblk);
		raw_inode_page = (struct ffs_inode_page *)(ibh->b_data);
		// printk("ino:%ld", ino);
		for (slt = 0; slt < FILE_SLOT_NUM; slt++)
		{
			if (test_bit(slt, raw_inode_page->header.slot_bitmap))
			{
				if(!test_bit(bkt * SLOT_NUM + slt, ls_bitmap)) continue;
				/* 开始传 */
				unsigned char d_type = FT_UNKNOWN;

				ino = compose_ino(ffs_ino.dir_seg.dir, bkt, slt, 1);
				raw_inode = &(raw_inode_page->inode[slt]);
				//printk("bucket_id:%x, slot_id:%x, filename:%s\n", bkt, slt, raw_inode->filename.name);
				if(dir_emit(ctx, raw_inode->filename.name, raw_inode->filename.name_len, le64_to_cpu(ino), d_type))
				{
					bitmap_clear(ls_bitmap, bkt * SLOT_NUM + slt, 1);
					ctx->pos ++;
					//printk("bkt:%d, slt:%d\n", bkt, slt);
				}
				/* 上下文指针原本指向目录项文件的位置，现在我们设计变了，改成了表示第pos个子目录 */
			}
		}
		brelse(ibh);
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
	struct ffs_inode_page *raw_inode_page; /* 8 slots composed bukcet */
	struct ffs_ino ino;
	struct ffs_ino dir_ino;
	dir_ino.ino = inode->i_ino;
	ino.ino = INVALID_INO;
	unsigned long mask;

	mask = (FILE_BUCKET_NUM - 1LU);
	bucket_id = (unsigned long)hashcode & mask;
	//("get_file_ino, file_ht type:%d, bkt_id:%d\n", file_ht->dtype, bucket_id);

	pblk = compose_file_lba(dir_ino.dir_seg.dir, bucket_id, 0, 0, 0);
	down_interruptible(&file_ht->bkt_sem[bucket_id]);
	(*bh) = sb_bread(sb, pblk);
	if (unlikely(!(*bh))) {
		return INVALID_INO;
	}

	raw_inode_page = (struct ffs_inode_page *)((*bh)->b_data);
	for (slt = 0; slt < SLOT_NUM; slt++)
	{
		(*raw_inode) = &(raw_inode_page->inode[slt]);
		if (test_bit(slt, raw_inode_page->header.slot_bitmap)) {
			if((*raw_inode)->filename.name_len == filename->len && !strncmp(filename->name, (*raw_inode)->filename.name, filename->len))
			{
				if (!strcmp("f1", (*raw_inode)->filename.name)) printk("lba:%ld, ffs lookup, size = %d, \n", pblk, (*raw_inode)->size);
				break;
			}
		}
	}
	up(&file_ht->bkt_sem[bucket_id]);
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
