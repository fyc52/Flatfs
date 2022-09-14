#include <linux/slab.h>
#include <linux/module.h>
#include <linux/string.h>
#ifndef _TEST_H_
#define _TEST_H_
#include "flatfs_d.h"
#include "lba.h"
#endif

void init_file_ht(struct HashTable *file_ht)
{
	file_ht = kmalloc(sizeof(struct HashTable), GFP_NOIO);
	int bkt;
	for(bkt = 0; bkt < (1 << MIN_FILE_BUCKET_BITS); bkt++) {
		file_ht->buckets[bkt].bucket_id = bkt;
		bitmap_zero(file_ht->buckets[bkt].slot_bitmap, 1 << FILE_SLOT_BITS);
		/* 第一个slot固定用来存放该bucket下所有文件的inode信息 */
		// bitmap_set(file_ht->buckets[bkt].slot_bitmap, 0, 1);
		file_ht->buckets[bkt].valid_slot_count = 0;
		int slt;
		for(slt = 0; slt < (1 << FILE_SLOT_BITS); slt++ ) {
			file_ht->buckets[bkt].slots[slt].slot_id  = (unsigned char)slt;
			file_ht->buckets[bkt].slots[slt].filename.name_len = 0;
		}
	}
	
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
static unsigned long insert_file(struct HashTable *file_ht, char * filename)
{	
	unsigned int hashcode = BKDRHash(filename);
	unsigned long bucket_id = (unsigned long)hashcode & (1LU << (DEFAULT_FILE_BLOCK_BITS + FILE_SLOT_BITS) - 1LU);
	__u8 slt = find_first_zero_bit(file_ht->buckets[bucket_id].slot_bitmap, 1);
	bitmap_set(file_ht->buckets[bucket_id].slot_bitmap, slt, 1);
	file_ht->buckets[bucket_id].valid_slot_count ++;

	int namelen = strlen(filename);
	//file_ht->buckets[bucket_id].slots[slt].filename = kmalloc(sizeof(struct ffs_name), GFP_NOIO);

	memcpy(file_ht->buckets[bucket_id].slots[slt].filename.name, filename, namelen);
	file_ht->buckets[bucket_id].slots[slt].filename.name_len = namelen;

	unsigned long file_seg = 0LU;
	file_seg = file_seg | ((unsigned long)slt << DEFAULT_FILE_BLOCK_BITS);
	file_seg = file_seg | (bucket_id << (DEFAULT_FILE_BLOCK_BITS + FILE_SLOT_BITS));
	return file_seg;
}


static unsigned long get_file_seg(struct HashTable *file_ht, char * filename)
{	
	unsigned int hashcode = BKDRHash(filename);
	unsigned long bucket_id = (unsigned long)hashcode & ((1LU << MIN_FILE_BUCKET_BITS) - 1LU);
	int slt;
	for(slt = 1; slt < (1 << FILE_SLOT_BITS); slt++) {
		int namelen = strlen(filename);
		if(ffs_match(namelen, filename, &file_ht->buckets[bucket_id].slots[slt].filename))
			break;
	}

	unsigned long file_seg = 0LU;
	if(slt >= (1 << FILE_SLOT_BITS)) {
		return file_seg;
	}

	file_seg = file_seg | ((unsigned long)slt << DEFAULT_FILE_BLOCK_BITS);
	file_seg = file_seg | (bucket_id << (DEFAULT_FILE_BLOCK_BITS + FILE_SLOT_BITS));
	return file_seg;
}


static void delete_file(struct HashTable *file_ht, char * filename)
{
	unsigned int hashcode = BKDRHash(filename);
	unsigned long bucket_id = (unsigned long)hashcode & ((1LU << MIN_FILE_BUCKET_BITS) - 1LU);
	int slt;
	for(slt = 1; slt < (1 << FILE_SLOT_BITS); slt++) {
		int namelen = strlen(filename);
		if(ffs_match(namelen, filename, &file_ht->buckets[bucket_id].slots[slt].filename))
		{
			int name_pos;
			for(name_pos = 0; name_pos <= file_ht->buckets[bucket_id].slots[slt].filename.name_len; name_pos ++)
			{
				file_ht->buckets[bucket_id].slots[slt].filename.name[name_pos] = 0;
			}
			file_ht->buckets[bucket_id].slots[slt].filename.name_len = 0;
			file_ht->buckets[bucket_id].valid_slot_count --;
			break;
		}
	}
}

struct ffs_inode_info* FFS_I(struct inode * inode){
	return container_of(inode, struct ffs_inode_info, vfs_inode);
}

//这里处理文件的inode和data的lba，注意两者的起始基址不同
lba_t compose_lba(int dir_id, int bucket_id, int slot_id, int flag){//flag: 0,inode 1,data
	lba_t lba_base = 0;
	lba_t lba = 0;
	if(flag == 0){//file inode区lba计算,按照bucket算
		lba_base = FILE_META_LBA_BASE;
		if(slot_id != -1){
			lba |= (BUCKETS_PER_DIR * dir_id + bucket_id) << PAGE_SHIFT;
			lba |= slot_id << BLOCK_SHIFT;
			lba += lba_base;
		}
		else{//文件inode所在bucket的slba
			lba |= (BUCKETS_PER_DIR * dir_id + bucket_id)  << PAGE_SHIFT;
			lba += lba_base;
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
	char * name = dentry->d_name.name;
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
		dir_id = (ino-1) >> (MIN_FILE_BUCKET_BITS + FILE_SLOT_BITS);

	lba_t lba = (dir_id) << PAGE_SHIFT;
	return lba;
}


lba_t ffs_set_start_lba(struct HashTable* file_ht, char *filename)
{
	//TUDO
	int parent_ino = 0;
	lba_t lba_dir_seg  = parent_ino << (LBA_TT_BITS - MAX_DIR_BITS);
	lba_t lba_file_seg = insert_file(file_ht, filename);
	return (lba_dir_seg | lba_file_seg);
}

