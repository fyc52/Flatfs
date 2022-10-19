#ifndef _TEST_H_
#define _TEST_H_
#include "flatfs_d.h"
#endif

lba_t ffs_get_lba_data(struct inode *inode, lba_t iblock);
lba_t ffs_get_lba_meta(struct inode *inode);
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
unsigned long flatfs_file_inode_by_name(struct HashTable *hashtbl, struct inode *parent, int parent_dir_id, struct qstr *child);
unsigned long flatfs_file_slot_alloc_by_name(struct HashTable *hashtbl, struct inode *parent, int parent_dir_id, struct qstr *child);