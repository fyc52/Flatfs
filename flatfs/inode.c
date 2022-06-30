#include <linux/module.h>
//#include <stdlib.h>//内核模块不能使用
#include <linux/fs.h>
#include "flatfs_d.h"


extern struct dentry_operations ffs_dentry_ops;
extern struct dentry_operations ffs_ci_dentry_ops;
extern struct inode *flatfs_get_inode(struct super_block *sb, int mode, 
					dev_t dev);
extern unsigned long calculate_slba(struct inode* dir, struct dentry* dentry);
extern struct inode_operations ffs_dir_inode_ops;
extern struct inode_operations ffs_file_inode_ops;
extern struct file_operations ffs_file_file_ops;
extern struct address_space_operations ffs_aops;
extern struct file_operations ffs_dir_operations;


unsigned long flatfs_inode_by_name(struct inode *dir, struct dentry *dentry){
	//todo:分配ino==slba,无需设置inode_bitmap;以后要改成从字符串计算得到ino，下面先直接用文件名等于ino编号
	return simple_strtoul(dentry->d_name.name, NULL, 0);
	//return calculate_slba(dir,dentry);
}

//调用具体文件系统的lookup函数找到当前分量的inode，并将inode与传进来的dentry关联（通过d_splice_alias()->__d_add）
//dir:父目录的inode；
//dentry：本目录的dentry，需要关联到本目录的inode
static struct dentry *ffs_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)	
{
	int r, err;
	struct dentry *ret;
	struct inode *inode;
	unsigned long ino = flatfs_inode_by_name(dir, dentry);	//不用查询目录文件，计算出ino
	loff_t size;
	//判断inode是否存在？
	struct flatfs_sb_info *ffs_sb = dir->i_sb->s_fs_info; 
	cuckoo_hash_t* ht = ffs_sb->cuckoo;

	if(cuckoo_query(ht, ino, size) == FAIL){
		printk(KERN_INFO "flatfs lookup, ino: %lu, size: %llu\n", ino, size);//调试
		inode = NULL;
		goto out;
	}
	printk(KERN_INFO "flatfs lookup, ino: %lu, size: %llu\n", ino, size);//调试
	/*从挂载的文件系统里寻找inode,仅用于处理内存icache*/
	inode = iget_locked(dir->i_sb, ino);//目录dentry、inode全缓存，这里会命中
	
	if(ino > MAX_DIR_INUM){
		inode->i_size = size;											
		inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
		inode->i_uid = dir->i_uid;
		inode->i_gid = dir->i_gid;
		inode->i_rdev=dir->i_sb->s_dev;
		inode->i_mapping->a_ops = &ffs_aops;
		inode->i_mode |= S_IFREG ;
		inode->i_op = &ffs_file_inode_ops;
		inode->i_fop = &ffs_file_file_ops;
		set_nlink(inode,1);//不允许硬链接，常规文件的nlink固定为1
	}
	else{
		if(inode->i_mode != S_IFDIR)
			printk(KERN_ALERT "flatfs err in inode type %u\n ", inode->i_mode & S_IFMT);
	}
out:
	return d_splice_alias(inode, dentry);//将inode与dentry绑定
}

void ffs_add_entry(struct inode *dir){
	loff_t dir_size = i_size_read(dir);
	i_size_write(dir, dir_size + 1);//父目录vfs inode i_size+1
	mark_inode_dirty(dir);//标记父目录为脏
}

static int
ffs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev)
{
	struct inode * inode = flatfs_get_inode(dir->i_sb, mode, dev);//分配VFS inode
	int error = -ENOSPC;
	struct flatfs_sb_info *ffs_sb = dir->i_sb->s_fs_info; 
	cuckoo_hash_t* ht = ffs_sb->cuckoo;

	inode->i_ino = flatfs_inode_by_name(dir,dentry);//为新inode分配ino#
	printk(KERN_INFO "flatfs: mknod ino=%lu\n",inode->i_ino);
	if (inode) {
		//spin_lock(dir->i_lock);
		if((mode & S_IFMT)==S_IFDIR)
			dget(dentry);   /* 这里额外增加dentry引用计数从而将dentry常驻内存，仅针对目录 */

		mark_inode_dirty(inode);	//为ffs_inode分配缓冲区，标记缓冲区为脏，并标记inode为脏
		d_instantiate(dentry, inode);//将dentry和新创建的inode进行关联
		
		ffs_add_entry(dir);//写父目录
		printk(KERN_INFO "flatfs: mknod dir size is = %llu\n",dir->i_size);

		if(cuckoo_insert(ht, inode->i_ino, 0)==FAIL){
			cuckoo_resize(ht);
			cuckoo_insert(ht, inode->i_ino, 0);
		}
		cuckoo_update(ht, dir->i_ino, i_size_read(dir));
		
		//spin_unlock(dir->i_lock);
		//同步数据
		return 0;
	}
	return error;
}


static int ffs_mkdir(struct inode * dir, struct dentry * dentry, umode_t mode)
{
	// printk(KERN_ALERT "--------------[mkdir] dump_stack start----------------");
	// dump_stack();
	// printk(KERN_ALERT "--------------[mkdir] dump_stack end----------------");
	printk(KERN_INFO "flatfs mkdir");
	int ret = ffs_mknod(dir, dentry, mode | S_IFDIR, 0);
	if (!ret)
		inc_nlink(dir);
	return ret;
}


static int ffs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	// to do: 删除磁盘 inode(lba通过inode->ino转换而来)
	inode_dec_link_count(inode);//drop_nlink & mark_inode_dirty
	loff_t dir_size = i_size_read(dir);
	i_size_write(dir, dir_size - 1);
	mark_inode_dirty(dir);
	//to do:减少全局size table中父目录的size,并更新磁盘上父目录的inode.size
	return 0;
}

static int ffs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct inode * inode = d_inode(dentry);
	loff_t dir_size;
	// to do: 考虑是否处理子目录和子文件
	
	int err = ffs_unlink(dir,dentry);
	if(!err){
		inode->i_size = 0;
		inode_dec_link_count(inode);
		inode_dec_link_count(dir);
	}

	

	return 0;
}



static int ffs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
	printk(KERN_INFO "flatfs create");
	// printk(KERN_ALERT "--------------[create] dump_stack start----------------");
	// dump_stack();
	// printk(KERN_ALERT "--------------[create] dump_stack end----------------");
	return ffs_mknod(dir, dentry, mode | S_IFREG, 0);
}

// static int ffs_get_link(struct inode * dir, struct dentry *dentry, 
// 			const char * symname)
// {
// 	struct inode *inode;
// 	int error = -ENOSPC;

// 	inode = samplefs_get_inode(dir->i_sb, S_IFLNK|S_IRWXUGO, 0);
// 	if (inode) {
// 		int l = strlen(symname)+1;
// 		error = page_symlink(inode, symname, l);
// 		if (!error) {
// 			if (dir->i_mode & S_ISGID)
// 				inode->i_gid = dir->i_gid;
// 			d_instantiate(dentry, inode);
// 			dget(dentry);
// 			dir->i_mtime = dir->i_ctime = CURRENT_TIME;
// 		} else
// 			iput(inode);
// 	}
// 	return error;
// }
struct inode_operations ffs_file_inode_ops = {
    .setattr	= simple_setattr,
	.getattr	= simple_getattr,
};

// static int flatfs_symlink(struct inode * dir, struct dentry *dentry, const char * symname)
// {
// 	struct inode *inode;
// 	int error = -ENOSPC;

// 	inode = flatfs_get_inode(dir->i_sb, dir, S_IFLNK|S_IRWXUGO, 0);
// 	if (inode) {
// 		int l = strlen(symname)+1;
// 		error = page_symlink(inode, symname, l);
// 		if (!error) {
// 			d_instantiate(dentry, inode);
// 			dget(dentry);
// 			dir->i_mtime = dir->i_ctime = current_time(dir);
// 		} else
// 			iput(inode);
// 	}
// 	return error;
// }

struct inode_operations ffs_dir_inode_ops = {
	.create         = ffs_create,
	.lookup         = ffs_lookup,//to do : change to ffs_lookup
	.link			= simple_link,
	.unlink         = ffs_unlink,
	//.symlink		= flatfs_symlik,
	.mkdir          = ffs_mkdir,
	.rmdir          = ffs_rmdir,
	.mknod          = ffs_mknod,	//该函数由系统调用mknod（）调用，创建特殊文件（设备文件、命名管道或套接字）。要创建的文件放在dir目录中，其目录项为dentry，关联的设备为rdev，初始权限由mode指定。
	.rename         = simple_rename,
};


