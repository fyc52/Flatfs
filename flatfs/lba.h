#ifndef _TEST_H_
#define _TEST_H_
#include "flatfs_d.h"
#endif

lba_t ffs_get_lba_data(struct inode *inode, lba_t iblock);
lba_t ffs_get_lba_meta(struct inode *inode);
lba_t ffs_get_lba_file_bucket(struct inode *parent,struct dentry *dentry, int dir_id);
lba_t ffs_get_lba_dir_meta(unsigned long ino, int dir_id);
lba_t ffs_set_start_lba(struct HashTable* file_ht, char *filename);
unsigned int BKDRHash(char *str);