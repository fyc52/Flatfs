#define FLATFS_ROOT_I 2

/* This is an example of filesystem specific mount data that a file system might
   want to store.  FS per-superblock data varies widely and some fs do not
   require any information beyond the generic info which is already in
   struct super_block */
struct flatfs_sb_info {
	unsigned int rsize;
	unsigned int wsize;
	int mnt_flags;
	struct nls_table *local_nls;
};

static inline struct flatfs_sb_info *
FFS_SB(struct super_block *sb)
{
	return sb->s_fs_info;//文件系统特殊信息
}
