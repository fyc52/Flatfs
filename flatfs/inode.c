#include <linux/module.h>
//#include <stdlib.h>//内核模块不能使用
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/blk_types.h>
#include <linux/namei.h>
#include <linux/delay.h> 
#ifndef _TEST_H_
#define _TEST_H_
#include "flatfs_d.h"
#endif

extern struct dentry_operations ffs_dentry_ops;
extern struct dentry_operations ffs_ci_dentry_ops;
extern struct inode *flatfs_new_inode(struct super_block *sb, int mode, dev_t dev);
struct inode *flatfs_iget(struct super_block *sb, int mode, dev_t dev, int is_root);
extern struct inode_operations ffs_dir_inode_ops;
extern struct inode_operations ffs_file_inode_ops;
extern struct file_operations ffs_file_file_ops;
extern struct address_space_operations ffs_aops;
extern struct file_operations ffs_dir_operations;
extern void mark_buffer_dirty(struct buffer_head *bh);
extern void unlock_buffer(struct buffer_head *bh);
extern void lock_buffer(struct buffer_head *bh);
extern void brelse(struct buffer_head *bh);
extern void set_buffer_uptodate(struct buffer_head *bh);
extern void ll_rw_block(int, int, int, struct buffer_head * bh[]);
extern void wait_on_buffer(struct buffer_head *bh);
extern struct buffer_head * sb_bread_unmovable(struct super_block *sb, sector_t block);
extern unsigned int BKDRHash(char *str);
extern struct hashfs_inode_info* HASHFS_I(struct inode * inode);
extern int blkdev_issue_discard(struct block_device *bdev, sector_t sector,
		sector_t nr_sects, gfp_t gfp_mask, unsigned long flags);
extern int dquot_initialize(struct inode *inode);


static struct ffs_inode *hashfs_get_inode(struct super_block *sb, ino_t ino,
					struct buffer_head **p)
{
	struct buffer_head * bh;
	unsigned long block;

	*p = NULL;
	if ((ino <= FLATFS_ROOT_INO) || ino > MAX_INODE_NUM)
		goto Einval;

	block = hashfs_get_data_lba(sb, ino, 0);

	if (!block || !(bh = sb_bread(sb, block)))
		goto Eio;

	*p = bh;
	return (struct ffs_inode *) (bh->b_data);

Einval:
	return ERR_PTR(-EINVAL);
Eio:
	return ERR_PTR(-EIO);
}

struct inode *hashfs_iget (struct super_block *sb, struct inode * dir, unsigned long ino)
{
	struct hashfs_inode_info *ei;
	struct buffer_head * bh = NULL;
	struct ffs_inode *raw_inode;
	struct inode *inode;
	long ret = -EIO;
	int n;

	inode = iget_locked(sb, ino);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;

	ei = HASHFS_I(inode);
	
	raw_inode = hashfs_get_inode(inode->i_sb, ino, &bh);
	if (IS_ERR(raw_inode)) {
		ret = PTR_ERR(raw_inode);
 		goto bad_inode;
	}

	// 用盘内inode赋值inode操作
	inode->i_size = raw_inode->i_size;	
	inode->i_blocks = raw_inode->i_blocks;										
	inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
	// inode->i_uid  = (uid_t) le16_to_cpu(raw_inode->i_uid);
	// inode->i_gid  = (gid_t) le16_to_cpu(raw_inode->i_gid);
	inode->i_rdev = sb->s_dev;
	inode->i_mode |= S_IFREG;
	set_nlink(inode, 1);            //不允许硬链接，常规文件的nlink固定为1

	inode->i_mapping->a_ops = &ffs_aops;

	if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &ffs_file_inode_ops;
		inode->i_fop = &ffs_file_file_ops;
	} 
	else {
		inode->i_op = &ffs_dir_inode_ops;
		inode->i_fop = &ffs_dir_operations;
	}
		
	brelse (bh);
	unlock_new_inode(inode);
	return inode;
	
bad_inode:
	brelse(bh);
	iget_failed(inode);
	return ERR_PTR(ret);
}

static struct dentry *
ffs_lookup(struct inode * dir, struct dentry *dentry, unsigned int flags)
{
	struct inode * inode;
	ino_t ino;
	
	if (dentry->d_name.len > FFS_MAX_FILENAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	ino = hashfs_inode_by_name(dir, &dentry->d_name);
	inode = NULL;
	if (ino) {
		inode = hashfs_iget(dir->i_sb, dir, ino);
		if (inode == ERR_PTR(-ESTALE)) {
			return ERR_PTR(-EIO);
		}
	}
	return d_splice_alias(inode, dentry);
}


static int
ffs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev)
{
	struct inode * inode;
	int err;

	//err = dquot_initialize(dir);
	//if (err)
		//return err;
	inode = flatfs_new_inode (dir->i_sb, mode, dev);
	err = PTR_ERR(inode);
	if (!IS_ERR(inode)) {
		//init_special_inode(inode, inode->i_mode, dev);
		//printk("mknod before mark inode dirty\n");
		insert_inode_locked(inode);
		mark_inode_dirty(inode);
		err = hashfs_add_nondir(dentry, inode);
	}
	
	//if(inode) unlock_new_inode(inode);

	//fyc hash test break point
	if(!strcmp("fyc", dentry->d_name.name))
		print_hash_info();
	return err;
}


static int ffs_mkdir(struct inode * dir, struct dentry * dentry, umode_t mode)
{
	struct inode * inode;
	int err;

	err = dquot_initialize(dir);
	if (err)
		return err;

	inode_inc_link_count(dir);

	inode = flatfs_new_inode(dir->i_sb, S_IFDIR | mode, 0);
	err = PTR_ERR(inode);
	if (IS_ERR(inode))
		goto out_dir;

	inode_inc_link_count(inode);

	err = hashfs_make_empty(inode, dir);
	if (err)
		goto out_fail;

	/* 将新建目录加入到父目录的目录项文件中 */
	err = hashfs_add_link(dentry, inode);
	if (err)
		goto out_fail;
		
	insert_inode_locked(inode);
	/* 对目录项建立dentry快速缓存 */
	d_instantiate_new(dentry, inode);
	//if(inode) unlock_new_inode(inode);
out:
	return err;

out_fail:
	inode_dec_link_count(inode);
	inode_dec_link_count(inode);
	discard_new_inode(inode);
out_dir:
	inode_dec_link_count(dir);
	goto out;
}


static int ffs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode * inode = d_inode(dentry);
	struct hashfs_dir_entry_2 * de;
	struct page * page;
	int err;

	err = dquot_initialize(dir);
	if (err)
		goto out;

	de = hashfs_find_entry (dir, &dentry->d_name, &page);
	if (!de) {
		err = -ENOENT;
		goto out;
	}

	/* 处理目录项文件 */
	err = hashfs_delete_entry (de, page);
	if (err)
		goto out;
	free_ino(FFS_SB(inode->i_sb), inode->i_ino);
	hashfs_remove_inode(inode);
	inode->i_ctime = dir->i_ctime;
	inode_dec_link_count(inode);
	err = 0;
out:
	//printk("unlink:remove_inode OK\n");
	return err;
}

static int ffs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct inode * inode = d_inode(dentry);
	int err = -ENOTEMPTY;

	if (hashfs_empty_dir(inode)) {
		err = ffs_unlink(dir, dentry);
		if (!err) {
			inode->i_size = 0;
			inode_dec_link_count(inode);
			inode_dec_link_count(dir);
		}
	}
	return err;
}



static int ffs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
	//printk(KERN_INFO "flatfs create\n");
	// printk(KERN_ALERT "--------------[create] dump_stack start----------------");
	// dump_stack();
	// printk(KERN_ALERT "--------------[create] dump_stack end----------------");
	int err = 0;
	//printk("dentry->name:%s\n", dentry->d_name.name);
	err = ffs_mknod(dir, dentry, mode | S_IFREG, 0);
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


