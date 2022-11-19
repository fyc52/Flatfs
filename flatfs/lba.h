#ifndef _TEST_H_
#define _TEST_H_
#include "flatfs_d.h"
#endif

lba_t ffs_get_lba_data(struct inode *inode, lba_t iblock);
lba_t ffs_get_big_file_lba_data(struct inode *inode, lba_t iblock);
lba_t ffs_get_lba_meta(struct inode *inode);
lba_t ffs_get_big_file_lba_meta(struct inode *inode);
lba_t ffs_get_lba_file_bucket(struct inode *parent,struct dentry *dentry, int dir_id);
lba_t ffs_get_lba_dir_meta(unsigned long ino, int dir_id);
lba_t ffs_set_start_lba(struct HashTable* file_ht, char *filename);
struct ffs_inode_info* FFS_I(struct inode * inode);

static inline unsigned long inode_to_dir_id(unsigned long inode)
{
	return (inode - 1) >> (MIN_FILE_BUCKET_BITS + FILE_SLOT_BITS);
}

static inline unsigned long dir_id_to_inode(unsigned long dir_id)
{
	return (dir_id << (MIN_FILE_BUCKET_BITS + FILE_SLOT_BITS)) + 1;
}

unsigned int BKDRHash(char *str);
void init_file_ht(struct HashTable **file_ht);
void init_big_file_ht(struct Big_Dir_HashTable **file_ht);

void free_file_ht(struct HashTable **file_ht);
void free_big_file_ht(struct Big_Dir_HashTable **file_ht);

int delete_file(struct HashTable *file_ht, int bucket_id, int slot_id);
int delete_big_file(struct Big_Dir_HashTable *file_ht, int bucket_id, int slot_id);

unsigned long flatfs_file_inode_by_name(struct HashTable *hashtbl, struct inode *parent, int parent_dir_id, struct qstr *child, struct ffs_inode *raw_inode, struct buffer_head *bh);
unsigned long flatfs_big_file_inode_by_name(struct Big_Dir_HashTable *hashtbl, struct inode *parent, int parent_dir_id, struct qstr *child, struct ffs_inode *raw_inode, struct buffer_head *bh);

unsigned long flatfs_file_slot_alloc_by_name(struct HashTable *hashtbl, struct inode *parent, int parent_dir_id, struct qstr *child);
unsigned long flatfs_big_file_slot_alloc_by_name(struct Big_Dir_HashTable *hashtbl, struct inode *parent, int parent_dir_id, struct qstr *child);
int read_dir_files(struct HashTable *hashtbl, struct inode *inode, unsigned long ino, struct dir_context *ctx);
int read_big_dir_files(struct Big_Dir_HashTable *hashtbl, struct inode *inode, unsigned long ino, struct dir_context *ctx);
void print2log(struct HashTable *hashtbl);
int resize_dir(struct flatfs_sb_info *sb, int dir_id);