#include <linux/module.h>
#include <linux/fs.h>
#include "flatfs.h"

extern struct dentry_operations ffs_dentry_ops;
extern struct dentry_operations ffs_ci_dentry_ops;
extern struct inode *flatfs_get_inode(struct super_block *sb, int mode, 
					dev_t dev);


static struct dentry *ffs_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)	
{
	printk(KERN_INFO "flatfs lookup");
	printk(KERN_ALERT "--------------[lookup] dump_stack start----------------");
	dump_stack();
	printk(KERN_ALERT "--------------[lookup] dump_stack end----------------");
	// struct flatfs_sb_info * ffs_sb = FFS_SB(dir->i_sb);
	// if (dentry->d_name.len > NAME_MAX)
	// 	return ERR_PTR(-ENAMETOOLONG);
	// if (ffs_sb->flags & ffs_MNT_CASE)
	// 	dentry->d_op = &ffs_ci_dentry_ops;
	// else
	// 	dentry->d_op = &ffs_dentry_ops;

	// d_add(dentry, NULL);
	return NULL;
}

static int
ffs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev)
{
	struct inode * inode = flatfs_get_inode(dir->i_sb, mode, dev);
	int error = -ENOSPC;
	
	printk(KERN_INFO "flatfs: mknod\n");
	if (inode) {
		if (dir->i_mode & S_ISGID) {
			inode->i_gid = dir->i_gid;
			if (S_ISDIR(mode))
				inode->i_mode |= S_ISGID;
		}
		d_instantiate(dentry, inode);//将dentry和新创建的inode进行关联,只有目录类型的inode才会调用该函数指针。
		dget(dentry);   /* Extra count - pin the dentry in core */
		error = 0;
		dir->i_mtime = dir->i_ctime = current_time(dir);

	}
	return error;
}


static int ffs_mkdir(struct inode * dir, struct dentry * dentry, umode_t mode)
{
	// printk(KERN_ALERT "--------------[mkdir] dump_stack start----------------");
	// dump_stack();
	// printk(KERN_ALERT "--------------[mkdir] dump_stack end----------------");
	printk(KERN_INFO "flatfs mkdir");
	int retval = 0;
	
	retval = ffs_mknod(dir, dentry, mode | S_IFDIR, 0);

	if (!retval)
		inc_nlink(dir);
	return retval;
}

static int ffs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
	printk(KERN_INFO "flatfs create");
	printk(KERN_ALERT "--------------[create] dump_stack start----------------");
	dump_stack();
	printk(KERN_ALERT "--------------[create] dump_stack end----------------");
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

static int flatfs_symlink(struct inode * dir, struct dentry *dentry, const char * symname)
{
	struct inode *inode;
	int error = -ENOSPC;

	inode = flatfs_get_inode(dir->i_sb, dir, S_IFLNK|S_IRWXUGO, 0);
	if (inode) {
		int l = strlen(symname)+1;
		error = page_symlink(inode, symname, l);
		if (!error) {
			d_instantiate(dentry, inode);
			dget(dentry);
			dir->i_mtime = dir->i_ctime = current_time(dir);
		} else
			iput(inode);
	}
	return error;
}

struct inode_operations ffs_dir_inode_ops = {
	.create         = ffs_create,
	.lookup         = simple_lookup,
	.link			= simple_link,
	.unlink         = simple_unlink,
	//.symlink		= flatfs_symlik,
	.mkdir          = ffs_mkdir,
	.rmdir          = simple_rmdir,
	.mknod          = ffs_mknod,	//该函数由系统调用mknod（）调用，创建特殊文件（设备文件、命名管道或套接字）。要创建的文件放在dir目录中，其目录项为dentry，关联的设备为rdev，初始权限由mode指定。
	.rename         = simple_rename,
};


