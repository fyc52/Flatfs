#include <linux/module.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/version.h>
#include <linux/nls.h>
#include<linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/backing-dev.h>
#include "flatfs.h"

static int flatfs_super_statfs(struct dentry *d, struct kstatfs *buf) {
	return 0;
}
static void
flatfs_put_super(struct super_block *sb)
{
	struct flatfs_sb_info *ffs_sb;

	ffs_sb = FFS_SB(sb);
	if (ffs_sb == NULL) {
		/* Empty superblock info passed to unmount */
		return;
	}

	
 
	/* FS-FILLIN your fs specific umount logic here */

	kfree(ffs_sb);
	return;
}

struct super_operations flatfs_super_ops = {
	.statfs         = flatfs_super_statfs,
	.drop_inode     = generic_delete_inode, /* VFS提供的通用函数，会判断是否定义具体文件系统的超级块操作函数delete_inode，若定义的就调用具体的inode删除函数(如ext3_delete_inode )，否则调用truncate_inode_pages和clear_inode函数(在具体文件系统的delete_inode函数中也必须调用这两个函数)。 */
	.put_super      = flatfs_put_super,
};

struct inode *flatfs_get_inode(struct super_block *sb, int mode, dev_t dev)
{
        struct inode * inode = new_inode(sb);//https://blog.csdn.net/weixin_43836778/article/details/90236819
	struct flatfs_sb_info * ffs_sb = FFS_SB(sb);

        if (inode) {
                inode->i_mode = mode;//访问权限,https://zhuanlan.zhihu.com/p/78724124
                inode->i_uid = current->fsuid;/* Low 16 bits of Owner Uid */
                inode->i_gid = current->fsgid;/* Low 16 bits of Group Id */
                inode->i_blocks = 0;//文件的块数
                inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;//访问、修改、创建时间
		printk(KERN_INFO "about to set inode ops\n");
		inode->i_mapping->a_ops = &ffs_aops;//相关的地址映射
		//inode->i_mapping->backing_dev_info = &ffs_backing_dev_info;
                switch (mode & S_IFMT) {/* type of file ，S_IFMT是文件类型掩码,用来取mode的0--3位*/
                default:
			init_special_inode(inode, mode, dev);
			break;
                case S_IFREG:/* regular 普通文件*/
			printk(KERN_INFO "file inode\n");
			//inode->i_op = &ffs_file_inode_ops;
			inode->i_fop =  &ffs_file_operations;
			break;
                case S_IFDIR:/* directory 目录文件*/
			printk(KERN_INFO "directory inode ffs_sb: %p\n",ffs_sb);
			//inode->i_op = &ffs_dir_inode_ops;
			//inode->i_fop = &simple_dir_operations;

                        /* link == 2 (for initial ".." and "." entries) */
                        inode->i_nlink++;//i_nlink是文件硬链接数,目录是由至少2个dentry指向的：./和../，所以是2
						break;
                }
        }
        return inode;
	
}

static int flatfs_fill_super(struct super_block * sb, void * data, int silent)//mount时被调用，会创建一个sb
{
	struct inode *inode;
	struct flatfs_sb_info *ffs_sb;

	sb->s_maxbytes = MAX_LFS_FILESIZE; /*文件大小上限*/
	sb->s_blocksize = FLATFS_BSTORE_BLOCKSIZE ; //以字节为单位的块大小
	sb->s_blocksize_bits = FLATFS_BSTORE_BLOCKSIZE_BITS; //以位为单位的块大小
	sb->s_magic = FLATFS_MAGIC; //可能是用来内存分配的地址
	sb->s_op = &flatfs_super_ops;//sb操作
	sb->s_time_gran = 1; /* 时间戳的粒度（单位为纳秒) */

	printk(KERN_INFO "flatfs: fill super\n");

	inode = flatfs_get_inode(sb, S_IFDIR | 0755, 0);//分配根目录的inode,增加引用计数，对应iput;S_IFDIR表示是一个目录,后面0755是权限位:https://zhuanlan.zhihu.com/p/48529974

	if (!inode)
		return -ENOMEM;

	sb->s_fs_info = kzalloc(sizeof(struct flatfs_sb_info), GFP_KERNEL);//kzalloc=kalloc+memset（0），GFP_KERNEL是内存分配标志
	ffs_sb = FFS_SB(sb);
	if (!ffs_sb) {
		iput(inode);
		return -ENOMEM;
	}

	sb->s_root = d_alloc_root(inode);//用来为fs的根目录（并不一定是系统全局文件系统的根“／”）分配dentry对象。它以根目录的inode对象指针为参数。函数中会将d_parent指向自身，注意，这是判断一个fs的根目录的唯一准则
	if (!sb->s_root) {//分配结果检测，如果失败
		iput(inode);
		kfree(ffs_sb);
		return -ENOMEM;
	}
	
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
	return get_sb_nodev(fs_type, flags, data, flatfs_fill_super);//内存文件系统，无实际设备,https://zhuanlan.zhihu.com/p/482045070
	//return mount_bdev(fs_type, flags, dev_name, data, flatfs_fill_super);//后续替换
}

static void flatfs_kill_sb(struct super_block *sb)
{
	//sync_filesystem(sb);
	//kill_block_super(sb);
}

static struct file_system_type flatfs_fs_type = { //文件系统最基本的变量
	.owner = THIS_MODULE,
	.name = "flatfs",
	.mount = flatfs_mount,     //创建sb,老版本get_sb
	.kill_sb = flatfs_kill_sb,  //删除sb
	/*  .fs_flags */
};

static int __init init_flatfs_fs(void) //宏定义__init表示该函数旨在初始化期间使用，模块装载后就扔掉，释放内存
{
	printk(KERN_INFO "init flatfs\n");
	return register_filesystem(&flatfs_fs_type); //内核文件系统API,将flatfs添加到内核文件系统链表
}

static void __exit exit_flatfs_fs(void)
{
	unregister_filesystem(&flatfs_fs_type);
}

module_init(init_flatfs_fs) //宏：模块加载, 调用init_flatfs_fs
module_exit(exit_flatfs_fs)
