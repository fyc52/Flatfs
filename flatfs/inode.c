#include <linux/module.h>
//#include <stdlib.h>//内核模块不能使用
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/blk_types.h>
#include <linux/namei.h>
#ifndef _TEST_H_
#define _TEST_H_
#include "flatfs_d.h"
#include "lba.h"
#endif

extern struct dentry_operations ffs_dentry_ops;
extern struct dentry_operations ffs_ci_dentry_ops;
extern struct inode *flatfs_get_inode(struct super_block *sb, int mode, dev_t dev);
struct inode *flatfs_iget(struct super_block *sb, int mode, dev_t dev, int is_root);
extern unsigned long calculate_slba(struct inode* dir, struct dentry* dentry);
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
extern struct ffs_inode_info* FFS_I(struct inode * inode);
extern int blkdev_issue_discard(struct block_device *bdev, sector_t sector,
		sector_t nr_sects, gfp_t gfp_mask, unsigned long flags);


static int test = 1;

//当文件未找到时需返回空闲slot id
struct ffs_inode *ffs_find_get_inode_file(struct super_block *sb, lba_t slba, char* name, int* slot_id, struct buffer_head **p)
{
	//printk("ffs_find_get_inode_file ");
	struct buffer_head *bhs[SLOTS_PER_BUCKET];
	int index = -1;
	struct ffs_inode* temp = NULL;
	int i;
	int free_slot_id;
	bool first_time = true;
	slba = slba >> BLOCK_SHIFT;
	//printk("ffs_find_get_inode_file sb dev = %d", sb->s_dev);	
	//page cache分配
	for(i = 0; i < SLOTS_PER_BUCKET ; i ++){
		bhs[i] = sb_bread(sb, slba);
		slba ++;
	}
	//("sb_getblk ok");
	
	ll_rw_block(REQ_OP_READ, REQ_META | REQ_PRIO, SLOTS_PER_BUCKET, bhs);
	//printk("ll_rw_block ok");
	//等待读完成
	for(i = 0; i < SLOTS_PER_BUCKET ; i++){
		if(bhs[i])
			wait_on_buffer(bhs[i]);
	}
	//printk("wait_on_buffer ok");
	for(i = 0; i < SLOTS_PER_BUCKET ; i++){
		temp = (struct ffs_inode *)(bhs[i]->b_data);
		if(temp->valid == 0){
			if(first_time){
				free_slot_id = i;
				first_time = false;
			}
			continue;
		}
		if(temp->filename.name_len == strlen(name) && !strcmp(temp->filename.name, name)) {
			index = i;
			*slot_id = i;
			lock_buffer(bhs[i]);
			if(!buffer_uptodate(bhs[i]))
				set_buffer_uptodate(bhs[i]);
			lock_buffer(bhs[i]);
			break;
		}
	}
	//printk("buffer_uptodate ok");
	if(i == SLOTS_PER_BUCKET){
		*p = NULL;
		*slot_id = free_slot_id;
		return NULL;
	}

	*p = bhs[index]; 

	temp =  (struct ffs_inode *)(bhs[index]->b_data);

	for (i = 0; i < SLOTS_PER_BUCKET; i++){
		if(i == index )
			continue;
		if(bhs[i]) brelse(bhs[i]);
	}
	return temp;
}

// struct ffs_inode *ffs_get_inode_dir(struct super_block *sb, lba_t slba, char* name, struct buffer_head **p)
struct ffs_inode *ffs_get_inode_dir(struct super_block *sb, lba_t slba, struct buffer_head **p)
{
	struct buffer_head * bh = NULL;
	bh = sb_bread(sb, (slba >> BLOCK_SHIFT));
	if(!bh)
		return NULL;
	*p = bh;
	return (struct ffs_inode *)bh->b_data;
}

//调用具体文件系统的lookup函数找到当前分量的inode，并将inode与传进来的dentry关联（通过d_splice_alias()->__d_add）
//dir:父目录的inode；
//dentry：本目录的dentry，需要关联到本目录的inode
static struct dentry *ffs_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)	
{
	//printk(KERN_INFO "flatfs: lookup, name = %s\n", dentry->d_name.name);
	int r, err;
	struct inode *inode;
	unsigned long ino = 0;
	loff_t size = 0;// long long
	struct buffer_head *bh = NULL;
	struct ffs_inode *raw_inode = NULL;
	struct flatfs_sb_info *ffs_sb = dir->i_sb->s_fs_info;
	// struct page* page; 
	int dir_id;
	int bucket_id;
	int slot_id = 0;
	struct ffs_inode_info * dfi = FFS_I(dir);

	if (dentry->d_name.len > FFS_MAX_FILENAME_LEN)
		return NULL;
	
	//printk(KERN_INFO "flatfs: flatfs_dir_inode_by_name\n");
	ino = flatfs_dir_inode_by_name(dir->i_sb->s_fs_info, dir->i_ino, &dentry->d_name);
	//printk(KERN_INFO "flatfs: flatfsdir_inode_by_name OK ino = %lx\n", ino);

	dir_id = (ino - 1) >> (MIN_FILE_BUCKET_BITS + FILE_SLOT_BITS);
	//printk("dir_id = %x\n", inode_to_dir_id(dir->i_ino));
	//printk("flags = %x\n", flags);

	/* 子目录树没有找到，前往hashtbl查询子文件 */
	if(ino == 0) {
		ino = flatfs_file_inode_by_name(ffs_sb->hashtbl[inode_to_dir_id(dir->i_ino)], dir, dir_id, &dentry->d_name, raw_inode, bh);
		if(ino == 0 && dfi->is_big_dir) ino = flatfs_big_file_inode_by_name(ffs_sb->big_dir_hashtbl[dfi->big_dir_id], dir, dir_id, &dentry->d_name, raw_inode, bh);
	}
		
	/* 子目录树和子文件中均没找到，说明没有这个子文件/目录 */
	if(ino == 0) { 
		inode = NULL;
		//printk(KERN_INFO "inode is NULL\n");
		goto out2;
	}
	//printk(KERN_INFO "flatfs: get ino = %lx\n", ino);
	/* 有子文件/目录 */
	inode = iget_locked(dir->i_sb, ino);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW)) {
		/* 在内存中有最新的inode，直接结束 */
		//printk(KERN_INFO "flatfs: new inode OK\n");
		goto out1;
	}

	/* 
	 * 有子文件/目录，但是在内存中没有最新的inode，需要读盘 
	 * 一般这个函数开机的时候才会调用，按照我们的设计，这个时候只可能是文件，
	 * 因为目录树存在内存了
	*/
	//raw_inode = ffs_find_get_inode_file(dir->i_sb, 0, (char *)(dentry->d_name.name), &slot_id, &bh);
	// printk(KERN_INFO "flatfs: ffs_find_get_inode_file\n");
	if(unlikely(!raw_inode)) {
		goto out1;
	}
	//printk(KERN_INFO "flatfs: get raw_inode OK\n");

	struct ffs_inode_info *fi = FFS_I(inode);
	fi->dir_id = dir_id;
	fi->valid = 1;
	
	// 用盘内inode赋值inode操作
	inode->i_size = raw_inode->size;											
	inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
	inode->i_uid  = dir->i_uid;
	inode->i_gid  = dir->i_gid;
	inode->i_rdev = dir->i_sb->s_dev;
	inode->i_mapping->a_ops = &ffs_aops;
	// if(is_dir) {
	// 	fi->bucket_id = -1;
	// 	fi->slot_id	  = -1;
	// 	inode->i_mode |= S_IFDIR;	
	// 	inode->i_op = &ffs_dir_inode_ops;
	// 	inode->i_fop = &ffs_dir_operations;
	// 	set_nlink(inode, 2);
	// }
	fi->bucket_id = bucket_id;
	fi->slot_id  = slot_id;
	inode->i_mode |= S_IFREG ;
	inode->i_op = &ffs_file_inode_ops;
	inode->i_fop = &ffs_file_file_ops;
	set_nlink(inode, 1);            //不允许硬链接，常规文件的nlink固定为1

out1:
	if(inode) unlock_new_inode(inode);
out2:
	if(bh) brelse(bh);
	return d_splice_alias(inode, dentry);//将inode与dentry绑定
}


static int
ffs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev)
{
	struct inode * inode = flatfs_get_inode(dir->i_sb, mode, dev);//分配VFS inode
	int error = -ENOSPC;
	struct flatfs_sb_info *ffs_sb = dir->i_sb->s_fs_info; 
	// strcpy(ffs_sb->name, "flatfs");
	//cuckoo_hash_t* ht = ffs_sb->cuckoo;
	loff_t size = 0;
	struct ffs_inode_info * dfi = FFS_I(dir);
	struct ffs_inode_info * fi = FFS_I(inode);
	unsigned long ino = 0;
	struct bucket *bucket;
	int mknod_is_dir = mode & S_IFDIR;
	// dump_stack();
	// 为新inode分配ino#
re_mknod:
	if(mknod_is_dir) {
		// printk(KERN_INFO "mknod d/f name: %s\n", s_dentry->d_name.name);
		// char *dir_name = kmalloc(my_strlen((char *)(dentry->d_name.name)) + 2, GFP_KERNEL);
		// memcpy(dir_name, dentry->d_name.name, my_strlen((char *)(dentry->d_name.name)));
		unsigned long dir_id = fill_one_dir_entry(dir->i_sb->s_fs_info, dentry->d_name.name);
		unsigned long parent_ino = ((dfi->dir_id << (MIN_FILE_BUCKET_BITS + FILE_SLOT_BITS))) + 1;
		//printk("mknod dir id: %lu\n", dir_id);
		// unsigned long parent_ino = dir->i_ino;
		insert_dir(dir->i_sb->s_fs_info, dfi->dir_id, dir_id);
		init_file_ht(&(ffs_sb->hashtbl[dir_id]));
		ino = ((dir_id << (MIN_FILE_BUCKET_BITS + FILE_SLOT_BITS))) + 1;
		fi->dir_id = dir_id;
		fi->bucket_id = -1;
		fi->slot_id = -1;
		fi->valid = 1;
		fi->is_big_dir = 0;
		fi->big_dir_id = -1;
		fi->filename.name_len = my_strlen((char *)(dentry->d_name.name));
		memcpy(fi->filename.name, dentry->d_name.name, fi->filename.name_len);

		fi->test = 0;
		// struct ffs_inode_info * testi = FFS_I(inode);
		//printk("test inode info valid:%d\n", testi->valid);
	}
	else if(mknod_is_dir == 0 && dfi->is_big_dir == 0){
		//printk(KERN_INFO "flatfs: create\n");
		int dir_id = dfi->dir_id;
		//printk(KERN_INFO "flatfs: pdir_id = %d and file_name = %s\n", dfi->dir_id, (char *)(dentry->d_name.name));
		ino = flatfs_file_slot_alloc_by_name(ffs_sb->hashtbl[dir_id], dir, dir_id, &dentry->d_name);
		// if(!strcmp((char *)(dentry->d_name.name), "fycnb"))
		// {
		// 	print2log(ffs_sb->hashtbl[dir_id]);
		// }
		if(ino == -1) 
		{
			printk("mknod, hash crash\n");
			error = resize_dir(ffs_sb, dir_id);
			if(error == -1) {
				return -1;
			}
			dfi->is_big_dir = 1;
			dfi->big_dir_id = error;
			goto re_mknod;
		}
		// dfi->test --;
		//printk("mknod, ino:%lx\n", ino);
		fi->dir_id = dir_id;
		fi->bucket_id = ino_to_bucket(ino); 
		fi->slot_id = ino_to_slot(ino);
		fi->valid = 1;
		fi->is_big_dir = 0;
		fi->big_dir_id = -1;
		fi->size = 0;
		fi->filename.name_len = my_strlen((char *)(dentry->d_name.name));
		memcpy(fi->filename.name, dentry->d_name.name, fi->filename.name_len);
		bucket = &(ffs_sb->hashtbl[dir_id]->buckets[fi->bucket_id]);
		//printk("mknod --- ino:%lx, dir_id:%x, bucket_id:%x, slot_id:%x\n", ino, fi->dir_id, fi->bucket_id, fi->slot_id);
	}//大目录
	else{
		int dir_id = dfi->dir_id;
		//printk(KERN_INFO "Big dir, flatfs: pdir_id = %d and file_name = %s\n", dfi->dir_id, (char *)(dentry->d_name.name));

		ino = flatfs_big_file_slot_alloc_by_name(ffs_sb->big_dir_hashtbl[dfi->big_dir_id], dir, dir_id, &dentry->d_name);
		if(ino == -1)
		{
			printk("Unlikelt happen, big dir hash crash\n");
			return -1;
		}
		fi->dir_id = dir_id;
		fi->bucket_id = big_ino_to_bucket(ino); 
		fi->slot_id = big_ino_to_slot(ino);
		fi->valid = 1;
		fi->size = 0;
		fi->is_big_dir = 1;
		fi->big_dir_id = dfi->big_dir_id;
		fi->filename.name_len = my_strlen((char *)(dentry->d_name.name));
		memcpy(fi->filename.name, dentry->d_name.name, fi->filename.name_len);
		bucket = &(ffs_sb->big_dir_hashtbl[dfi->big_dir_id]->buckets[fi->bucket_id]);
		//printk("big dir, create bucket id:%d, slot id:%d ,ino:%ld, filename:%s\n", fi->bucket_id, fi->slot_id, ino, dentry->d_name.name);
	}
	inode->i_ino = ino;
	//printk(KERN_INFO "flatfs: mknod ino=%lu\n",inode->i_ino);
	if (inode) {
		//spin_lock(dir->i_lock);
		//if((mode & S_IFMT)==S_IFDIR)
		//	dget(dentry);   /* 这里额外增加dentry引用计数从而将dentry常驻内存,仅用于调试 */
		insert_inode_locked(inode);//将inode添加到inode hash表中，并标记为I_NEW
		mark_inode_dirty(inode);	//为ffs_inode分配缓冲区，标记缓冲区为脏，并标记inode为脏
		if(inode) unlock_new_inode(inode);
		if(mknod_is_dir == 0) spin_unlock(&(bucket->bkt_lock));
		d_instantiate(dentry, inode);//将dentry和新创建的inode进行关联
		
		// ffs_add_entry(dir);//写父目录
		//调试
		//loff_t dir_size = i_size_read(dir);
		//printk(KERN_INFO "flatfs: mknod dir size is = %llu\n",dir->i_size);

		// if(cuckoo_insert(ht, (unsigned char *)&(inode->i_ino), (unsigned char *)&size)==FAIL){
		// 	cuckoo_resize(ht);
		// 	cuckoo_insert(ht, (unsigned char *)&(inode->i_ino), (unsigned char *)&size);
		// }
		// cuckoo_update(ht, (unsigned char *)&(dir->i_ino), (unsigned char *)&dir_size);
		//调试
		// unsigned long long value;
		// cuckoo_query(ht, (unsigned char *)&(dir->i_ino), (unsigned char *)&value);
		// printk(KERN_INFO "flatfs: mknod dir size is = %llu %llu\n", value, dir_size);
		// cuckoo_query(ht, (unsigned char *)&(inode->i_ino), (unsigned char *)&value);
		// printk(KERN_INFO "flatfs: mknod file size is = %llu %llu\n", value, size);
		
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
	//printk(KERN_INFO "flatfs mkdir");
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
	int dir_id = fi->dir_id;
	int err;
	int start;
	struct bucket *bucket;

	//printk("ffs_unlink: dir_id is %d, filename is %s\n", dir_id, dentry->d_name.name);
	/*delete file in hashtbl*/
	if(fi->is_big_dir == 0) {
		err = delete_file(ffs_sb->hashtbl[dir_id], fi->bucket_id, fi->slot_id);
		bucket = &(ffs_sb->hashtbl[dir_id]->buckets[fi->bucket_id]);
	}
	else{
		err = delete_big_file(ffs_sb->big_dir_hashtbl[fi->big_dir_id], fi->bucket_id, fi->slot_id);
		bucket = &(ffs_sb->big_dir_hashtbl[fi->big_dir_id]->buckets[fi->bucket_id]);
		//printk("delete_big_file, big_dir_id:%d, bucket_id:%d, slot_id:%d\n", fi->big_dir_id, fi->bucket_id, fi->slot_id);
	}
	if(!err)
	{
		printk("unlink failed, filename is %s", dentry->d_name.name);
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
	spin_unlock(&(bucket->bkt_lock));

	/* drop_nlink & mark_inode_dirty */
	inode_dec_link_count(inode);
	// loff_t dir_size = i_size_read(dir);
	// i_size_write(dir, dir_size - 1);
	// mark_inode_dirty(dir);
	
	return 0;
}

static int ffs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct inode * inode = d_inode(dentry);
	// struct inode * pinode = d_inode(dentry->d_parent);
	loff_t dir_size;
	int err = -ENOTEMPTY;
	struct flatfs_sb_info *flatfs_sb_i = FFS_SB(dir->i_sb);
	struct ffs_inode_info * dfi = FFS_I(dir);
	struct ffs_inode_info * fi = FFS_I(inode);
	unsigned long dir_ino = dir->i_ino;
	//sector_t meta_start, meta_size, data_start, data_size;
	
	if(!i_size_read(inode)){
		err = ffs_unlink(dir, dentry);
		if(!err){
			//trim:
			//meta_start = (ffs_get_lba_dir_meta(fi->bucket_id, fi->dir_id) << 3);
			//meta_size = 8;//4KB
			//blkdev_issue_discard(dir->i_sb->s_bdev, meta_start, meta_size, GFP_NOFS, 0);

			inode->i_size = 0;
			inode_dec_link_count(inode);
			inode_dec_link_count(dir);
			//TUDO free dentry in memory
			//printk("ffs_rmdir, dir_id = %d, inode_id = %d, dir name = %s\n", inode_to_dir_id(dir_ino), inode_to_dir_id(inode->i_ino), dentry->d_name.name);
			remove_dir(flatfs_sb_i, dir->i_ino, inode->i_ino, fi->big_dir_id);
		}
		return 0;
	}
	return err;
}



static int ffs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
	//printk(KERN_INFO "flatfs create");
	// printk(KERN_ALERT "--------------[create] dump_stack start----------------");
	// dump_stack();
	// printk(KERN_ALERT "--------------[create] dump_stack end----------------");
	int err;
	err = ffs_mknod(dir, dentry, mode | S_IFREG, 0);
	if(err == -1)
	{
		printk("Create failed, hash crash!");
	}
	return err;
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


