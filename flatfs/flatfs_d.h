

#include <linux/list.h>
#include <linux/fs.h>
#include <linux/rwsem.h>
#include <linux/fscache.h>
#include <linux/list_sort.h>
#include <linux/slab.h>
#include <linux/ktime.h>
#include "cuckoo_hash.h"


#define BUCKET_NR 2500//一个bucket 4个slot，每个slot记录一个inode
//#define META_LBA_OFFSET //数据区域要从这里开始计算
#define TOTAL_DEPTH 8	//定义目录深度
#define MAX_DIR_INUM 255 //定义目录ino范围
/* helpful if this is different than other fs */
#define FLATFS_MAGIC 0x73616d70 /* "FLAT" */
#define FLATFS_BSTORE_BLOCKSIZE PAGE_SIZE
#define FLATFS_BSTORE_BLOCKSIZE_BITS PAGE_SHIFT
extern unsigned long calculate_slba(struct inode* dir, struct dentry* dentry);


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
};

static inline struct flatfs_sb_info *
FFS_SB(struct super_block *sb)
{
	return sb->s_fs_info; //文件系统特殊信息
}


