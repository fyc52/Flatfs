#ifndef _TEST_H_
#define _TEST_H_
#include "flatfs_d.h"
#endif


lba_t ffs_get_data_lba(struct inode *inode, lba_t iblock, int tag);
lba_t ffs_get_meta_lba(struct inode *inode, int tag);

struct ffs_inode_info* FFS_I(struct inode * inode);

static inline unsigned inode_to_dir_id(ffs_ino_t ino)
{
	return ino;
}

static inline ffs_ino_t dir_id_to_inode(unsigned dir_id)
{
	return dir_id;
}

unsigned int BKDRHash(char *str);

/* create table */
void init_file_ht(struct HashTable **file_ht);

/* destroy table */
void free_file_ht(struct HashTable **file_ht, int tag);

/* unlink / delete */
int delete_file(struct HashTable *file_ht, int bucket_id, int slot_id);

/* lookup / find */
unsigned long flatfs_file_inode_by_name(struct HashTable *hashtbl, struct inode *parent, struct qstr *child, struct ffs_inode **raw_inode, struct buffer_head **bh);

/* create file / insert */
unsigned long flatfs_file_slot_alloc_by_name(struct HashTable *hashtbl, struct inode *parent, int parent_dir_id, struct qstr *child);

/* iterator read */
int read_dir_files(struct HashTable *hashtbl, struct inode *inode, unsigned long ino, struct dir_context *ctx);
int read_big_dir_files(struct HashTable *hashtbl, struct inode *inode, unsigned long ino, struct dir_context *ctx);

void print2log(struct HashTable *hashtbl);
int resize_dir(struct flatfs_sb_info *sb, int dir_id);

lba_t compose_lba_large_hash(int dir_id, int bucket_id, int slot_id, int block_id, int flag);
lba_t compose_lba_small_hash(int dir_id, int bucket_id, int slot_id, int block_id, int flag);
lba_t compose_lba(int dir_id, int bucket_id, int slot_id, int block_id, int flag, int tag);