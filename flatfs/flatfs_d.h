

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
#include "cuckoo_hash.h"


#define BUCKET_NR 2500//一个bucket 4个slot，每个slot记录一个inode

#define TOTAL_DEPTH 8	//定义目录深度
#define MAX_DIR_INUM 255 //定义目录ino范围
/* helpful if this is different than other fs */
#define FLATFS_MAGIC 0x73616d70 /* "FLAT" */
#define PAGE_SHIFT 12
#define BLOCK_SHIFT 9
#define BLOCK_SIZE 512
#define FLATFS_BSTORE_BLOCKSIZE BLOCK_SIZE
#define FLATFS_BSTORE_BLOCKSIZE_BITS BLOCK_SHIFT

#define lba_t sector_t
#define INIT_SPACE 10

#define FFS_BLOCK_SIZE_BITS 9
#define FFS_BLOCK_SIZE (1 << FFS_BLOCK_SIZE_BITS)

/* LBA分配设置 */
#define MAX_DIR_BITS 15
#define MIN_DIR_BITS 7

#define MAX_FILE_BUCKET_BITS 20
#define MIN_FILE_BUCKET_BITS 12

#define FILE_SLOT_BITS 3

#define MAX_FILE_BLOCK_BITS 40
#define MIN_FILE_BLOCK_BITS 16
#define DEFAULT_FILE_BLOCK_BITS 32

#define LBA_TT_BITS 63

#define BUCKETS_PER_DIR (1 << MIN_FILE_BUCKET_BITS)
#define BLOCKS_PER_BUCKET (1 << FILE_SLOT_BITS)
#define FILE_META_LBA_BASE 1 << (FILE_SLOT_BITS + DEFAULT_FILE_BLOCK_BITS)//文件的inode区域要从这里开始计算
#define FILE_DATA_LBA_BASE 1 << (MIN_FILE_BUCKET_BITS + FILE_SLOT_BITS + DEFAULT_FILE_BLOCK_BITS)

enum {
    ENOINO = 0
};


struct ffs_lba
{
	unsigned long var; // ino
	unsigned size;
	unsigned offset;
};


struct ffs_inode //磁盘inode
{					  
	loff_t size; //尺寸
    char* filename;
};

struct ffs_inode_info //内存文件系统特化inode
{					   
    //sector_t lba;//存放
    int dir_id;//文件，该字段表示父目录的id；目录，该字段表示当前目录的id
    int bucket_id; //-1表示目录
    int slot_id;
    struct inode vfs_inode;
	//spinlock_t i_raw_lock;/* protects updates to the raw inode */
	//struct buffer_head *i_bh;	/*i_bh contains a new or dirty disk inode.*/
};

static inline struct ffs_inode_info *FLAT_I(struct inode *inode)
{
	return container_of(inode, struct ffs_inode_info, vfs_inode);
}

/* ffs在内存superblock */
struct flatfs_sb_info
{ //一般会包含信息和数据结构，kevin的db就是在这里实现的
	cuckoo_hash_t *cuckoo;
	struct dir_entry * root;
};

static inline struct flatfs_sb_info *
FFS_SB(struct super_block *sb)
{
	return sb->s_fs_info; //文件系统特殊信息
}

struct ffs_name {
	__u8	name_len;		/* Name length */
	char	name[];			/* Dir name */
};

struct dir_entry {
    /* 目录的inode num */
    unsigned long ino;
    /* 目录名 */
    struct ffs_name *dir_name;

    /* LBA分配上，每一个字段占的位数 */
    unsigned short dir_bits;
    unsigned short bucket_bits;
    unsigned short slot_bits;
    unsigned short block_bits;

    /* 指向子目录的指针 */
    struct dir_entry **subdirs;
    /* 当前子目录数组申请的空间大小 */
    unsigned long space;
    /* 子目录的个数 */
    unsigned long dir_size;
    
};

DECLARE_BITMAP(ino_bitmap, 1 << MAX_DIR_BITS);

static inline void init_ino_bitmap() {
    bitmap_zero(ino_bitmap, 1 << MAX_DIR_BITS);
}

static inline unsigned long get_unused_ino() {
    unsigned long ino;
    ino = find_first_zero_bit(ino_bitmap, 1);
    if(ino == 1 << MAX_DIR_BITS) {
        return ENOINO;
    }
    /* 设置该ino为已经被使用 */
    bitmap_set(ino_bitmap, ino, 1);
    /* root的inode num设定为1， 那么其他目录的ino从2开始编号 */
    ino += 2;
    return ino;
}

extern unsigned long calculate_slba(struct inode* dir, struct dentry* dentry);
unsigned long flatfs_inode_by_name(struct inode *dir, struct dentry *dentry, int* is_dir);



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
};


/*
 * NOTE! unlike strncmp, ffs_match returns 1 for success, 0 for failure.
 *
*/
static inline ffs_match(int len, const char * name,
					struct ffs_name * dn)
{
    if (len != dn->name_len)
		return 0;
	return !memcmp(name, dn->name, len);
}