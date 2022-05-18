#include <linux/module.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/version.h>
#include <linux/nls.h>
#include "flatfs.h"

static int flatfs_super_statfs(struct dentry *d, struct kstatfs *buf) {
	return 0;
}

struct super_operations flatfs_super_ops = {
	.statfs         = flatfs_super_statfs,
	.drop_inode     = generic_delete_inode, /* Not needed, is the default */
	.put_super      = flatfs_put_super,
};

static int flatfs_fill_super(struct super_block * sb, void * data, int silent)//mount时被调用，会创建一个sb
{
	printk("fill sb of flatfs\n");
	return 0;
}
/*
 * mount flatfs, call kernel util mount_bdev
 * actual work of flatfs is done in flatfs_fill_super
 */
static struct dentry *flatfs_mount(struct file_system_type *fs_type,
        int flags, const char *dev_name, void *data)
{
	//return get_sb_nodev(fs_type, flags, data, flatfs_fill_super, mnt);//内存文件系统，无实际设备
	return mount_bdev(fs_type, flags, dev_name, data, flatfs_fill_super);
	//return mount_bdev(fs_type, flags, dev_name, data, lightfs_fill_super);//后续替换
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
