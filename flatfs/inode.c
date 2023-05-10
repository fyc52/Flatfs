#include <linux/module.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/blk_types.h>
#include <linux/namei.h>
#include <linux/backing-dev.h>

#ifndef _TEST_H_
#define _TEST_H_
#include "flatfs_d.h"
#include "lba.h"
#endif

extern struct inode *flatfs_get_inode(struct super_block *sb, int mode, dev_t dev);

//调用具体文件系统的lookup函数找到当前分量的inode，并将inode与传进来的dentry关联（通过d_splice_alias()->__d_add）
//dir:父目录的inode；
//dentry：本目录的dentry，需要关联到本目录的inode
static struct dentry *ffs_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)	
{
	//printk(KERN_INFO "flatfs: lookup, name = %s\n", dentry->d_name.name);
	struct inode *inode;
	unsigned long ino = 0;
	struct buffer_head *bh = NULL;
	struct ffs_inode *raw_inode = NULL;
	struct flatfs_sb_info *ffs_sb = dir->i_sb->s_fs_info;
	// struct page* page; 
	int dir_id;
	int bucket_id;
	int slot_id = 0;
	struct ffs_ino ffs_ino;
	sector_t pblk;
	umode_t mode = 0;

	if (dentry->d_name.len > FFS_MAX_FILENAME_LEN)
		goto out;
	
	//printk(KERN_INFO "flatfs: flatfs_dir_inode_by_name\n");
	ino = flatfs_dir_inode_by_name(dir->i_sb->s_fs_info, dir->i_ino, &dentry->d_name);
	dir_id = dir->i_ino;
	//printk(KERN_INFO "flatfs: flatfs_dir_inode_by_name, dir_id = %d\n", dir_id);
	/* 子目录树没有找到，前往hashtbl查询子文件 */
	if(ino == 0) {
		ino = flatfs_file_inode_by_name(ffs_sb->hashtbl[dir_id], dir, &dentry->d_name, &raw_inode, &bh);
	}
	else {
		printk(KERN_INFO "flatfs: get ino = %lx\n", ino);
		printk("lookup dir name: %s\n", dentry->d_name.name);
		pblk = compose_dir_lba(ino);
		bh = sb_bread(dir->i_sb, pblk);//这里不使用bread，避免读盘
		raw_inode = (struct ffs_inode *) bh->b_data;
		mode = S_IFDIR;
		goto get_dir;
	}
		
	/* 子目录树和子文件中均没找到，说明没有这个子文件/目录 */
	if(ino == 0) { 
		inode = NULL;
		//printk(KERN_INFO "inode is NULL\n");
		goto out;
	}
	else {
		//printk("lookup file name: %s\n", dentry->d_name.name);
		mode = S_IFREG;
	}
	//printk(KERN_INFO "flatfs: get ino = %lx\n", ino);
	/* 有子文件/目录 */
get_dir:
	inode = iget_locked(dir->i_sb, ino);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW)) {
		/* 在内存中有最新的inode，直接结束 */
		//printk(KERN_INFO "flatfs: new inode OK\n");
		goto out;
	}
	if(unlikely(!raw_inode)) {
		goto out;
	}
	/* 
	 * 有子文件/目录，但是在内存中没有最新的inode，需要读盘 
	 * 一般这个函数开机的时候才会调用，按照我们的设计，这个时候只可能是文件，
	 * 因为目录树存在内存了
	*/
	//printk(KERN_INFO "flatfs: get raw_inode OK\n");

	struct ffs_inode_info *fi = FFS_I(inode);
	fi->valid = 1;
	
	// 用盘内inode赋值inode操作
	inode->i_size = raw_inode->size;											
	inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
	inode->i_atime.tv_nsec = inode->i_mtime.tv_nsec = inode->i_ctime.tv_nsec = 0;
	inode->i_uid = current_fsuid();											/* Low 16 bits of Owner Uid */
	inode->i_gid = current_fsgid();	
	//inode->i_rdev = dir->i_sb->s_dev;
	inode->i_mapping->a_ops = &ffs_aops;
	fi->size = raw_inode->size;
	if (!fi->filename.name_len) {
		fi->filename.name_len = dentry->d_name.len;
		memcpy(fi->filename.name, dentry->d_name.name, fi->filename.name_len);
	}
	if (mode == S_IFDIR) {
		printk("looup dir ok\n");
		inode->i_mode = mode;
		inode->i_op = &ffs_dir_inode_ops;
		inode->i_fop = &ffs_dir_operations;
		fi->inode_type = DIR_INODE;
	}
	else {
		//printk("looup file ok\n");
		inode->i_mode = mode;
		inode->i_op = &ffs_file_inode_ops;
		inode->i_fop = &ffs_file_file_ops;
		fi->inode_type = FILE_INODE;
	}
	inc_nlink(inode);		
	if(inode) unlock_new_inode(inode);
	
out:
	if(bh) brelse(bh);
	return d_splice_alias(inode, dentry);//将inode与dentry绑定
}


static int
ffs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev)
{
	struct inode *inode = flatfs_get_inode(dir->i_sb, mode, dev);//分配VFS inode
	int error = -ENOSPC;
	struct flatfs_sb_info *ffs_sb = dir->i_sb->s_fs_info; 
	struct ffs_inode_info *fi = FFS_I(inode);
	ffs_ino_t ino = 0;
	int mknod_is_dir = mode & S_IFDIR;
	struct ffs_ino ffs_ino;
	struct ffs_ino ffs_dir_ino;
	ffs_dir_ino.ino = dir->i_ino;
	
	// 为新inode分配ino#
	if(mknod_is_dir) {
		unsigned long dir_id = fill_one_dir_entry(dir->i_sb->s_fs_info, dentry->d_name.name);
		printk("mknod dir id: %lu\n", dir_id);
		printk("mknod dir name: %s\n", dentry->d_name.name);
		// unsigned long parent_ino = dir->i_ino;
		insert_dir(dir->i_sb->s_fs_info, ffs_dir_ino.dir_seg.dir, dir_id);
		init_file_ht(&(ffs_sb->hashtbl[dir_id]));
		ino = dir_id;
		fi->valid = 1;
		fi->inode_type = DIR_INODE;
		fi->size = 0;
		fi->filename.name_len = dentry->d_name.len;
		memcpy(fi->filename.name, dentry->d_name.name, fi->filename.name_len);
	}
	else {
		// printk(KERN_INFO "flatfs: create\n");
		int dir_id = ffs_dir_ino.dir_seg.dir;
		//printk(KERN_INFO "flatfs: pdir_id = %d and file_name = %s\n", dfi->dir_id, (char *)(dentry->d_name.name));
		ffs_ino = flatfs_file_slot_alloc_by_name(ffs_sb->hashtbl[dir_id], dir, &dentry->d_name);
		if (ffs_ino.ino == INVALID_INO) 
		{
			return -1;
		}
		ino = ffs_ino.ino;
		fi->valid = 1;
		fi->size = 0;
		fi->inode_type = FILE_INODE;
		fi->filename.name_len = dentry->d_name.len;
		memcpy(fi->filename.name, dentry->d_name.name, fi->filename.name_len);
		printk("mknod --- ino:%ld, dir_id:%d, bucket_id:%d, slot_id:%d\n", ino, ffs_ino.file_seg.dir, ffs_ino.file_seg.bkt, ffs_ino.file_seg.slot);
	}

	inode->i_ino = ino;
	//printk(KERN_INFO "flatfs: mknod ino=%lu\n",inode->i_ino);
	if (inode) {

		//dget(dentry);   /* 这里额外增加dentry引用计数从而将dentry常驻内存,仅用于调试 */
		insert_inode_locked(inode);//将inode添加到inode hash表中，并标记为I_NEW
		//mark_inode_dirty(inode);	//为ffs_inode分配缓冲区，标记缓冲区为脏，并标记inode为脏
		if(inode) unlock_new_inode(inode);
		d_instantiate(dentry, inode);//将dentry和新创建的inode进行关联
		error = 0;
	}
	return error;
}

static int ffs_rename (struct inode * old_dir, struct dentry * old_dentry,
			struct inode * new_dir,	struct dentry * new_dentry,
			unsigned int flags)
{
	return 0;
}


static int ffs_mkdir(struct inode * dir, struct dentry * dentry, umode_t mode)
{
	int ret = ffs_mknod(dir, dentry, mode | S_IFDIR, 0);
	if (!ret)
		inc_nlink(dir);
	return ret;
}


static int ffs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct flatfs_sb_info *ffs_sb = FFS_SB(dir->i_sb); 
	struct inode *inode = dentry->d_inode;
	struct ffs_inode_info* fi = FFS_I(inode);
	struct ffs_ino ffs_ino;
	int err;
	int start;

	if(fi->valid == 0 || fi->inode_type == DIR_INODE) goto out;
	/*delete file in hashtbl*/
	ffs_ino.ino = inode->i_ino;
	err = delete_file(ffs_sb->hashtbl[ffs_ino.file_seg.dir], ffs_ino.file_seg.bkt, ffs_ino.file_seg.slot);
	if (!err) {
		printk("unlink failed, dirid: %d, bucketid: %d, slotid: %d\n", ffs_ino.file_seg.dir, ffs_ino.file_seg.bkt, ffs_ino.file_seg.slot);
		goto out;
	}
	
	/* mark inode invalid */
	fi->valid = 0;
	for(start = 0; start < fi->filename.name_len; start ++)
	{
		fi->filename.name[start] = 0;
	}
	fi->filename.name_len = 0;
	inode->i_ctime = dir->i_ctime;
	mark_inode_dirty(inode);
	/* drop_nlink & mark_inode_dirty */
	inode_dec_link_count(inode);
out:
	return 0;
}

static int ffs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct inode * inode = d_inode(dentry);
	// struct inode * pinode = d_inode(dentry->d_parent);
	int err = -ENOTEMPTY;
	struct flatfs_sb_info *flatfs_sb_i = FFS_SB(dir->i_sb);
	//sector_t meta_start, meta_size, data_start, data_size;
	
	if(!i_size_read(inode)){
		err = ffs_unlink(dir, dentry);
		if(!err){
			inode->i_size = 0;
			inode_dec_link_count(inode);
			inode_dec_link_count(dir);
			//TUDO free dentry in memory
			//printk("ffs_rmdir, dir_id = %d, inode_id = %d, dir name = %s\n", inode_to_dir_id(dir_ino), inode_to_dir_id(inode->i_ino), dentry->d_name.name);
			remove_dir(flatfs_sb_i, dir->i_ino, inode->i_ino);
		}
		return 0;
	}
	return err;
}



static int ffs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
	int err;
	err = ffs_mknod(dir, dentry, mode | S_IFREG, 0);
	if(err == -1)
	{
		printk("Create failed, hash crash!");
	}
	return err;
}


struct inode_operations ffs_file_inode_ops = {
    .setattr	= simple_setattr,
	.getattr	= simple_getattr,
};

struct inode_operations ffs_dir_inode_ops = {
	.create         = ffs_create,
	.lookup         = ffs_lookup,
	.link			= simple_link,
	.unlink         = ffs_unlink,
	//.symlink		= flatfs_symlik,
	.mkdir          = ffs_mkdir,
	.rmdir          = ffs_rmdir,
	.mknod          = ffs_mknod,	//该函数由系统调用mknod（）调用，创建特殊文件（设备文件、命名管道或套接字）。要创建的文件放在dir目录中，其目录项为dentry，关联的设备为rdev，初始权限由mode指定。
	.rename         = simple_rename,
};


