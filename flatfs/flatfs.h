#ifndef FLATFS_H
#define FLATFS_H

#include <linux/list.h>
#include <linux/fs.h>
#include <linux/rwsem.h>
#include <linux/fscache.h>
#include <linux/list_sort.h>
#include <linux/slab.h>
#include <linux/ktime.h>



#define META_LBA_OFFSET //数据区域要从这里开始计算
#define TOTAL_DEPTH 8	//定义目录深度
/* helpful if this is different than other fs */
#define FLATFS_MAGIC 0x73616d70 /* "FLAT" */
#define FLATFS_BSTORE_BLOCKSIZE PAGE_SIZE
#define FLATFS_BSTORE_BLOCKSIZE_BITS PAGE_SHIFT



struct ffs_lba
{
	unsigned long var; // ino
	unsigned size;
	unsigned offset;
};
//在磁盘存放的位置：lba=0+ino；（lba0用于存sb）
struct ffs_inode
{					   //磁盘inode，仅用于恢复时读取
	unsigned int size; //尺寸
	unsigned long var; // ino/lba,充当数据指针
};

/* ffs在内存superblock */
struct flatfs_sb_info
{ //一般会包含信息和数据结构，kevin的db就是在这里实现的

	int flags;
};

static inline struct flatfs_sb_info *
FFS_SB(struct super_block *sb)
{
	return sb->s_fs_info; //文件系统特殊信息
}


char calculate_filename(char *name)
{
	int i = 0;
	char s;

	while ( name[i] && name[i] != '.' )
	{
		s ^= name[i];
		i++;
	}
	return s;
}

unsigned long calculate_part_lba(char s, int depth)
{
	unsigned long plba = 0x00000000UL;

	plba = plba + s << (TOTAL_DEPTH * 8 - depth * 8);
	return plba;
}

int parse_depth(unsigned long ino){
	int depth = 1;
	uint8_t mask1 = 255;//设定一级目录8bit

	if(!ino)
		return 0;//at root dir
	for(;;){
		if((ino & (mask1 << (TOTAL_DEPTH * 8 - depth * 8) )) == 0)
			break;
		depth++;
	}
	return depth;
}

unsigned long calculate_slba(struct inode* dir, struct dentry* dentry)
{
	char *name = dentry->d_name.name;
	unsigned long var = dir->i_ino;
	// struct dentry* tem_den = dir->i_dentry;
	// //是否为mount root？dentry对象的d_parent指针设置为指向自身是判断一个dentry对象是否是一个fs的根目录的唯一准则
	// if( tem_den->d_parent == tem_den ){
	// 	return 0x00000000UL;
	// } 
	unsigned long plba = calculate_part_lba(calculate_filename(name), (parse_depth(var)+1));
	var = var & plba;

	return var;
}

#endif