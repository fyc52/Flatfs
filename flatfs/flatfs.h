#define FLATFS_ROOT_I 2
/* helpful if this is different than other fs */
#define FLATFS_MAGIC     0x73616d70 /* "FLAT" */
#define FLATFS_BSTORE_BLOCKSIZE		PAGE_SIZE
#define FLATFS_BSTORE_BLOCKSIZE_BITS	PAGE_SHIFT



/* This is an example of filesystem specific mount data that a file system might
   want to store.  FS per-superblock data varies widely and some fs do not
   require any information beyond the generic info which is already in
   struct super_block */
struct flatfs_sb_info {//一般会包含信息和数据结构，kevin的db就是在这里实现的

	int flags;
	
};

static inline struct flatfs_sb_info *
FFS_SB(struct super_block *sb)
{
	return sb->s_fs_info;//文件系统特殊信息
}
