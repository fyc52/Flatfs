
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

typedef __u64 lba_t;
typedef __u32 ffs_ino_t;

#define FLATFS_ROOT_INO 0x00000002UL

#define MAX_FILE_TYPE_NAME 256

/* helpful if this is different than other fs */
#define FLATFS_MAGIC 0x73616d70 /* "FLAT" */
#define PAGE_SHIFT 12
#define BLOCK_SHIFT 12
#define BLOCK_SIZE 1 << BLOCK_SHIFT
// #define BLOCK_SIZE 512
#define FLATFS_BSTORE_BLOCKSIZE BLOCK_SIZE
#define FLATFS_BSTORE_BLOCKSIZE_BITS BLOCK_SHIFT

#define INVALID_LBA 0UL
#define INVALID_INO 0U
#define INIT_SPACE 10

#define FFS_BLOCK_SIZE_BITS 12
#define FFS_BLOCK_SIZE (1 << FFS_BLOCK_SIZE_BITS)

#define FFS_MAX_FILENAME_LEN 100

/* LBA分配设置 */
#define LBA_TT_BITS 62

#define DIR_BITS 9

#define FILE_BUCKET_BITS 20
#define FILE_BUCKET_NUM (1 << FILE_BUCKET_BITS)
#define FILE_SLOT_BITS 3
#define FILE_SLOT_NUM  (1 << FILE_SLOT_BITS)

/* block refers to file offset */
#define FILE_BLOCK_BITS LBA_TT_BITS - DIR_BITS - FILE_SLOT_BITS - FILE_BUCKET_BITS - FFS_BLOCK_SIZE_BITS
#define FILE_BLOCK_SIZE (1ULL << FILE_BLOCK_BITS)


/* for dirent rec len */
#define FFS_DIR_PAD		 	        4
#define FFS_DIR_ROUND 			    (FFS_DIR_PAD - 1)
#define FFS_DIR_REC_LEN(name_len)	(((name_len) + 8 + FFS_DIR_ROUND) & ~FFS_DIR_ROUND)
#define FFS_MAX_REC_LEN		        ((1<<16)-1)

/* capacity number for each segment */
#define TT_BUCKET_NUM (1UL << FILE_BUCKET_BITS)
#define TT_DIR_NUM    (1UL << DIR_BITS)
#define SLOT_NUM      (1UL << FILE_SLOT_BITS)
#define INVALID_DIR_ID TT_DIR_NUM

struct lba {
    union {
        /* data lba in common(small) dir mode */
        struct {
            uint64_t iblk : FILE_BLOCK_BITS;
            uint64_t slot : FILE_SLOT_BITS;
            uint64_t bkt  : FILE_BUCKET_BITS;
            uint64_t dir  : DIR_BITS;
            uint64_t rsv  : 13;
        } data_seg;

        struct {
            uint64_t bkt  : FILE_BUCKET_BITS;
            uint64_t dir  : DIR_BITS;
            uint64_t rsv1 : LBA_TT_BITS - DIR_BITS - FILE_BUCKET_BITS;
            uint64_t rsv2 : 1;
        } file_meta_seg;

        struct {
            uint64_t dir  : DIR_BITS;
            uint64_t rsv1 : LBA_TT_BITS - DIR_BITS;
            uint64_t rsv2 : 1;
        } dir_meta_seg;

        lba_t lba;
    };
};


struct ffs_ino {
    union {
        /* data lba in common(small) dir mode */
        struct {
            uint64_t slot : FILE_SLOT_BITS;
            uint64_t bkt  : FILE_BUCKET_BITS;
            uint64_t dir  : DIR_BITS;
            uint64_t rsv  : 32;
        } file_seg;

        struct {
            uint64_t dir  : DIR_BITS;
            uint64_t rsv  : 45;
        } dir_seg;

        ffs_ino_t ino;
    };
};

struct ffs_name {
	__u16	name_len;		/* Name length */
	char	name[FFS_MAX_FILENAME_LEN + 2];			/* Dir name */
};

struct ffs_inode_page_header
{
    DECLARE_BITMAP(slot_bitmap, SLOT_NUM);
    int valid_slot_num;
    __u32 reserved[13];
};

struct ffs_inode         // 磁盘inode
{					  
	int valid;
    loff_t size;
    struct ffs_name filename;
};

struct ffs_inode_page   // 磁盘inode_page
{					  
	struct ffs_inode_page_header header;
    struct ffs_inode inode[SLOT_NUM];
};

struct ffs_inode_info   // 内存文件系统特化inode
{					   
    int dir_id;         // 文件: 表示父目录的id；目录：表示当前目录的id
    int bucket_id;      // -1表示目录
    int slot_id;
    struct inode vfs_inode;
    int valid;
    //unsigned long i_flags;
    loff_t size;
    struct ffs_name filename;
};

static inline struct flatfs_sb_info *
FFS_SB(struct super_block *sb)
{
	return sb->s_fs_info; //文件系统特殊信息
}


/* ls alias lba space */
#define DIR_NAME_LEN (24)
#define DIRENT_NUM   (128)

struct ffs_dirent {
    __u8 namelen;
    char name[DIR_NAME_LEN];
};

struct direntPage {
    struct ffs_dirent dirents[DIRENT_NUM];
};


/** dir tree **/
struct dir_entry {
    /* 目录的ino */
    unsigned long dir_id;
    /* 目录名 */
    char dir_name[FFS_MAX_FILENAME_LEN + 2];
    int namelen;
    unsigned short rec_len;

    /* 指向子目录的指针 */
    struct dir_list *subdirs;
    /* 子目录的个数 */
    unsigned long dir_size;
    loff_t pos;
    /* if dtype is large, this field refers to large dir id */
};


struct dir_list_entry {
    struct dir_entry *de;
    struct dir_list_entry *last;
    struct dir_list_entry *next;
};


struct dir_list {
    struct dir_list_entry *head;
    struct dir_list_entry *tail;
};


struct dir_tree {
    struct dir_entry de[TT_DIR_NUM];
    unsigned long dir_entry_num;
    DECLARE_BITMAP(dir_id_bitmap, TT_DIR_NUM);
};


static void init_dir_id_bitmap(struct dir_tree *dtree) {
    bitmap_zero(dtree->dir_id_bitmap, TT_DIR_NUM);
    bitmap_set(dtree->dir_id_bitmap, 0, 1); //不使用
    bitmap_set(dtree->dir_id_bitmap, 1, 1); //根结点inode为1
    bitmap_set(dtree->dir_id_bitmap, 2, 1); //根结点inode为2
}


static inline unsigned get_unused_dir_id(struct dir_tree *dtree) {
    unsigned long *bitmap;
    unsigned dir_id;
    int border;

    bitmap = dtree->dir_id_bitmap;
    border = TT_DIR_NUM;
    
    dir_id = find_first_zero_bit(bitmap, border);
    if(dir_id == border) {
        return INVALID_DIR_ID;
    }
    /* 设置该ino为已经被使用 */
    bitmap_set(bitmap, dir_id, 1);
    return dir_id;
}

struct slot {
    __u8 slot_id;
    struct ffs_name filename;
};

struct bucket {
    DECLARE_BITMAP(slot_bitmap, SLOT_NUM);
    __u8 valid_slot_count;
    spinlock_t bkt_lock;
};

struct HashTable {
    struct bucket *buckets;
    loff_t pos;
	__u32 total_slot_count;
};

/* 
    ffs在内存superblock,
    一般会包含信息和数据结构，kevin的db就是在这里实现的
*/
struct flatfs_sb_info
{
	struct dir_entry *root;
    struct dir_tree  *dtree_root;
    char   name[MAX_FILE_TYPE_NAME];
    struct HashTable *hashtbl[TT_DIR_NUM];
};

/* disk super block */ 
struct dir_tree_meta
{ 
    /* fill vaild dir id */ 
	int dir_parent[1 << DIR_BITS];
    __u16 vaild_dir_num;

};

static inline int my_strlen(char *name)
{
    int len = 0;
    while(name[len] != '\0') len ++;
    return len;
}

unsigned long fill_one_dir_entry(struct flatfs_sb_info *sb_i, char *dir_name);
void insert_dir(struct flatfs_sb_info *sb_i, unsigned long parent_dir_id, unsigned long insert_dir_id);
void dir_exit(struct flatfs_sb_info *sb_i);
void init_dir_tree(struct dir_tree **dtree);
void init_root_entry(struct flatfs_sb_info *sb_i, struct inode * ino);
void remove_dir(struct flatfs_sb_info *sb_i, unsigned long parent_ino, unsigned long dir_ino);
int read_dir_dirs(struct flatfs_sb_info *sb_i, unsigned long ino, struct dir_context *ctx);
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
extern struct dentry *d_make_root(struct inode *root_inode);
ffs_ino_t flatfs_dir_inode_by_name(struct flatfs_sb_info *sb_i, unsigned long parent_ino, struct qstr *child);