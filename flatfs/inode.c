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
extern unsigned long flatfs_inode_by_name(struct flatfs_sb_info *sb_i, unsigned long parent_ino, struct qstr *child, int* is_dir);
extern unsigned int BKDRHash(char *str);
extern struct ffs_inode_info* FFS_I(struct inode * inode);
extern int shared_slot_id;
extern int shared_is_flatfs;

static int mknod_is_dir;

//当文件未找到时需返回空闲slot id
struct ffs_inode *ffs_find_get_inode_file(struct super_block *sb, lba_t slba, char* name, int* slot_id, struct buffer_head **p)
{
	printk("ffs_find_get_inode_file ");
	struct buffer_head *bhs[BLOCKS_PER_BUCKET];
	int index = -1;
	struct ffs_inode* temp = NULL;
	int i;
	int free_slot_id;
	bool first_time = true;
	slba = slba >> BLOCK_SHIFT;
	printk("ffs_find_get_inode_file sb dev = %d", sb->s_dev);	
	//page cache分配
	for(i = 0; i < BLOCKS_PER_BUCKET ; i ++){
		bhs[i] = sb_bread(sb, slba);
		slba ++;
	}
	printk("sb_getblk ok");
	
	ll_rw_block(REQ_OP_READ, REQ_META | REQ_PRIO, BLOCKS_PER_BUCKET, bhs);
	printk("ll_rw_block ok");
	//等待读完成
	for(i = 0; i < BLOCKS_PER_BUCKET ; i++){
		if(bhs[i])
			wait_on_buffer(bhs[i]);
	}
	printk("wait_on_buffer ok");
	for(i = 0; i < BLOCKS_PER_BUCKET ; i++){
		temp = (struct ffs_inode *)(bhs[i]->b_data);
		if(temp->valid == 0){
			if(first_time){
				free_slot_id = i;
				first_time = false;
			}
			continue;
		}
		if(temp->filename == name){
			index = i;
			*slot_id = i;
			lock_buffer(bhs[i]);
			if(!buffer_uptodate(bhs[i]))
				set_buffer_uptodate(bhs[i]);
			lock_buffer(bhs[i]);
			break;
		}
	}
	printk("buffer_uptodate ok");
	if(i == BLOCKS_PER_BUCKET){
		*p = NULL;
		*slot_id = free_slot_id;
		return NULL;
	}

	*p = bhs[index]; 

	for (i = 0; i < BLOCKS_PER_BUCKET; i++){
		if(i == index )
			continue;
		brelse(bhs[i]);
	}

	return (struct ffs_inode *)(bhs[index]->b_data);
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
	printk(KERN_INFO "flatfs: lookup\n");
	int r, err;
	struct inode *inode;
	unsigned long ino = 0;
	int is_dir = 1;
	loff_t size = 0;// long long
	struct buffer_head *bh = NULL;
	struct ffs_inode *raw_inode = NULL;
	struct flatfs_sb_info *ffs_sb = dir->i_sb->s_fs_info;
	struct page* page; 
	int dir_id;
	int bucket_id;
	int slot_id = 1;

	if (dentry->d_name.len > FFS_MAX_FILENAME_LEN)
		return NULL;

		
	//test inode buffer block
	// struct address_space *mapping = dir->i_mapping;
	// if(mapping)
	// {
	// 	printk("test inode buffer block mapping OK");
	// 	struct page *mapping_page = read_mapping_page(mapping, 0, NULL);
	// 	if(!page)
	// 	{
	// 		printk("test inode buffer block get page failed");
	// 	}
	// 	else
	// 	{
	// 		struct buffer_head *head = page_buffers(page);
	// 		if(head)
	// 		{
	// 			bh = head;
	// 			do {
	// 				if(!bh) break;
	// 				printk("bh->b_blocknr = %lld\n", bh->b_blocknr);
	// 				bh = bh->b_this_page;
	// 			} while (bh != head);
	// 		}
			
	// 	}
	// }
	printk(KERN_INFO "flatfs: flatfs_inode_by_name\n");
	// printk(KERN_INFO "dentry->d_name: %s\n", dentry->d_name);
	ino = flatfs_inode_by_name(dir->i_sb->s_fs_info, dir->i_ino, &dentry->d_name, &is_dir);	//通过查询dir-idx计算出目标目录或文件的ino,如果是目录且存在，则直接获取到ino(dentry); 如果是文件，则返回文件所在目录的ino(dir)
	//调试用：
	printk(KERN_INFO "is_dir: %d\n", is_dir);
	if((!is_dir)&& (ino != dir->i_ino))
	{
		printk(KERN_WARNING "ffs inode number error\n");
		return dentry;
	}
	printk(KERN_INFO "flatfs: flatfs_inode_by_name OK ino = %ld\n", ino);

	dir_id = (ino - 1) >> (MIN_FILE_BUCKET_BITS + FILE_SLOT_BITS);
	printk("dir_id = %x\n", dir_id);
	printk("dir_id = %x\n", flags);
	/* 读盘获取inode */
	if(!is_dir){//文件
		unsigned int hashcode = BKDRHash((char *)(dentry->d_name.name));
		printk("hashcode = %ld\n", hashcode);
		unsigned long bucket_id = (unsigned long)(hashcode & ((1LU << MIN_FILE_BUCKET_BITS) - 1LU));
		printk("bucket_id = %ld\n", bucket_id);
		sector_t bucket_pblk = ffs_get_lba_file_bucket(dir, dentry, dir_id);
		printk("ffs_get_lba_file_bucket OK, bucket_pblk = %lld", bucket_pblk);

		ino = ((dir_id << (MIN_FILE_BUCKET_BITS + FILE_SLOT_BITS)) | (bucket_id << FILE_SLOT_BITS) | slot_id) + 1;
		inode = iget_locked(dir->i_sb, ino);
		if (!inode)
			return ERR_PTR(-ENOMEM);
		if (!(inode->i_state & I_NEW)) {
			printk(KERN_INFO "flatfs: new inode OK\n");
			return d_splice_alias(inode, dentry);
		}
		raw_inode = ffs_find_get_inode_file(dir->i_sb, bucket_pblk, (char *)(dentry->d_name.name), &slot_id, &bh);
		printk(KERN_INFO "flatfs: ffs_find_get_inode_file\n");
		if(shared_is_flatfs)
			shared_slot_id = slot_id;
		if(!raw_inode){//没找到
			inode = NULL;
			goto out;
		}
		printk(KERN_INFO "flatfs: get raw_inode OK\n");
	}/* 结束判断inode存在性 */
	else{//目录
		if(ino == dir->i_ino){//没找到
			inode = NULL;
			printk(KERN_INFO "inode is NULL\n");
			goto out;
		}
		//获取目录inode
		inode = iget_locked(dir->i_sb, ino);
		if (!inode)
			return ERR_PTR(-ENOMEM);
		if (!(inode->i_state & I_NEW))
			goto out;
		lba_t pblk = ffs_get_lba_dir_meta(ino, -1);
		raw_inode = ffs_get_inode_dir(dir->i_sb, pblk , &bh);
	}
	
	printk("Lookup get inode OK");

	struct ffs_inode_info *fi = FFS_I(inode);
	fi->dir_id = dir_id;
	fi->valid = 1;

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
		if(shared_is_flatfs)
			fi->slot_id  = shared_slot_id;
		else
			fi->slot_id  = slot_id;
		inode->i_mode |= S_IFREG ;
		inode->i_op = &ffs_file_inode_ops;
		inode->i_fop = &ffs_file_file_ops;
		set_nlink(inode, 1);//不允许硬链接，常规文件的nlink固定为1
	}
		
	brelse(bh);
	unlock_new_inode(inode);
out:
	return d_splice_alias(inode, dentry);//将inode与dentry绑定
}

//调用具体文件系统的lookup函数找到当前分量的inode，并将inode与传进来的dentry关联（通过d_splice_alias()->__d_add）
//dir:父目录的inode；
//dentry：本目录的dentry，需要关联到本目录的inode
static struct dentry *ffs_lookup2(struct inode *dir, struct dentry *dentry, unsigned int flags, int is_dir, int * slot_id)	
{
	int r, err;
	struct inode *inode;
	unsigned long ino = 0;
	int useless;
	//int is_dir = 0;
	ino = flatfs_inode_by_name(dir->i_sb->s_fs_info,dir->i_ino, &dentry->d_name, &useless);	//通过查询dir-idx计算出目标目录或文件的ino,如果是目录且存在，则直接获取到ino(dentry); 如果是文件，则返回文件所在目录的ino(dir)
	//调试用：
	if((!is_dir)&& (ino != dir->i_ino))
		printk(KERN_WARNING "ffs inode number error\n");

	int dir_id = (ino - 1) >> (MIN_FILE_BUCKET_BITS + FILE_SLOT_BITS);
	loff_t size = 0;// long long
	struct buffer_head *bh;
	struct ffs_inode *raw_inode = NULL;
	struct flatfs_sb_info *ffs_sb = dir->i_sb->s_fs_info;
	struct page* page; 
	int bucket_id;
	
	/* 读盘获取inode */
	if(!is_dir){//文件
		//int slot_id;
		unsigned int hashcode = BKDRHash((char *)(dentry->d_name.name));
		unsigned long bucket_id = (unsigned long)(hashcode & ((1LU << MIN_FILE_BUCKET_BITS) - 1LU));
		sector_t bucket_pblk = ffs_get_lba_file_bucket(dir,dentry,dir_id);
		raw_inode = ffs_find_get_inode_file(dir->i_sb, bucket_pblk, (char *)(dentry->d_name.name), slot_id, &bh);
		if(!raw_inode){//没找到
			inode = NULL;
			goto out;
		}
		else{//根据slotid计算出来文件的ino
			ino = ((dir_id << (MIN_FILE_BUCKET_BITS + FILE_SLOT_BITS)) | (bucket_id << FILE_SLOT_BITS) | *slot_id) + 1;
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
		lba_t pblk = ffs_get_lba_dir_meta(ino,-1);
		raw_inode = ffs_get_inode_dir(dir->i_sb, pblk , &bh);
	}
	
	struct ffs_inode_info *fi = FFS_I(inode);
	fi->dir_id = dir_id;
	fi->valid = 1;

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
		fi->slot_id = *slot_id;
		inode->i_mode |= S_IFREG ;
		inode->i_op = &ffs_file_inode_ops;
		inode->i_fop = &ffs_file_file_ops;
		set_nlink(inode,1);//不允许硬链接，常规文件的nlink固定为1
	}
		
	brelse(bh);
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
	// strcpy(ffs_sb->name, "flatfs");
	//cuckoo_hash_t* ht = ffs_sb->cuckoo;
	loff_t size = 0;
	struct ffs_inode_info * dfi = FFS_I(dir);
	struct ffs_inode_info * fi = FFS_I(inode);
	unsigned long ino = 0;
	int useless;

	// dump_stack();
	// 为新inode分配ino#
	if(mknod_is_dir) {
		// printk(KERN_INFO "mknod d/f name: %s\n", s_dentry->d_name.name);
		char *dir_name = kmalloc(my_strlen((char *)(dentry->d_name.name))+2, GFP_KERNEL);
		memcpy(dir_name, dentry->d_name.name, my_strlen((char *)(dentry->d_name.name)));
		unsigned long dir_id = fill_one_dir_entry(dir->i_sb->s_fs_info, dir_name);
		unsigned long parent_ino = ((dfi->dir_id << (MIN_FILE_BUCKET_BITS + FILE_SLOT_BITS))) + 1;
		printk("mknod dir id: %lu\n", dir_id);
		// unsigned long parent_ino = dir->i_ino;
		insert_dir(dir->i_sb->s_fs_info, dfi->dir_id, dir_id);
		ino = ((dir_id << (MIN_FILE_BUCKET_BITS + FILE_SLOT_BITS))) + 1;
		fi->dir_id = dir_id;
		fi->bucket_id = -1;
		fi->slot_id = -1;
		fi->valid = 1;
	}
	else{
		printk(KERN_INFO "flatfs: create\n");
		int dir_id = dfi->dir_id;
		printk(KERN_INFO "flatfs: dir_id = %d and dir_name = %s\n", dfi->dir_id, (char *)(dentry->d_name.name));
		//TUDO
		unsigned int hashcode;
		hashcode = BKDRHash((char *)(dentry->d_name.name));
		unsigned long bucket_id = (unsigned long)(hashcode & ((1LU << MIN_FILE_BUCKET_BITS) - 1LU));
		ino = ((dir_id << (MIN_FILE_BUCKET_BITS + FILE_SLOT_BITS)) | (bucket_id << FILE_SLOT_BITS) | shared_slot_id) + 1;
		fi->dir_id = dir_id;
		fi->bucket_id = bucket_id;
		fi->slot_id = shared_slot_id;
		fi->valid = 1;
	}
	inode->i_ino = ino;
	printk(KERN_INFO "flatfs: mknod ino=%lu\n",inode->i_ino);
	if (inode) {
		//spin_lock(dir->i_lock);
		//if((mode & S_IFMT)==S_IFDIR)
		//	dget(dentry);   /* 这里额外增加dentry引用计数从而将dentry常驻内存,仅用于调试 */
		insert_inode_locked(inode);//将inode添加到inode hash表中，并标记为I_NEW
		mark_inode_dirty(inode);	//为ffs_inode分配缓冲区，标记缓冲区为脏，并标记inode为脏
		unlock_new_inode(inode);
		d_instantiate(dentry, inode);//将dentry和新创建的inode进行关联
		
		ffs_add_entry(dir);//写父目录
		//调试
		//loff_t dir_size = i_size_read(dir);
		printk(KERN_INFO "flatfs: mknod dir size is = %llu\n",dir->i_size);

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
	printk(KERN_INFO "flatfs mkdir");
	mknod_is_dir = 1;
	int ret = ffs_mknod(dir, dentry, mode | S_IFDIR, 0);
	if (!ret)
		inc_nlink(dir);
	return ret;
}


static int ffs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	//删除磁盘 inode
	struct ffs_inode_info* fi = FFS_I(inode);
	fi->valid =0;
	mark_inode_dirty(inode);

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
	struct flatfs_sb_info *flatfs_sb_i = FFS_SB(dir->i_sb);
	unsigned long dir_ino = dir->i_ino;
	
	if(!i_size_read(inode)){
		err = ffs_unlink(dir, dentry);
		if(!err){
			inode->i_size = 0;
			inode_dec_link_count(inode);
			inode_dec_link_count(dir);
			//TUDO free dentry in memory
			remove_dir(flatfs_sb_i, dir_ino);
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
	mknod_is_dir = 0;
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


