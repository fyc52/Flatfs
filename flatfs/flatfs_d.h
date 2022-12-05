
#include <linux/list.h>
#include <linux/fs.h>
#include <linux/rwsem.h>
#include <linux/fscache.h>
#include <linux/list_sort.h>
#include <linux/slab.h>
#include <linux/ktime.h>
#include <linux/bitmap.h>
#include <linux/dcache.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/bitmap.h>
#include <linux/types.h>


#define FLATFS_ROOT_INO 0x00000002UL

#define MAX_FILE_TYPE_NAME 256

#define FLATFS_MAGIC 0x73616d70 /* "FLAT" */
#define PAGE_SHIFT 12
#define BLOCK_SHIFT 12
#define BLOCK_SIZE 1 << BLOCK_SHIFT
// #define BLOCK_SIZE 512
#define FLATFS_BSTORE_BLOCKSIZE BLOCK_SIZE
#define FLATFS_BSTORE_BLOCKSIZE_BITS BLOCK_SHIFT

#define INVALID_LBA 0UL
#define INVALID_INO 0U

#define FFS_BLOCK_SIZE_BITS 12
#define FFS_BLOCK_SIZE (1 << FFS_BLOCK_SIZE_BITS)

#define FFS_MAX_FILENAME_LEN 100


/* for dirent rec len */
#define FFS_DIR_PAD		 	        4
#define FFS_DIR_ROUND 			    (FFS_DIR_PAD - 1)
#define FFS_DIR_REC_LEN(name_len)	(((name_len) + 8 + FFS_DIR_ROUND) & ~FFS_DIR_ROUND)
#define FFS_MAX_REC_LEN		        ((1<<16)-1)


struct ffs_name {
	__u16	name_len;		/* Name length */
	char	name[FFS_MAX_FILENAME_LEN + 2];			/* Dir name */
};


struct ffs_inode         // 磁盘inode
{					  
	__le16	i_mode;		/* File mode */
	__le16	i_uid;		/* Low 16 bits of Owner Uid */
	__le32	i_size;		/* Size in bytes */
	__le32	i_atime;	/* Access time */
	__le32	i_ctime;	/* Creation time */
	__le32	i_mtime;	/* Modification time */
	__le32	i_dtime;	/* Deletion Time */
	__le16	i_gid;		/* Low 16 bits of Group Id */
	__le16	i_links_count;	/* Links count */
	__le32	i_blocks;	/* Blocks count */
	__le32	i_flags;	/* File flags */
    int valid;
    struct ffs_name filename;
};


struct ffs_inode_info   // 内存文件系统特化inode
{					   
    struct inode vfs_inode;
    unsigned long i_flags;
    struct ffs_name filename;
    __u32	i_dir_start_lookup; 
	//spinlock_t i_raw_lock;/* protects updates to the raw inode */
};

static inline struct ffs_inode_info *FLAT_I(struct inode *inode)
{
	return container_of(inode, struct ffs_inode_info, vfs_inode);
}

static inline struct flatfs_sb_info *
FFS_SB(struct super_block *sb)
{
	return sb->s_fs_info; //文件系统特殊信息
}

static inline struct ffs_inode_info *FFS_I(struct inode *inode)
{
	return container_of(inode, struct ffs_inode_info, vfs_inode);
}


#define MAX_INODE_NUM (1U << 20)

/* 
    ffs在内存superblock,
    一般会包含信息和数据结构，kevin的db就是在这里实现的
*/
struct flatfs_sb_info
{
	struct dir_entry *root;
    struct dir_tree  *dtree_root;
    char   name[MAX_FILE_TYPE_NAME];
    DECLARE_BITMAP(ino_bitmap, MAX_INODE_NUM);
};

static inline void init_ino_bitmap(struct flatfs_sb_info *sbi) {
    bitmap_clear(sbi->ino_bitmap, 0, MAX_INODE_NUM);
    bitmap_set(sbi->ino_bitmap, 0, 3);
}

static inline unsigned get_unused_ino(struct flatfs_sb_info *sbi) {

    unsigned ino = find_first_zero_bit(sbi->ino_bitmap, MAX_INODE_NUM);
    if(ino == MAX_INODE_NUM) {
        return MAX_INODE_NUM;
    }
    bitmap_set(sbi->ino_bitmap, ino, 1);
    return ino;
}

static inline void free_ino(struct flatfs_sb_info *sbi, unsigned ino) {
    bitmap_clear(sbi->ino_bitmap, ino, 1);
}


/*
 * NOTE! unlike strncmp, ffs_match returns 1 for success, 0 for failure.
 *
*/
static inline int ffs_match(int len, const char * name,
					struct ffs_name * dn)
{
    if (len != dn->name_len)
		return 0;
	return !memcmp(name, dn->name, len);
}

static inline int my_strlen(char *name)
{
    int len = 0;
    while(name[len] != '\0') len ++;
    return len;
}

static inline char * inode_to_name(struct inode * ino)
{
    struct hlist_node *tmp_list = NULL;
	struct inode* pinode = ino;
 	struct dentry *s_dentry = NULL;
	hlist_for_each(tmp_list, &(pinode->i_dentry))
	{
    	s_dentry = hlist_entry(tmp_list, struct dentry, d_u.d_alias);
	}
    if(s_dentry == NULL) printk("Dentry is NULL");
    return (char* )(s_dentry->d_name.name);
}


/* hash.c */
#define hashfs_data_start (1UL<<18)
#define max_block_num (1UL<<25)
#define hashfs_meta_size 8
#define hashfs_meta_size_bits 3
#define INVALID_LBA 0

extern sector_t hashfs_get_data_lba(struct super_block *sb, ino_t ino, sector_t iblock);
extern sector_t hashfs_set_data_lba(struct inode *inode, sector_t iblock);
extern void hashfs_remove_inode(struct inode *inode);


/* dir.c */
#define HASHFS_DIR_PAD		 	4
#define HASHFS_DIR_ROUND 			(HASHFS_DIR_PAD - 1)
#define HASHFS_DIR_REC_LEN(name_len)	(((name_len) + 8 + HASHFS_DIR_ROUND) & \
					 ~HASHFS_DIR_ROUND)

/*
 * The new version of the directory entry.  Since HASHFS structures are
 * stored in intel byte order, and the name_len field could never be
 * bigger than 255 chars, it's safe to reclaim the extra byte for the
 * file_type field.
 */
struct hashfs_dir_entry_2 {
	__le32	inode;			/* Inode number */
	__le16	rec_len;		/* Directory entry length */
	__u8	name_len;		/* Name length */
	__u8	file_type;
	char	name[];			/* File name, up to HASHFS_NAME_LEN */
};

/* Reserved for compression usage... */
#define HASHFS_DIRTY_FL			FS_DIRTY_FL
#define HASHFS_COMPRBLK_FL		FS_COMPRBLK_FL	/* One or more compressed clusters */
#define HASHFS_NOCOMP_FL			FS_NOCOMP_FL	/* Don't compress */
#define HASHFS_ECOMPR_FL			FS_ECOMPR_FL	/* Compression error */
/* End compression flags --- maybe not all used */	
#define HASHFS_BTREE_FL			FS_BTREE_FL	/* btree format dir */
#define HASHFS_INDEX_FL			FS_INDEX_FL	/* hash-indexed directory */
#define HASHFS_IMAGIC_FL			FS_IMAGIC_FL	/* AFS directory */
#define HASHFS_JOURNAL_DATA_FL		FS_JOURNAL_DATA_FL /* Reserved for ext3 */
#define HASHFS_NOTAIL_FL			FS_NOTAIL_FL	/* file tail should not be merged */
#define HASHFS_DIRSYNC_FL			FS_DIRSYNC_FL	/* dirsync behaviour (directories only) */
#define HASHFS_TOPDIR_FL			FS_TOPDIR_FL	/* Top of directory hierarchies*/
#define HASHFS_RESERVED_FL		FS_RESERVED_FL	/* reserved for hashfs lib */

/* dir.c */
extern ino_t ffs_inode_by_name(struct inode *dir, const struct qstr *child);
extern int hashfs_make_empty(struct inode *inode, struct inode *parent);
extern int hashfs_add_link (struct dentry *dentry, struct inode *inode);
extern int hashfs_empty_dir (struct inode * inode);

/* file.c */
extern int ffs_get_block_prep(struct inode *inode, sector_t iblock, struct buffer_head *bh, int create);
