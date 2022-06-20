#ifndef FLATFS_H
#define FLATFS_H

#include <linux/list.h>
#include <linux/fs.h>
#include <linux/rwsem.h>
#include <linux/fscache.h>
#include <linux/list_sort.h>
#include <linux/slab.h>
#include <linux/ktime.h>


#define META_LBA_OFFSET//数据区域要从这里开始计算
#define FLATFS_ROOT_I 2
/* helpful if this is different than other fs */
#define FLATFS_MAGIC     0x73616d70 /* "FLAT" */
#define FLATFS_BSTORE_BLOCKSIZE		PAGE_SIZE
#define FLATFS_BSTORE_BLOCKSIZE_BITS	PAGE_SHIFT

struct ffs_lba{
unsigned long var;//ino
unsigned size;  
unsigned offset;
}

struct ffs_inode{//磁盘inode，仅用于恢复时读取
   unsigned int size;//尺寸
   unsigned long var; //ino

}

/* ffs在内存superblock */
struct flatfs_sb_info {//一般会包含信息和数据结构，kevin的db就是在这里实现的

	int flags;
	
};

static inline struct flatfs_sb_info *
FFS_SB(struct super_block *sb)
{
	return sb->s_fs_info;//文件系统特殊信息
}



#endif