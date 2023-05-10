#ifndef _TEST_H_
#define _TEST_H_
#include "flatfs_d.h"
#endif

struct ffs_inode_info* FFS_I(struct inode * inode);

unsigned int BKDRHash(char *str, int len);

/* create table */
void init_file_ht(struct HashTable **file_ht);

/* destroy table */
void free_file_ht(struct HashTable **file_ht);

/* unlink / delete */
int delete_file(struct HashTable *file_ht, struct inode *dir, int bucket_id, int slot_id);

/* lookup / find */
unsigned long flatfs_file_inode_by_name(struct HashTable *hashtbl, struct inode *parent, struct qstr *child, struct ffs_inode **raw_inode, struct buffer_head **bh);

/* create file / insert */
struct ffs_ino flatfs_file_slot_alloc_by_name(struct HashTable *hashtbl, struct inode *parent, struct qstr *child);

/* iterator read */
int read_dir_files(struct HashTable *hashtbl, struct inode *inode, ffs_ino_t ino, struct dir_context *ctx);

lba_t compose_file_lba(int dir_id, int bucket_id, int slot_id, int block_id, int flag);
lba_t compose_dir_lba(int dir_id);
ffs_ino_t compose_ino(int dir_id, int bucket_id, int slot_id, int flag);
lba_t ffs_get_data_lba(struct inode *inode, lba_t iblock);