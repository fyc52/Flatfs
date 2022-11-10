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
#ifndef _TEST_H_
#define _TEST_H_
#include "flatfs_d.h"
#include "lba.h"
#endif

void init_file_ht(struct HashTable **file_ht)
{
	int bkt, slt;
	*file_ht = (struct HashTable *)kzalloc(sizeof(struct HashTable), GFP_KERNEL);
	for(bkt = 0; bkt < (1 << MIN_FILE_BUCKET_BITS); bkt++) {
		// (*file_ht)->buckets[bkt].bucket_id = bkt;
		bitmap_zero((*file_ht)->buckets[bkt].slot_bitmap, 1 << FILE_SLOT_BITS);
		/* 第一个slot固定用来存放该bucket下所有文件的inode信息 */
		// bitmap_set(file_ht->buckets[bkt].slot_bitmap, 0, 1);
		(*file_ht)->buckets[bkt].valid_slot_count = 0;
		
		// for(slt = 0; slt < (1 << FILE_SLOT_BITS); slt++ ) {
		// 	(*file_ht)->buckets[bkt].slots[slt].slot_id  = (unsigned char)slt;
		// 	(*file_ht)->buckets[bkt].slots[slt].filename.name_len = 0;
		// }
	}
}

void free_file_ht(struct HashTable **file_ht)
{
	kfree(*file_ht);
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


/* insert into file_hash 
 * return file_seg  
*/
static inline unsigned long insert_file(struct HashTable *file_ht, char * filename)
{	
	unsigned int hashcode = BKDRHash(filename);
	unsigned long bucket_id = (unsigned long)hashcode & ((1LU << (MIN_FILE_BUCKET_BITS)) - 1LU);
	__u8 slt = find_first_zero_bit(file_ht->buckets[bucket_id].slot_bitmap, 1 << FILE_SLOT_BITS);
	bitmap_set(file_ht->buckets[bucket_id].slot_bitmap, slt, 1);
	file_ht->buckets[bucket_id].valid_slot_count ++;

	// size_t namelen = my_strlen(filename);
	// //file_ht->buckets[bucket_id].slots[slt].filename = kmalloc(sizeof(struct ffs_name), GFP_NOIO);

	// memcpy(file_ht->buckets[bucket_id].slots[slt].filename.name, filename, namelen);
	// file_ht->buckets[bucket_id].slots[slt].filename.name_len = namelen;

	unsigned long file_seg = 0LU;
	//printk("file_seg1: %lx\n", file_seg);
	file_seg = file_seg | ((unsigned long)slt);
	//printk("file_seg2: %lx\n", file_seg);
	file_seg = file_seg | (bucket_id << FILE_SLOT_BITS);
	//printk("file_seg3: %lx, bkt_id: %lx\n", file_seg, bucket_id);
	return file_seg;
}


int delete_file(struct HashTable *file_ht, int bucket_id, int slot_id)
{
	if(bucket_id > (1 << MIN_FILE_BUCKET_BITS) || slot_id > (1 << FILE_SLOT_BITS))
		 return 0;
	//printk("start to delete file\n");
	if (!test_bit(slot_id, file_ht->buckets[bucket_id].slot_bitmap)) {
		return 0;
	}

	file_ht->buckets[bucket_id].valid_slot_count --;
	bitmap_clear(file_ht->buckets[bucket_id].slot_bitmap, slot_id, 1);
	//printk("bitmap: %x\n", *(file_ht->buckets[bucket_id].slot_bitmap));
	return 1;
}

struct ffs_inode_info* FFS_I(struct inode * inode){
	return container_of(inode, struct ffs_inode_info, vfs_inode);
}

//这里处理文件的inode和data的lba，注意两者的起始基址不同
lba_t compose_lba(int dir_id, int bucket_id, int slot_id, int flag){//flag: 0,inode 1,data
	lba_t lba_base = 0;
	lba_t lba = 0;
	if(flag == 0){//file inode区lba计算,按照bucket算
		// dump_stack();
		lba_base = 1LL << (FILE_SLOT_BITS + DEFAULT_FILE_BLOCK_BITS);
		//printk("compose_lba: lba_base = %lld", lba_base);
		if(slot_id != -1){
			lba = ((((lba_t)(dir_id) * BUCKETS_PER_DIR) + bucket_id) << FILE_SLOT_BITS);
			lba |= slot_id;
		}
		else{//文件inode所在bucket的slba
			//printk("compose_lba: dir_id = %d, bucket_id = %d", dir_id, bucket_id);
			lba = ((((lba_t)(dir_id) * BUCKETS_PER_DIR) + bucket_id) * SLOTS_PER_BUCKET);
			// lba += lba_base;
		}
			
	}
	else{//file data区的lba计算，按offset算
		lba_base = FILE_DATA_LBA_BASE;
		lba |= dir_id << (MIN_FILE_BUCKET_BITS + FILE_SLOT_BITS + DEFAULT_FILE_BLOCK_BITS);
		lba |= bucket_id << (FILE_SLOT_BITS + DEFAULT_FILE_BLOCK_BITS);
		lba |= slot_id << (DEFAULT_FILE_BLOCK_BITS);
		lba += lba_base;
	}

	return lba;
}

//文件的数据， 读写文件数据
lba_t ffs_get_lba_data(struct inode *inode, lba_t iblock) {
	
	struct ffs_inode_info* fi = FFS_I(inode);
	lba_t base = compose_lba(fi->dir_id, fi->bucket_id,fi->slot_id, 1);
	lba_t lba  = base | iblock << BLOCK_SHIFT;

	return lba;
}

//写文件的inode
lba_t ffs_get_lba_meta(struct inode *inode) {
	
	struct ffs_inode_info* fi = FFS_I(inode);
	lba_t lba = compose_lba(fi->dir_id, fi->bucket_id,fi->slot_id, 0);

	return lba;
}

//读取file inode:  1/512B,8/BUCKET,bucketsize=4kB，计算出文件所对应的bucket的lba，读盘，遍历bucket，判断文件存在性
lba_t ffs_get_lba_file_bucket(struct inode *parent,struct dentry *dentry, int dir_id){
	struct ffs_inode_info* p_fi = FFS_I(parent);
	char * name = (char *)(dentry->d_name.name);
	//计算bucketid
	unsigned int hashcode = BKDRHash(name);
	unsigned long bucket_id = (unsigned long)(hashcode & ((1LU << MIN_FILE_BUCKET_BITS) - 1LU));
	//获取bucket的起始lba
	lba_t file_bucket_slba = compose_lba(dir_id, bucket_id, -1, 0);
	return file_bucket_slba;
}

/*读写dir inode:  1/4kB, ino = -1:写 */
lba_t ffs_get_lba_dir_meta(unsigned long ino, int dir_id){
	if(ino != -1)
		dir_id = (ino - 1) >> (MIN_FILE_BUCKET_BITS + FILE_SLOT_BITS);
	// lba_t lba = (dir_id) << PAGE_SHIFT;
	//printk("lba_base: %lld\n", lba_base);
    lba_t lba =  dir_id;
	//printk("lba: %lld\n", lba);
	return lba;
}

/* 根据文件名分配slot，并返回ino */
unsigned long flatfs_file_slot_alloc_by_name(struct HashTable *hashtbl, struct inode *parent, int parent_dir_id, struct qstr *child)
{
	struct ffs_inode_info* p_fi = FFS_I(parent);
	char * name = (char *)(child->name);
	// strcpy(name, "fycnbb");
	//printk("start to insert file\n");
	unsigned long file_seg = insert_file(hashtbl, name);
	unsigned long ino = 0;

	ino = (file_seg | (parent_dir_id << (FILE_SLOT_BITS + MIN_FILE_BUCKET_BITS))); 
	//printk("flatfs_file_slot_alloc_by_name: file_seg:%lx, ino:%lx\n", file_seg, ino);
	return ino;	
}


int read_dir_files(struct HashTable *hashtbl, struct inode *inode, unsigned long ino, struct dir_context *ctx)
{
    unsigned long dir_id = (ino - 1) >> (MIN_FILE_BUCKET_BITS + FILE_SLOT_BITS);
	int bkt, slt;
	int pos = 0;
	struct super_block *sb = inode->i_sb;
	struct buffer_head *ibh = NULL;
	struct ffs_inode* raw_inode;
	sector_t pblk;


	//printk("ctx->pos:%d\n", ctx->pos);
	if(hashtbl->pos == 0)
	{
		bkt = -1;
		goto first;
	}

	for(bkt = 0; bkt < (1 << MIN_FILE_BUCKET_BITS); bkt++) {
		for(slt = 0; slt < (1 << FILE_SLOT_BITS); slt++ ) {
			if(pos >= hashtbl->pos) break;
			if (test_bit(slt, hashtbl->buckets[bkt].slot_bitmap)) 
				pos ++;
		}	
		if(pos >= hashtbl->pos) break;
	}

	for(1; slt < (1 << FILE_SLOT_BITS); slt++ ) {
		if (test_bit(slt, hashtbl->buckets[bkt].slot_bitmap)) {
			/* 开始传 */
			unsigned char d_type = NT_FILE;

			ino = (slt + ((bkt + (dir_id << MIN_FILE_BUCKET_BITS)) << FILE_SLOT_BITS));
			ibh = sb_bread(sb, ino);
			raw_inode = (struct ffs_inode*)(ibh->b_data);
			//printk("ino:%lx, dir_id:%x, bucket_id:%x, slot_id:%x, filename:%s\n", ino, dir_id, bkt, slt, fi->filename.name);
        	dir_emit(ctx, raw_inode->filename.name, raw_inode->filename.name_len, le32_to_cpu(ino), d_type);
        	__le16 dlen = 1;
        	/* 上下文指针原本指向目录项文件的位置，现在我们设计变了，改成了表示第pos个子目录 */
        	ctx->pos +=  le16_to_cpu(dlen);
			hashtbl->pos +=  le16_to_cpu(dlen);
		}
	}
	//printk("bkt:%d, slt:%d\n", bkt, slt);

first:
	for(bkt ++; bkt < (1 << MIN_FILE_BUCKET_BITS); bkt++) {
		for(slt = 0 ; slt < (1 << FILE_SLOT_BITS); slt++ ) {
			if (test_bit(slt, hashtbl->buckets[bkt].slot_bitmap)) {
				/* 开始传 */
				unsigned char d_type = NT_FILE;

				ino = (slt + ((bkt + (dir_id << MIN_FILE_BUCKET_BITS)) << FILE_SLOT_BITS));
				ibh = sb_bread(sb, ino);
				raw_inode = (struct ffs_inode*)(ibh->b_data);
				//printk("ino:%lx, dir_id:%x, bucket_id:%x, slot_id:%x, filename:%s\n", ino, dir_id, bkt, slt, fi->filename.name);
        		dir_emit(ctx, raw_inode->filename.name, raw_inode->filename.name_len, le32_to_cpu(ino), d_type);
        		__le16 dlen = 1;
        		/* 上下文指针原本指向目录项文件的位置，现在我们设计变了，改成了表示第pos个子目录 */
        		ctx->pos +=  le16_to_cpu(dlen);
				hashtbl->pos += le16_to_cpu(dlen);
			}
		}
	}
	brelse(ibh);
    return 0;
}

/* 根据文件名查找slot，生成文件的 <file, slot> 字段 */
static inline unsigned long get_file_seg(struct inode *inode, int dir_id, struct HashTable *file_ht, char * filename)
{	
	unsigned int hashcode = BKDRHash(filename);
	unsigned long bucket_id = (unsigned long)hashcode & ((1LU << MIN_FILE_BUCKET_BITS) - 1LU);
	int slt;
	int namelen = my_strlen(filename);
	struct super_block *sb = inode->i_sb;
	struct ffs_inode* raw_inode;
	struct ffs_inode_info* fi = FFS_I(inode);
	sector_t pblk;
	struct buffer_head *head;
	struct buffer_head *ibh;
	int first_read = 0;
	pblk = compose_lba(fi->dir_id, bucket_id, 0, 0);
	//printk("get_file_seg, bucket_id:%x, name:%s\n", bucket_id, filename);
	for(slt = 0; slt < (1 << FILE_SLOT_BITS); slt++) {
		if(!test_bit(slt, file_ht->buckets[bucket_id].slot_bitmap)) 
		{
			continue;
		}
		if(first_read == 0)
		{
			first_read = 1;
			head = sb_bread(sb, pblk + slt);
			wait_on_buffer(head);
			if (unlikely(!head)){
				//printk(KERN_ERR "allocate bh for ffs_inode fail");
				return -ENOMEM;
			}
			ibh = head;
			//printk("lookup, pblk = %lld, slt = %d, lba = %lld", pblk ,slt, ibh->b_blocknr);
		}
		else
		{
			ibh = head;
			// if(ibh)
			// 	printk("lookup, pblk = %lld, slt = %d, lba = %lld", pblk ,slt, ibh->b_blocknr);
			do {
				//printk("lookup, pblk = %lld, slt = %d, lba = %lld", pblk ,slt, ibh->b_blocknr);
  				if(ibh->b_blocknr == pblk +slt)
  					break;
				ibh = ibh->b_this_page;
 			} while (ibh != head);

			if(ibh->b_blocknr != pblk + slt)
			{
				printk("unlike to happened");
				ibh = sb_bread(sb, pblk + slt);
				wait_on_buffer(ibh);
				if (unlikely(!ibh)){
					//printk(KERN_ERR "allocate bh for ffs_inode fail");
					return -ENOMEM;
				}
				//printk("unlikely, pblk = %lld, slt = %d, lba = %lld", pblk ,slt, ibh->b_blocknr);
			}
		}

		raw_inode = (struct ffs_inode *) ibh->b_data;//b_data就是地址，我们的inode位于bh内部offset为0的地方
		//printk("ffs_dirty_inode: name:%s\n", raw_inode->filename.name);
		if(!strcmp(filename, raw_inode->filename.name))
		{
			break;
		}
	}
	if(first_read) brelse(head);
	unsigned long file_seg = 0LU;
	if(slt >= (1 << FILE_SLOT_BITS)) {
		return file_seg;
	}

	file_seg = file_seg | (unsigned long)slt;
	file_seg = file_seg | (bucket_id << FILE_SLOT_BITS);
	return file_seg;
}

/* 根据文件名查找ino */
unsigned long flatfs_file_inode_by_name(struct HashTable *hashtbl, struct inode *parent, int parent_dir_id, struct qstr *child)
{
	struct ffs_inode_info* p_fi = FFS_I(parent);
	char * name = (char *)(child->name);
	unsigned long file_seg = get_file_seg(parent, parent_dir_id, hashtbl, name);
	unsigned long ino = 0;

	if(file_seg == 0) {
		return ino;
	}

	ino = (file_seg | (parent_dir_id << (FILE_SLOT_BITS + MIN_FILE_BUCKET_BITS))); 
	return ino;	
}