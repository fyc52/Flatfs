
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

#ifndef _TEST_H_
#define _TEST_H_
#include "cuckoo_hash.h"
#endif

/* trash */

#define FLATFS_ROOT_INO 0x00000002UL

#define MAX_FILE_TYPE_NAME 256

#define BUCKET_NR 2500//一个bucket 4个slot，每个slot记录一个inode

#define TOTAL_DEPTH 8	//定义目录深度
#define MAX_DIR_INUM 255 //定义目录ino范围
/* helpful if this is different than other fs */
#define FLATFS_MAGIC 0x73616d70 /* "FLAT" */
#define PAGE_SHIFT 12
#define BLOCK_SHIFT 12
#define BLOCK_SIZE 1 << BLOCK_SHIFT
// #define BLOCK_SIZE 512
#define FLATFS_BSTORE_BLOCKSIZE BLOCK_SIZE
#define FLATFS_BSTORE_BLOCKSIZE_BITS BLOCK_SHIFT

typedef __u64 lba_t;
typedef __u32 ffs_ino_t;

#define INVALID_LBA 0UL
#define INVALID_INO 0U
#define INIT_SPACE 10

#define FFS_BLOCK_SIZE_BITS 12
#define FFS_BLOCK_SIZE (1 << FFS_BLOCK_SIZE_BITS)

#define FFS_MAX_FILENAME_LEN 100

/* LBA分配设置 */
#define TAG_BITS 1

#define MAX_DIR_BITS 14
#define MIN_DIR_BITS 6

#define MAX_FILE_BUCKET_BITS 20
#define MIN_FILE_BUCKET_BITS 12


#define FILE_SLOT_BITS 3
#define FILE_SLOT_NUM  (1<<FILE_SLOT_BITS)

/* block refers to file offset */
#define MAX_FILE_BLOCK_BITS 40
#define MIN_FILE_BLOCK_BITS 16
#define DEFAULT_FILE_BLOCK_BITS 32

#define LBA_TT_BITS 62

#define BLOCKS_PER_SLOT (1ULL << DEFAULT_FILE_BLOCK_BITS)
#define BLOCKS_PER_BUCKET (1ULL << (DEFAULT_FILE_BLOCK_BITS+FILE_SLOT_BITS))
#define BUCKETS_PER_DIR (1ULL << MIN_FILE_BUCKET_BITS)
#define BUCKETS_PER_BIG_DIR (1ULL << (MAX_FILE_BUCKET_BITS + MIN_DIR_BITS))
#define SLOTS_PER_BUCKET (1ULL << FILE_SLOT_BITS)
#define FILE_META_LBA_BASE (1ULL << (FILE_SLOT_BITS + DEFAULT_FILE_BLOCK_BITS))//文件的inode区域要从这里开始计算


/* LBA convert to any segment*/
#define lba_to_block(lba)  (lba & (BLOCKS_PER_SLOT - 1ULL))
#define lba_to_slot(lba)   ((lba >> DEFAULT_FILE_BLOCK_BITS) & (SLOTS_PER_BUCKET - 1ULL))
#define lba_to_bucket(lba) ((lba >> (DEFAULT_FILE_BLOCK_BITS+FILE_SLOT_BITS) & (BUCKETS_PER_DIR - 1ULL))
#define lba_to_dir(lba)    ((lba >> (DEFAULT_FILE_BLOCK_BITS+FILE_SLOT_BITS+MIN_FILE_BUCKET_BITS)))
#define compose_to_lba(dir,bucket,slot,block) (block + ((slot + ((bucket + (dir << MIN_FILE_BUCKET_BITS)) << FILE_SLOT_BITS)) << DEFAULT_FILE_BLOCK_BITS))

#define ino_to_slot(ino) (ino & (SLOTS_PER_BUCKET-1ULL))
#define ino_to_bucket(ino) (ino >> FILE_SLOT_BITS) & (BUCKETS_PER_DIR - 1)

#define big_ino_to_slot(ino) (ino & (SLOTS_PER_BUCKET-1ULL))
#define big_ino_to_bucket(ino) (ino >> FILE_SLOT_BITS) & (BUCKETS_PER_BIG_DIR - 1)

/* for dirent rec len */
#define FFS_DIR_PAD		 	        4
#define FFS_DIR_ROUND 			    (FFS_DIR_PAD - 1)
#define FFS_DIR_REC_LEN(name_len)	(((name_len) + 8 + FFS_DIR_ROUND) & ~FFS_DIR_ROUND)
#define FFS_MAX_REC_LEN		        ((1<<16)-1)


/*****************************************************/

/*****************************************************/
/* total lba */
#define TOTAL_LBA_BITS (61)

/* block offset */
#define BLOCK_OFFSET_BITS   (32)

/* total slots in bucket */
#define SLOT_BITS     (3)

/* common dir mode */
/* total buckets in hash table */
#define S_BUCKET_BITS (12)
/* total dirs */
#define S_DIR_BITS    (14)
#define DIR_BITS  S_DIR_BITS

/* extend dir mode */
/* total buckets in hash table */
#define L_BUCKET_BITS (20)
/* total dirs */
#define L_DIR_BITS    (6)

/* 
    bit gap between small dir nad large dir,
    for large dir add 0 in lower bits
*/
#define SL_DIR_DIFF   (S_DIR_BITS - L_DIR_BITS)

/* (dir, bucket) total seg */
#define FILE_BITS     (29)

/* X tag for common/extend dir */
#define X_BITS        (1)

/* capacity number for each segment */
#define S_BUCKET_NUM  (1UL << S_BUCKET_BITS)
#define S_DIR_NUM     (1UL << S_DIR_BITS)
#define L_BUCKET_NUM  (1UL << L_BUCKET_BITS)
#define L_DIR_NUM     (1UL << L_DIR_BITS)
#define TT_DIR_NUM    (1UL << (S_DIR_BITS + X_BITS))
#define SLOT_NUM      (1UL << SLOT_BITS)
#define BIG_TAG_BASE  (1ULL << (L_DIR_BITS + L_BUCKET_BITS + SLOT_BITS + BLOCK_OFFSET_BITS))

struct lba {
    union {
        /* data lba in common(small) dir mode */
        struct {
            uint64_t off  : BLOCK_OFFSET_BITS;
            uint64_t slot : SLOT_BITS;
            uint64_t bkt  : S_BUCKET_BITS;
            uint64_t dir  : S_DIR_BITS;
            uint64_t xtag : X_BITS;
            uint64_t rsv  : 2;
        } s_seg;

        /* data lba in extend(large) dir mode */
        struct {
            uint64_t off  : BLOCK_OFFSET_BITS;
            uint64_t slot : SLOT_BITS;
            uint64_t bkt  : L_BUCKET_BITS;
            uint64_t dir  : L_DIR_BITS;
            uint64_t xtag : X_BITS;
            uint64_t rsv  : 2;
        } l_seg;

        /* meta data lba in extend(large) dir mode */
        struct {
            uint64_t off  : FFS_BLOCK_SIZE_BITS;
            // uint64_t slot : SLOT_BITS;
            uint64_t bkt  : S_BUCKET_BITS;
            uint64_t dir  : S_DIR_BITS;
            uint64_t rsv1 : TOTAL_LBA_BITS - S_DIR_BITS - S_BUCKET_BITS - SLOT_BITS - FFS_BLOCK_SIZE_BITS;
            uint64_t xtag : X_BITS;
            uint64_t rsv2 : 2;
        } s_meta_seg;

        struct {
            uint64_t off  : FFS_BLOCK_SIZE_BITS;
            // uint64_t slot : SLOT_BITS;
            uint64_t bkt  : L_BUCKET_BITS;
            uint64_t dir  : L_DIR_BITS;
            uint64_t rsv1 : TOTAL_LBA_BITS - L_DIR_BITS - L_BUCKET_BITS - SLOT_BITS - FFS_BLOCK_SIZE_BITS;
            uint64_t xtag : X_BITS;
            uint64_t rsv2 : 2;
        } l_meta_seg;

        struct {
            uint64_t off  : FFS_BLOCK_SIZE_BITS;
            uint64_t dir  : S_DIR_BITS;
            uint64_t rsv  : 38;
        } d_meta_seg;

        lba_t lba;
    };
};


struct ffs_ino {
    union {
        /* data lba in common(small) dir mode */
        struct {
            uint32_t slot : SLOT_BITS;
            uint32_t bkt  : S_BUCKET_BITS;
            uint32_t dir  : S_DIR_BITS;
            uint32_t xtag : X_BITS;
            uint32_t rsv  : 2;
        } s_file_seg;

        struct {
            uint32_t slot : SLOT_BITS;
            uint32_t bkt  : L_BUCKET_BITS;
            uint32_t dir  : L_DIR_BITS;
            uint32_t xtag : X_BITS;
            uint32_t rsv  : 2;
        } l_file_seg;

        struct {
            uint32_t dir  : DIR_BITS;
            uint32_t xtag : X_BITS;
            uint32_t rsv  : 17;
        } dir_seg;

        ffs_ino_t ino;
    };
};

enum dir_type {
    small,
    large,
};

#define INVALID_DIR_ID TT_DIR_NUM

/*****************************************************/

/*****************************************************/

/*****************************************************/

/*****************************************************/


/* trash2 */

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
    __u8 is_big_dir;
    int dir_id2;        // large dir id
    unsigned long i_flags;
    loff_t size;
    struct ffs_name filename;
    struct ffs_ino ino;
    struct lba lba;
    //__u8 i_type;  
	//spinlock_t i_raw_lock;/* protects updates to the raw inode */
	//struct buffer_head *i_bh;	/*i_bh contains a new or dirty disk inode.*/
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
    enum dir_type dtype;
    /* if dtype is large, this field refers to large dir id */
    unsigned long dir_id2;
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
    DECLARE_BITMAP(sdir_id_bitmap, S_DIR_NUM);
    DECLARE_BITMAP(ldir_id_bitmap, L_DIR_NUM);
};


static void init_dir_id_bitmap(struct dir_tree *dtree) {
    bitmap_zero(dtree->sdir_id_bitmap, S_DIR_NUM);
    bitmap_set(dtree->sdir_id_bitmap, 0, 1); //不使用
    bitmap_set(dtree->sdir_id_bitmap, 1, 1); //根结点inode为1
    bitmap_set(dtree->sdir_id_bitmap, 2, 1); //根结点inode为1
    bitmap_zero(dtree->ldir_id_bitmap, L_DIR_NUM);
    // printk("bitmap: %lx\n", *dir_id_bitmap);
}


static inline unsigned get_unused_dir_id(struct dir_tree *dtree, int tag) {
    unsigned long *bitmap;
    unsigned dir_id;
    int border;

    if (tag == large) {
        bitmap = dtree->ldir_id_bitmap;
        border = L_DIR_NUM;
    }
    else {
        bitmap = dtree->sdir_id_bitmap;
        border = S_DIR_NUM;
    }
    
    dir_id = find_first_zero_bit(bitmap, border);
    if(dir_id == border) {
        return INVALID_DIR_ID;
    }
    /* 设置该ino为已经被使用 */
    bitmap_set(bitmap, dir_id, 1);
    return dir_id;
}


/*                            
 *    Copied for File Hash index
 *     
 *     bucket id  +  slot id
 *
*/ 
struct slot {
    __u8 slot_id;
    struct ffs_name filename;
};

struct bucket {
    DECLARE_BITMAP(slot_bitmap, SLOT_NUM);
    __u8 valid_slot_count;
    spinlock_t		bkt_lock;
};

struct HashTable {
    struct bucket *buckets;
    loff_t pos;
    enum dir_type dtype;
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
	int dir_parent[1 << MAX_DIR_BITS];
    __u16 vaild_dir_num;

};

extern unsigned long calculate_slba(struct inode* dir, struct dentry* dentry);
ffs_ino_t flatfs_dir_inode_by_name(struct flatfs_sb_info *sb_i, unsigned long parent_ino, struct qstr *child);


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

unsigned long fill_one_dir_entry(struct flatfs_sb_info *sb_i, char *dir_name);
void insert_dir(struct flatfs_sb_info *sb_i, unsigned long parent_dir_id, unsigned long insert_dir_id);
void dir_exit(struct flatfs_sb_info *sb_i);
void init_dir_tree(struct dir_tree **dtree);
void init_root_entry(struct flatfs_sb_info *sb_i, struct inode * ino);
void remove_dir(struct flatfs_sb_info *sb_i, unsigned long parent_ino, unsigned long dir_ino);
int read_dir_dirs(struct flatfs_sb_info *sb_i, unsigned long ino, struct dir_context *ctx);
unsigned resize_dir(struct flatfs_sb_info *sb_i, int dir_id);