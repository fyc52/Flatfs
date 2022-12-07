
#include <linux/types.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/version.h>
#include <linux/nls.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/backing-dev.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/parser.h>
#include <linux/magic.h>
#include <linux/uaccess.h>
#include <linux/namei.h>
#include <linux/buffer_head.h>
// #include <linux/list_sort.h>
// #include <linux/writeback.h>
// #include <linux/path.h>
// #include <linux/kallsyms.h>
// #include <linux/list.h>
// #include <linux/scatterlist.h>

// #include <linux/iversion.h>
//#include <cstdlib>
//#include <iostream>

#ifndef _TEST_H_
#define _TEST_H_
#include "flatfs_d.h"
#endif
static struct kmem_cache * ffs_inode_cachep;

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
// extern struct buffer_head *sb_getblk(struct super_block *sb, sector_t block);
extern struct ffs_inode_info* FFS_I(struct inode * inode);
extern struct dentry *d_make_root(struct inode *root_inode);


static int flatfs_super_statfs(struct dentry *d, struct kstatfs *buf)
{
	return 0;
}

static void flatfs_put_super(struct super_block *sb)
{
	struct flatfs_sb_info *ffs_sb;
	//printk(KERN_INFO "put super of flatfs\n");

	ffs_sb = FFS_SB(sb);
	if (ffs_sb == NULL)
	{
		/* Empty superblock info passed to unmount */
		return;
	}

	/* FS-FILLIN your fs specific umount logic here */
	//kfree(ffs_sb->cuckoo);
	//ffs_sb->cuckoo=NULL;
	kfree(ffs_sb);
	return;
}

static void ffs_dirty_inode(struct inode *inode, int flags)
{
	//printk("ffs_dirty_inode\n");
	struct buffer_head *ibh = NULL;
	struct super_block *sb = inode->i_sb;
	struct ffs_inode* raw_inode;
	sector_t pblk;

	struct ffs_inode_info *fi = FFS_I(inode);
	struct block_device *bdev = sb->s_bdev;
	struct ffs_inode_page *raw_inode_page;

	pblk = hashfs_get_data_lba(sb, inode->i_ino, 0);
	//printk("ffs_dirty_inode, pblk = %lld, filename = %s\n", pblk, fi->filename.name);
	ibh = sb_bread(sb, pblk);
	// wait_on_buffer(ibh);
 	if (unlikely(!ibh)){
		printk(KERN_ERR "allocate bh for ffs_inode fail");
		return -ENOMEM;
	}	
	lock_buffer(ibh);

	//actual write inode in buffer cache
	//fill bh
	if(fi->filename.name_len > FFS_MAX_FILENAME_LEN) {
		printk("file name len error\n");
		goto out;
	}
	raw_inode = (struct ffs_inode *) ibh->b_data;//b_data就是地址，我们的inode位于bh内部offset为0的地方
	raw_inode->i_size = inode->i_size;
	raw_inode->i_blocks = inode->i_blocks;
	raw_inode->filename.name_len = fi->filename.name_len;
	memcpy(raw_inode->filename.name, fi->filename.name, fi->filename.name_len);

out:
	if (!buffer_uptodate(ibh))
   		set_buffer_uptodate(ibh);  // 表示可以回写
	unlock_buffer(ibh);

	mark_buffer_dirty(ibh);        // 触发回写
	if(ibh) brelse(ibh);           // put_bh, 对应getblk
}

static struct inode *ffs_alloc_inode(struct super_block *sb)
{
	struct ffs_inode_info *fi;
	fi = kmem_cache_alloc(ffs_inode_cachep, GFP_KERNEL);
	if (!fi)
		return NULL;
	atomic64_set(&fi->vfs_inode.i_version, 1);
	// fi->vfs_inode.i_version = 1;

	return &fi->vfs_inode;
}

struct super_operations flatfs_super_ops = {
	.statfs = flatfs_super_statfs,
	.drop_inode = generic_delete_inode, /* VFS提供的通用函数，会判断是否定义具体文件系统的超级块操作函数delete_inode，若定义的就调用具体的inode删除函数(如ext3_delete_inode )，否则调用truncate_inode_pages和clear_inode函数(在具体文件系统的delete_inode函数中也必须调用这两个函数)。 */
	.put_super = flatfs_put_super,
	.dirty_inode = ffs_dirty_inode,
	.alloc_inode = ffs_alloc_inode,
};

struct inode *flatfs_iget(struct super_block *sb, int mode, dev_t dev, int is_root)
{
	struct ffs_inode_info *ei;
	struct buffer_head * bh = NULL;
	// struct ffs_inode *raw_inode;
	struct inode *inode;
	sector_t pblk;
	
	unsigned long root_ino = FLATFS_ROOT_INO;
	inode = iget_locked(sb, root_ino);
	
	if (inode)
	{
		ei = FFS_I(inode);
		
		pblk = hashfs_set_data_lba(inode, 0);
		bh = sb_bread(sb, pblk);
		//printk("iget bh OK!, bh_block = %lld", bh->b_blocknr);
		// raw_inode = (struct ffs_inode *) (bh->b_data);
		
		memcpy(ei->filename.name, "/", strlen("/"));
		ei->filename.name_len = my_strlen("/");

		inode->i_sb = sb;
		inode->i_mode = mode;													//访问权限,https://zhuanlan.zhihu.com/p/78724124
		inode->i_uid = current_fsuid();											/* Low 16 bits of Owner Uid */
		inode->i_gid = current_fsgid();											/* Low 16 bits of Group Id */
		inode->i_size = 0;													//文件的大小（byte）
		inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode); //访问、修改、发生改变的时间
		//printk(KERN_INFO "about to set inode ops\n");
		inode->i_mapping->a_ops = &ffs_aops; // page cache操作
		// inode->i_mapping->backing_dev_info = &ffs_backing_dev_info;
		switch (mode & S_IFMT)
		{ /* type of file ，S_IFMT是文件类型掩码,用来取mode的0--3位,https://blog.csdn.net/wang93IT/article/details/72832775*/
		default:
			init_special_inode(inode, mode, dev); //为字符设备或者块设备文件创建一个Inode（在文件系统层）.
			break;
		case S_IFREG: /* regular 普通文件*/
			//printk(KERN_INFO "file inode\n");
			inode->i_op = &ffs_file_inode_ops;
			inode->i_fop = &ffs_file_file_ops;
			inode->i_mapping->a_ops = &ffs_aops;
			break;
		case S_IFDIR: /* directory 目录文件*/

			inode->i_op = &ffs_dir_inode_ops;
			inode->i_fop = &ffs_dir_operations;
			inode->i_mapping->a_ops = &ffs_aops;
			inc_nlink(inode); // i_nlink是文件硬链接数,目录是由至少2个dentry指向的：./和../，所以是2；这里只加1，外层再加1
			break;
			//     case S_IFLNK://symlink
			// inode->i_op = &page_symlink_inode_operations;
			// inode_nohighmem(inode);
			// break;
		}
	}
	if(bh) (bh);
	return inode;
}

struct inode *flatfs_new_inode(struct super_block *sb, int mode, dev_t dev, char * filename)
{
	struct inode *inode;
	inode = new_inode(sb); // https://blog.csdn.net/weixin_43836778/article/details/90236819
	struct ffs_inode_info * fi;
	
	if (inode)
	{
		inode->i_sb = sb;
		inode->i_mode = mode;													//访问权限,https://zhuanlan.zhihu.com/p/78724124
		inode->i_uid = current_fsuid();											/* Low 16 bits of Owner Uid */
		inode->i_gid = current_fsgid();											/* Low 16 bits of Group Id */
		inode->i_size = 0;													//文件的大小（byte）
		inode->i_blocks = 0;
		inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode); //访问、修改、发生改变的时间
		//printk(KERN_INFO "about to set inode ops\n");
		inode->i_mapping->a_ops = &ffs_aops; // page cache操作
		// inode->i_mapping->backing_dev_info = &ffs_backing_dev_info;
		switch (mode & S_IFMT)
		{ /* type of file ，S_IFMT是文件类型掩码,用来取mode的0--3位,https://blog.csdn.net/wang93IT/article/details/72832775*/
		default:
			init_special_inode(inode, mode, dev); //为字符设备或者块设备文件创建一个Inode（在文件系统层）.
			break;
		case S_IFREG: /* regular 普通文件*/
			//printk(KERN_INFO "file inode\n");
			inode->i_op = &ffs_file_inode_ops;
			inode->i_fop = &ffs_file_file_ops;
			inode->i_mapping->a_ops = &ffs_aops;
			break;
		case S_IFDIR: /* directory 目录文件*/

			inode->i_op = &ffs_dir_inode_ops;
			inode->i_fop = &ffs_dir_operations;
			inode->i_mapping->a_ops = &ffs_aops;
			inc_nlink(inode); // i_nlink是文件硬链接数,目录是由至少2个dentry指向的：./和../，所以是2；这里只加1，外层再加1
			break;
			//     case S_IFLNK://symlink
			// inode->i_op = &page_symlink_inode_operations;
			// inode_nohighmem(inode);
			// break;
		}
		inode->i_ino = get_unused_ino(FFS_SB(inode->i_sb));
		fi = FFS_I(inode);
		strcpy(fi->filename.name, filename);
		fi->filename.name_len = my_strlen(filename);
		//printk("new inode：%s\n", fi->filename.name);
		//inode->i_state &= I_NEW;
		hashfs_set_data_lba(inode, 0);
		mark_inode_dirty(inode);
	}
	
	return inode;
}

int sb_set_blocksize(struct super_block *sb, int size)
{
	if (set_blocksize(sb->s_bdev, size))
		return 0;
	/* If we get here, we know size is power of two
	 * and it's value is between 512 and PAGE_SIZE */
	sb->s_blocksize = size;
	sb->s_blocksize_bits = blksize_bits(size);
	return sb->s_blocksize;
}

static int flatfs_fill_super(struct super_block *sb, void *data, int silent) // mount时被调用，会创建一个sb
{
	struct inode *inode;
	//unsigned long sb_block = get_sb_block(&data);
	int blocksize = BLOCK_SIZE;
	struct flatfs_sb_info *ffs_sb;
	unsigned long logic_sb_block = 1;
	loff_t dir_size;

	struct buffer_head *bh;
	blocksize = sb_min_blocksize(sb, BLOCK_SIZE);
	if (!(bh = sb_bread(sb, logic_sb_block))) {
		printk( KERN_ERR, "error: unable to read superblock");
	}
	else{
		printk("fill super, bh = %lld, sb dev = %d", bh->b_blocknr, sb->s_dev);		/* start block number */
	}
	//es = (struct ext2_super_block *) (((char *)bh->b_data) + offset);
	
	printk(KERN_INFO "flatfs_fill_super 1\n");
	ffs_sb = kzalloc(sizeof(struct flatfs_sb_info), GFP_NOIO);

	//printk(KERN_INFO "flatfs: ffs_sb init ok\n");
	//cuckoo_hash_t *cuckoo = cuckoo_hash_init(BUCKET_NR);
	//printk(KERN_INFO "flatfs: cuckoo init ok\n");
	//ffs_sb->cuckoo = cuckoo;
	//printk(KERN_INFO "flatfs: ffs_sb->cuckoo init ok\n");

	strcpy(ffs_sb->name, sb->s_type->name);

	printk(KERN_INFO "ffs_sb->name: %s\n", ffs_sb->name);
	sb->s_maxbytes = MAX_LFS_FILESIZE;					 /*文件大小上限*/
	sb->s_blocksize = FLATFS_BSTORE_BLOCKSIZE;			 //以字节为单位的块大小
	sb->s_blocksize_bits = FLATFS_BSTORE_BLOCKSIZE_BITS; //以位为单位的块大小
	sb->s_magic = FLATFS_MAGIC;							 //可能是用来内存分配的地址
	sb->s_op = &flatfs_super_ops;						 // sb操作
	sb->s_time_gran = 1;								 /* 时间戳的粒度（单位为纳秒) */
	printk(KERN_INFO "flatfs: fill super\n");

	inode = flatfs_iget(sb, S_IFDIR | 0755, 0, 1); //分配根目录的inode,增加引用计数，对应iput;S_IFDIR表示是一个目录,后面0755是权限位:https://zhuanlan.zhihu.com/p/48529974
	if (!inode)
		return -ENOMEM;

	printk(KERN_INFO "flatfs: flatfs_get_inode OK\n");
	inode->i_ino = FLATFS_ROOT_INO;//为根inode分配ino#，不能为0
	printk(KERN_INFO "flatfs: root inode = %lx\n", inode->i_ino);

	dir_size = i_size_read(inode);
	//cuckoo_insert(cuckoo, (unsigned char *)&(inode->i_ino), (unsigned char *)&dir_size);

	/* 创建hash表 */

	sb->s_fs_info = ffs_sb;
	//ffs_sb->s_sb_block = sb_block;
	//kzalloc(sizeof(struct flatfs_sb_info), GFP_KERNEL); // kzalloc=kalloc+memset（0），GFP_KERNEL是内存分配标志
	printk(KERN_INFO "flatfs: sb->s_fs_info init ok\n");
	ffs_sb = FFS_SB(sb);
	if (!ffs_sb) {
		iput(inode);
		return -ENOMEM;
	}
	init_ino_bitmap(ffs_sb);

	sb->s_root = d_make_root(inode); //用来为fs的根目录（并不一定是系统全局文件系统的根“／”）分配dentry对象。它以根目录的inode对象指针为参数。函数中会将d_parent指向自身，注意，这是判断一个fs的根目录的唯一准则
	printk(KERN_INFO "root name : %s\n", inode_to_name(sb->s_root->d_inode));
	if (!sb->s_root)
	{ //分配结果检测，如果失败
		printk(KERN_INFO "root node create failed\n");
		iput(inode);
		//kfree(ffs_sb->cuckoo);
		//ffs_sb->cuckoo=NULL;
		kfree(ffs_sb);
		return -ENOMEM;
	}

	mark_inode_dirty(inode);
	if(inode) unlock_new_inode(inode);
	/* FS-FILLIN your filesystem specific mount logic/checks here */
	return 0;
}
/*
 * mount flatfs, call kernel util mount_bdev
 * actual work of flatfs is done in flatfs_fill_super
 */
static struct dentry *flatfs_mount(struct file_system_type *fs_type,
								   int flags, const char *dev_name, void *data)
{
	// return mount_nodev(fs_type, flags, data, flatfs_fill_super);//内存文件系统，无实际设备,https://zhuanlan.zhihu.com/p/482045070
	printk(KERN_INFO "start mount of flatfs\n");
	return mount_bdev(fs_type, flags, dev_name, data, flatfs_fill_super);
}

static void flatfs_kill_sb(struct super_block *sb)
{
	printk(KERN_INFO "kill_sb of flatfs\n");
	sync_filesystem(sb);
	kill_block_super(sb);
	printk(KERN_INFO "kill_sb of flatfs OK\n");
}

static struct file_system_type flatfs_fs_type = {
	//文件系统最基本的变量
	.owner = THIS_MODULE,
	.name = "flatfs",
	.mount = flatfs_mount,	   //创建sb,老版本get_sb
	.kill_sb = flatfs_kill_sb, //删除sb
							   /*  .fs_flags */
};

static void init_once(void *foo)
{
	struct ffs_inode_info *fi = (struct ffs_inode_info *) foo;
	inode_init_once(&fi->vfs_inode);
}

static int __init init_inodecache(void)
{
	ffs_inode_cachep = kmem_cache_create("ffs_inode_cache",
					     sizeof(struct ffs_inode_info),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD|SLAB_ACCOUNT),
					     init_once);
	if (ffs_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

static void destroy_inodecache(void)
{
	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(ffs_inode_cachep);
}

static int __init init_flatfs_fs(void) //宏定义__init表示该函数旨在初始化期间使用，模块装载后就扔掉，释放内存
{
	int err;
	printk(KERN_INFO "init flatfs\n");
	err = init_inodecache();
	if (err)
		return err;
	err = register_filesystem(&flatfs_fs_type); //内核文件系统API,将flatfs添加到内核文件系统链表
	if (err)
		goto out;
	return 0;
out:
	destroy_inodecache();
	return err;	
}

static void __exit exit_flatfs_fs(void)
{
	unregister_filesystem(&flatfs_fs_type);
	destroy_inodecache();
}

module_init(init_flatfs_fs); //宏：模块加载, 调用init_flatfs_fs
module_exit(exit_flatfs_fs);
 MODULE_LICENSE ("GPL v2"); 
