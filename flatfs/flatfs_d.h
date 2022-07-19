

#include <linux/list.h>
#include <linux/fs.h>
#include <linux/rwsem.h>
#include <linux/fscache.h>
#include <linux/list_sort.h>
#include <linux/slab.h>
#include <linux/ktime.h>
#include <linux/bitmap.h>
#include <linux/dcache.h>
#include "cuckoo_hash.h"


#define BUCKET_NR 2500//一个bucket 4个slot，每个slot记录一个inode
//#define META_LBA_OFFSET //数据区域要从这里开始计算
#define TOTAL_DEPTH 8	//定义目录深度
#define MAX_DIR_INUM 255 //定义目录ino范围
/* helpful if this is different than other fs */
#define FLATFS_MAGIC 0x73616d70 /* "FLAT" */
#define FLATFS_BSTORE_BLOCKSIZE PAGE_SIZE
#define FLATFS_BSTORE_BLOCKSIZE_BITS PAGE_SHIFT

#define lba_size unsigned long
#define INIT_SPACE 10

/* LBA分配设置 */
#define MAX_DIR_BITS 15
#define MIN_DIR_BITS 7

#define MAX_FILE_BUCKET_BITS 20
#define MIN_FILE_BUCKET_BITS 12

#define FILE_SLOT_BITS 3

#define MAX_FILE_BLOCK_BITS 40
#define MIN_FILE_BLOCK_BITS 16
#define DEFAULT_FILE_BLOCK_BITS 32

enum {
    ENOINO = 0
};


struct ffs_lba
{
	unsigned long var; // ino
	unsigned size;
	unsigned offset;
};
//在磁盘存放的位置：lba=0+ino；（lba0用于存sb）
struct ffs_inode
{					   //磁盘inode，仅用于恢复时读取
	loff_t size; //尺寸
	unsigned long var; // ino/lba,充当数据指针
};

/* ffs在内存superblock */
struct flatfs_sb_info
{ //一般会包含信息和数据结构，kevin的db就是在这里实现的
	cuckoo_hash_t *cuckoo;
	struct dir_entry * root;
};

static inline struct flatfs_sb_info *
FFS_SB(struct super_block *sb)
{
	return sb->s_fs_info; //文件系统特殊信息
}

struct ffs_dir_name {
	__u8	name_len;		/* Name length */
	char	name[];			/* Dir name */
};

struct dir_entry {
    /* 目录的inode num */
    unsigned long ino;
    /* 目录名 */
    struct ffs_dir_name *dir_name;

    /* LBA分配上，每一个字段占的位数 */
    unsigned short dir_bits;
    unsigned short bucket_bits;
    unsigned short slot_bits;
    unsigned short block_bits;

    /* 指向子目录的指针 */
    struct dir_entry **subdirs;
    /* 当前子目录数组申请的空间大小 */
    unsigned long space;
    /* 子目录的个数 */
    unsigned long dir_size;
    
};

DECLARE_BITMAP(ino_bitmap, 1 << MAX_DIR_BITS);

static inline void init_ino_bitmap() {
    bitmap_zero(init_ino_bitmap, 1 << MAX_DIR_BITS);
}

static inline unsigned long get_unused_ino() {
    unsigned long ino;
    ino = find_first_zero_bit(init_ino_bitmap, 1);
    if(ino == 1 << MAX_DIR_BITS) {
        return ENOINO;
    }
    /* root的inode num设定为1， 那么其他目录的ino从2开始编号 */
    ino += 2;
    return ino;
}

extern unsigned long calculate_slba(struct inode* dir, struct dentry* dentry);
unsigned long flatfs_inode_by_name(struct inode *dir, struct dentry *dentry);