#include <linux/module.h>
//#include <stdlib.h>//内核模块不能使用
#include <linux/fs.h>
#include "flatfs_d.h"
#include "lba.h"

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


struct ffs_inode *ffs_find_get_inode_file(struct super_block *sb, lba_t slba, char* name, struct buffer_head **p)
{
	struct buffer_head * bh;
	bh = sb_bread(sb, (slba >> BLOCK_SHIFT));
	addr = 
	 == name
	
}

struct ffs_inode *ffs_get_inode_dir(struct super_block *sb, lba_t slba, char* name, struct buffer_head **p)
{
	struct buffer_head * bh = NULL;
	bh = sb_bread(sb, (slba >> BLOCK_SHIFT));
	if(!bh)
		return NULL;
	*p = bh;
	return (struct ffs_inode_info *)bh->data;
}

//调用具体文件系统的lookup函数找到当前分量的inode，并将inode与传进来的dentry关联（通过d_splice_alias()->__d_add）
//dir:父目录的inode；
//dentry：本目录的dentry，需要关联到本目录的inode
static struct dentry *ffs_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)	
{
	int r, err;
	struct inode *inode;
	unsigned long ino = 0;
	int ino = flatfs_inode_by_name(dir, dentry, &is_dir);	//通过查询dir-idx计算出目标目录或文件的ino,如果是目录且存在，则直接获取到ino; 如果是文件，则返回文件所在目录的ino
	int dir_id = (ino - 1) >> (MIN_FILE_BUCKET_BITS + FILE_SLOT_BITS);
	loff_t size = 0;// long long
	struct buffer_head *bh;
	struct ffs_inode *raw_inode = NULL;
	struct flatfs_sb_info *ffs_sb = dir->i_sb->s_fs_info;
	struct page* page; 
	int bucket_id;
	struct ffs_inode_info *dir_i = FFS_I(DIR);
	
	/* 读盘获取inode */
	if(!is_dir){//文件
		int slotid;
		unsigned int hashcode = BKDRHash(dentry->d_name.name);
		unsigned long bucket_id = (unsigned long)(hashcode & ((1LU << MIN_FILE_BUCKET_BITS) - 1LU));
		dir_id = ; //获取
		sector_t bucket_pblk = ffs_get_lba_file_bucket(dir,dentry,dir_id);
		raw_inode = ffs_find_get_inode_file(dir->i_sb, bucket_pblk, dentry->d_name.name, &slotid, &bh);
		if(!raw_inode){//没找到
			inode = NULL;
			goto out;
		}
		else{//根据slotid计算出来文件的ino
			ino = (dir_id << (MIN_FILE_BUCKET_BITS + FILE_SLOT_BITS)) | (bucket_id << FILE_SLOT_BITS) | slot_id;
			inode = iget_locked(dir->i_sb, ino);
		}
	}/* 结束判断inode存在性 */
	else{//目录
		if(ino == 0){//没找到
			inode = NULL;
			goto out;
		}
		//获取目录inode
		inode = iget_locked(dir->i_sb, ino);
		pblk = ffs_get_lba_dir_meta(ino,-1);
		raw_inode = ffs_get_inode_dir(dir->i_sb, pblk ,&bh);
	}
	
	struct ffs_inode_info *fi = FFS_I(inode);
	fi->dir_id = dir_id;

	printk(KERN_INFO "flatfs lookup found, ino: %lu, size: %llu\n", ino, raw_inode->size);//调试
	/*从挂载的文件系统里寻找inode,仅用于处理内存icache*/
	
	// 用盘内inode赋值inode操作
	inode->i_size = raw_inode->size;											
	inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
	inode->i_uid = dir->i_uid;
	inode->i_gid = dir->i_gid;
	inode->i_rdev=dir->i_sb->s_dev;
	inode->i_mapping->a_ops = &ffs_aops;
	if(is_dir){
		fi->bucket_id = -1;
		fi->slot_id	  = -1;
		inode->i_mode |= S_IFDIR;	
		inode->i_op = &ffs_dir_inode_ops;
		inode->i_fop = &ffs_dir_operations;
		set_nlink(inode,2);
	}
	else{
		fi->bucket_id = bucket_id;
		fi->slot_id   = slot_id;
		inode->i_mode |= S_IFREG ;
		inode->i_op = &ffs_file_inode_ops;
		inode->i_fop = &ffs_file_file_ops;
		set_nlink(inode,1);//不允许硬链接，常规文件的nlink固定为1
	}
		
	brelse(bh);//对应bread
	unlock_new_inode(inode);
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
	loff_t size=0;

	inode->i_ino = flatfs_inode_by_name(dir,dentry);//为新inode分配ino#
	printk(KERN_INFO "flatfs: mknod ino=%lu\n",inode->i_ino);
	if (inode) {
		//spin_lock(dir->i_lock);
		//if((mode & S_IFMT)==S_IFDIR)
			dget(dentry);   /* 这里额外增加dentry引用计数从而将dentry常驻内存,仅用于调试 */

		mark_inode_dirty(inode);	//为ffs_inode分配缓冲区，标记缓冲区为脏，并标记inode为脏
		unlock_new_inode(inode);
		d_instantiate(dentry, inode);//将dentry和新创建的inode进行关联
		
		ffs_add_entry(dir);//写父目录
		loff_t dir_size = i_size_read(dir);
		printk(KERN_INFO "flatfs: mknod dir size is = %llu\n",dir->i_size);

		if(cuckoo_insert(ht, (unsigned char *)&(inode->i_ino), (unsigned char *)&size)==FAIL){
			cuckoo_resize(ht);
			cuckoo_insert(ht, (unsigned char *)&(inode->i_ino), (unsigned char *)&size);
		}
		cuckoo_update(ht, (unsigned char *)&(dir->i_ino), (unsigned char *)&dir_size);
		//调试
		unsigned long long value;
		cuckoo_query(ht, (unsigned char *)&(dir->i_ino), (unsigned char *)&value);
		printk(KERN_INFO "flatfs: mknod dir size is = %llu %llu\n", value, dir_size);
		cuckoo_query(ht, (unsigned char *)&(inode->i_ino), (unsigned char *)&value);
		printk(KERN_INFO "flatfs: mknod file size is = %llu %llu\n", value, size);
		
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
	
	return 0;
}

static int ffs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct inode * inode = d_inode(dentry);
	loff_t dir_size;
	int err = -ENOTEMPTY;
	
	if(!i_size_read(inode)){
		err = ffs_unlink(dir,dentry);
		if(!err){
			inode->i_size = 0;
			inode_dec_link_count(inode);
			inode_dec_link_count(dir);
		}
		return 0;
	}
	return err;
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
	.lookup         = ffs_lookup,
	.link			= simple_link,
	.unlink         = ffs_unlink,
	//.symlink		= flatfs_symlik,
	.mkdir          = ffs_mkdir,
	.rmdir          = ffs_rmdir,
	.mknod          = ffs_mknod,	//该函数由系统调用mknod（）调用，创建特殊文件（设备文件、命名管道或套接字）。要创建的文件放在dir目录中，其目录项为dentry，关联的设备为rdev，初始权限由mode指定。
	.rename         = simple_rename,
};


