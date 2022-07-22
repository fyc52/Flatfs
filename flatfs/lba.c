#include <malloc.h>
#include <string.h>
#include "flatfs_d.h"


void init_file_ht(struct HashTable *file_ht)
{
	file_ht = (HashTable *)malloc(sizeof(HashTable));
	
	for(int bkt = 0; bkt < (1 << MIN_FILE_BUCKET_BITS); bkt++) {
		file_ht->buckets[bkt].bucket_id = bkt;
		bitmap_zero(file_ht->buckets[bkt].slot_bitmap, 1 << FILE_SLOT_BITS);
		/* 第一个slot固定用来存放该bucket下所有文件的inode信息 */
		bitmap_set(file_ht->buckets[bkt].slot_bitmap, 0, 1);
		file_ht->buckets[bkt].valid_slot_count = 0;

		for(int slt = 0; slt < (1 << FILE_SLOT_BITS); slt++ ) {
			file_ht->buckets[bkt].slots[slt].slot_id  = (unsigned char)slt;
			file_ht->buckets[bkt].slots[slt].filename.name_len = 0;
		}
	}
	
}


// BKDR Hash Function
static unsigned int BKDRHash(char *str)
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
	__u8 slt = find_first_zero_bit(file_ht->buckets[bucket_id].slot_bitmap);
	bitmap_set(file_ht->buckets[bucket_id].slot_bitmap, slt, 1);
	file_ht->buckets[bkt].valid_slot_count ++;

	int namelen = strlen(filename);
	file_ht->buckets[bucket_id].slots[slt].filename.name = (char *)malloc(namelen + 1);
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
	unsigned long bucket_id = (unsigned long)hashcode & (1LU << (DEFAULT_FILE_BLOCK_BITS + FILE_SLOT_BITS) - 1LU);
	for(int slt = 1; slt < (1 << FILE_SLOT_BITS); slt++) {
		int namelen = strlen(filename);
		if(ffs_match(namelen, filename, file_ht->buckets[bucket_id].slots[slt].filename))
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
	unsigned long bucket_id = (unsigned long)hashcode & (1LU << (DEFAULT_FILE_BLOCK_BITS + FILE_SLOT_BITS) - 1LU);
	for(int slt = 1; slt < (1 << FILE_SLOT_BITS); slt++) {
		int namelen = strlen(filename);
		if(ffs_match(namelen, filename, file_ht->buckets[bucket_id].slots[slt].filename))
		{
			free(file_ht->buckets[bucket_id].slots[slt].filename.name);
			file_ht->buckets[bucket_id].slots[slt].filename.name_len = 0;
			file_ht->buckets[bkt].valid_slot_count --;
			break;
		}
	}
}

struct ffs_inode_info* FFS_I(struct* inode){
	return container_of(inode, struct ffs_inode_info, vfs_inode);
}

//to do: 需要能兼顾计算目录和文件的inode以及文件的数据
lba_t ffs_get_lba(struct inode *inode, lba_t iblock) {
	
	struct ffs_inode_info* ffs_i = FFS_I(inode);
	
	lba_t base = ffs_i->lba;
	lba_t lba  = base + iblock;

	return lba;
}


lba_t ffs_set_start_lba(struct HashTable* file_ht, char *filename)
{
	lba_t lba_dir_seg  = parent_ino << (LBA_TT_BITS - MAX_DIR_BITS);
	lba_t lba_file_seg = insert_file(file_ht, filename);
	return (lba_dir_seg | lba_file_seg);
}

