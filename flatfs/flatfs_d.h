
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

#define FLATFS_ROOT_INO 0x00000002UL

#define MAX_FILE_TYPE_NAME 256

#define BUCKET_NR 2500//一个bucket 4个slot，每个slot记录一个inode

#define TOTAL_DEPTH 8	//定义目录深度
#define MAX_DIR_INUM 255 //定义目录ino范围
/* helpful if this is different than other fs */
#define FLATFS_MAGIC 0x73616d70 /* "FLAT" */
#define PAGE_SHIFT 12
#define BLOCK_SHIFT 10
// #define BLOCK_SIZE 512
#define FLATFS_BSTORE_BLOCKSIZE BLOCK_SIZE
#define FLATFS_BSTORE_BLOCKSIZE_BITS BLOCK_SHIFT

typedef u64 lba_t;
#define INIT_SPACE 10

#define FFS_BLOCK_SIZE_BITS 9
#define FFS_BLOCK_SIZE (1 << FFS_BLOCK_SIZE_BITS)

#define FFS_MAX_FILENAME_LEN 64

/* LBA分配设置 */
#define MAX_DIR_BITS 8
#define MIN_DIR_BITS 7

#define MAX_FILE_BUCKET_BITS 20
#define MIN_FILE_BUCKET_BITS 8


#define FILE_SLOT_BITS 3

/* block refers to file offset */
#define MAX_FILE_BLOCK_BITS 40
#define MIN_FILE_BLOCK_BITS 16
#define DEFAULT_FILE_BLOCK_BITS 32

#define LBA_TT_BITS 63

#define BLOCKS_PER_SLOT (1ULL << DEFAULT_FILE_BLOCK_BITS)
#define BLOCKS_PER_BUCKET (1ULL << (DEFAULT_FILE_BLOCK_BITS+FILE_SLOT_BITS))
#define BUCKETS_PER_DIR (1ULL << MIN_FILE_BUCKET_BITS)
#define SLOTS_PER_BUCKET (1ULL << FILE_SLOT_BITS)
#define FILE_META_LBA_BASE 1ULL << (FILE_SLOT_BITS + DEFAULT_FILE_BLOCK_BITS)//文件的inode区域要从这里开始计算
#define FILE_DATA_LBA_BASE 1ULL << (MIN_FILE_BUCKET_BITS + FILE_SLOT_BITS + DEFAULT_FILE_BLOCK_BITS)

/* LBA convert to any segment*/
#define lba_to_block(lba)  (lba & (BLOCKS_PER_SLOT - 1ULL))
#define lba_to_slot(lba)   ((lba >> DEFAULT_FILE_BLOCK_BITS) & (SLOTS_PER_BUCKET - 1ULL))
#define lba_to_bucket(lba) ((lba >> (DEFAULT_FILE_BLOCK_BITS+FILE_SLOT_BITS) & (BUCKETS_PER_DIR - 1ULL))
#define lba_to_dir(lba)    ((lba >> (DEFAULT_FILE_BLOCK_BITS+FILE_SLOT_BITS+MIN_FILE_BUCKET_BITS)))
#define compose_to_lba(dir,bucket,slot,block) (block + ((slot + ((bucket + (dir << MIN_FILE_BUCKET_BITS)) << FILE_SLOT_BITS)) << DEFAULT_FILE_BLOCK_BITS))

#define ino_to_slot(ino) (ino & (SLOTS_PER_BUCKET-1ULL))
#define ino_to_bucket(ino) (ino >> FILE_SLOT_BITS) & (BUCKETS_PER_DIR - 1)

/* for dirent rec len */
#define FFS_DIR_PAD		 	        4
#define FFS_DIR_ROUND 			    (FFS_DIR_PAD - 1)
#define FFS_DIR_REC_LEN(name_len)	(((name_len) + 8 + FFS_DIR_ROUND) & ~FFS_DIR_ROUND)
#define FFS_MAX_REC_LEN		        ((1<<16)-1)

enum {
    ENOINO = 0
};

struct ffs_name {
	__u8	name_len;		/* Name length */
	char	name[FFS_MAX_FILENAME_LEN + 2];			/* Dir name */
};

struct ffs_lba
{
	unsigned long var; // ino
	unsigned size;
	unsigned offset;
};


struct ffs_inode //磁盘inode
{					  
	int valid;
    loff_t size; //尺寸
    struct ffs_name filename;
};

struct ffs_inode_info //内存文件系统特化inode
{					   
    //sector_t lba;//存放
    int dir_id;//文件，该字段表示父目录的id；目录，该字段表示当前目录的id
    int bucket_id; //-1表示目录
    int slot_id;
    struct inode vfs_inode;
    int valid;
    unsigned long i_flags;
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

    /* LBA分配上，每一个字段占的位数 */
    // unsigned short dir_bits;
    // unsigned short bucket_bits;
    // unsigned short slot_bits;
    // unsigned short block_bits;

    //struct dir_entry *parent_entry;

    /* 指向子目录的指针 */
    struct dir_list *subdirs;
    /* 子目录的个数 */
    unsigned long dir_size;
    loff_t pos;
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
    struct dir_entry de[1 << MAX_DIR_BITS];
    unsigned long dir_entry_num;
    // unsigned long ino_bitmap[1 << MAX_DIR_BITS]
    DECLARE_BITMAP(dir_id_bitmap, 1 << MAX_DIR_BITS);
};


static void init_dir_id_bitmap(unsigned long *dir_id_bitmap) {
    bitmap_zero(dir_id_bitmap, 1 << MAX_DIR_BITS);
    bitmap_set(dir_id_bitmap, 0, 1); //不使用
    bitmap_set(dir_id_bitmap, 1, 1); //根结点inode为1
    bitmap_set(dir_id_bitmap, 2, 1); //根结点inode为1
    // printk("bitmap: %lx\n", *dir_id_bitmap);
}

static unsigned long get_unused_dir_id(unsigned long *dir_id_bitmap) {
    unsigned long dir_id;
    dir_id = find_first_zero_bit(dir_id_bitmap, 1 << MAX_DIR_BITS);
    // printk("get unused id: %lu\n", dir_id);
    if(dir_id == 1 << MAX_DIR_BITS) {
        return ENOINO;
    }
    /* 设置该ino为已经被使用 */
    bitmap_set(dir_id_bitmap, dir_id, 1);
    /* root的inode num设定为1， 那么其他目录的ino从2开始编号 */
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
    unsigned long bucket_id;
    DECLARE_BITMAP(slot_bitmap, 1 << FILE_SLOT_BITS);
    __u8 valid_slot_count;
    struct slot slots[1 << FILE_SLOT_BITS];
};

struct HashTable {
    struct bucket buckets[1 << MIN_FILE_BUCKET_BITS];
    loff_t pos;
};


/* ffs在内存superblock */
struct flatfs_sb_info
{ //一般会包含信息和数据结构，kevin的db就是在这里实现的
	//cuckoo_hash_t *cuckoo;
	struct dir_entry *root;
    struct dir_tree  *dtree_root;
    char   name[MAX_FILE_TYPE_NAME];
    struct HashTable *hashtbl;
};

extern unsigned long calculate_slba(struct inode* dir, struct dentry* dentry);
unsigned long flatfs_dir_inode_by_name(struct flatfs_sb_info *sb_i, unsigned long parent_ino, struct qstr *child);


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
void remove_dir(struct flatfs_sb_info *sb_i, unsigned long ino);
int read_dir_dirs(struct flatfs_sb_info *sb_i, unsigned long ino, struct dir_context *ctx);