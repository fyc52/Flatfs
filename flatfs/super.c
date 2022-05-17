#include <linux/module.h>
#include <linux/fs.h>
#include <linux/version.h>

static int flatfs_fill_super(struct super_block * sb, void * data, int silent)
{
	return 0;
}

int flatfs_get_sb(struct file_system_type *fs_type,
        int flags, const char *dev_name, void *data, struct vfsmount *mnt)
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
	.mount = flatfs_get_sb,     //创建sb
	.kill_sb = flatfs_kill_sb,  //删除sb
	/*  .fs_flags */
};

static int __init init_flatfs_fs(void) //__init表示该函数旨在初始化期间使用，模块装载后就扔掉，释放内存
{
	return register_filesystem(&flatfs_fs_type); //内核文件系统API
}

static void __exit exit_flatfs_fs(void)
{
	unregister_filesystem(&flatfs_fs_type);
}

module_init(init_flatfs_fs) //模块加载
module_exit(exit_flatfs_fs)
