
// #include <malloc.h>
#include <linux/string.h>
// #include <assert.h>
#ifndef _TEST_H_
#define _TEST_H_
#include "flatfs_d.h"
#endif

static int test_count = 10;
/*  根据相关参数，创建一个新的目录项   */
unsigned long fill_one_dir_entry(struct flatfs_sb_info *sb_i, char *dir_name)
{
    struct dir_tree *dtree = &(sb_i->dtree_root);
    unsigned long ino = get_unused_ino(dtree->ino_bitmap);
    struct dir_entry *de = &(dtree->de[ino]);

    de->dir_size = 0;
    de->subdirs = kmalloc(sizeof(struct dir_list), GFP_NOIO);
    de->subdirs->head = de->subdirs->tail = NULL;

    de->namelen = strlen(dir_name);
    if(de->namelen > 0) {
        memcpy(de->dir_name, dir_name, de->namelen);
    }

    return ino;
}


/* 创建文件系统时调用，初始化目录树结构 */
void init_dir_tree(struct flatfs_sb_info *sb_i)
{
    struct dir_tree *dtree = &(sb_i->dtree_root);
    dtree = kmalloc(sizeof(struct dir_tree), GFP_NOIO);
    dtree->dir_entry_num = 0;
    init_ino_bitmap(dtree->ino_bitmap);
    unsigned long ino = get_unused_ino(dtree->ino_bitmap);

    struct dir_entry *de;
    for(ino = 0; ino < (1 << MAX_DIR_BITS); ino++) {
        de = &(dtree->de[ino]);
        de->ino = ino;
        de->dir_size = 0;
        de->subdirs = kmalloc(sizeof(struct dir_list), GFP_NOIO);
        de->subdirs->head = de->subdirs->tail = NULL;
    }  
}


void insert_dir(struct flatfs_sb_info *sb_i, unsigned long ino, struct dir_entry *inserted_dir) 
{
    struct dir_entry *dir = &(sb_i->dtree_root.de[ino]);
    struct dir_list_entry *dle = kmalloc(sizeof(struct dir_list_entry), GFP_NOIO);
    dle->de = inserted_dir;
    dle->last = dle->next = NULL;

    if(dir->dir_size == 0) {
        dir->subdirs->head = dir->subdirs->tail = dle;
    }
    else {
        dir->subdirs->tail->next = dle;
        dle->last = dir->subdirs->tail;
        dir->subdirs->tail = dle;
    }

    dir->dir_size++;
    sb_i->dtree_root.dir_entry_num++;
}


/* 递归释放所有目录结点 */
static inline void clear_dir_entry(struct dir_entry *dir)
{
    struct dir_list_entry *p;
    struct dir_list_entry *dle;
    for(dle = dir->subdirs->head; dle != NULL; dle = dle->next) {
        clear_dir_entry(dle->de);
        p = dle;
        kfree(p);
    }
    dir->subdirs->head = dir->subdirs->tail = NULL;
    dir->namelen = 0;
    dir->dir_size = 0;
}


void delete_dir(struct flatfs_sb_info *sb_i, unsigned long ino, struct qstr *child)
{
    struct dir_entry *dir = &(sb_i->dtree_root.de[ino]);
    struct dir_list_entry *dle;
    const char *name = child->name;
	int namelen = child->len;
    int start = 0;

    for(dle = dir->subdirs->head; start < dir->dir_size && dle != NULL; 1) {
        if(namelen == dle->de->namelen && !memcmp(name, dle->de->dir_name, namelen)) {
            start++;
            dle = dle->next;
            continue;
        }
        else {
            struct dir_list_entry *pre = dle->last;
            struct dir_list_entry *suf = dle->next;
            if(dle == dir->subdirs->head) {  
                dir->subdirs->head = suf; 
            } else {
                pre->next = suf;
            }

            if(dle == dir->subdirs->tail) {
                dir->subdirs->tail = pre;
            } else {
                suf->last = pre;
            }

            clear_dir_entry(dle->de);
            kfree(dle);
            break;
        }
    }
}

static inline unsigned ffs_rec_len_from_dtree(__le16 dlen)
{
	return le16_to_cpu(dlen);
}

int read_dir(struct flatfs_sb_info *sb_i, unsigned long ino, struct dir_context *ctx)
{
    if(test_count >= 0){
        printk("fyc_test, ls");
    }
    struct dir_entry *de = &(sb_i->dtree_root.de[ino]);
    struct dir_list_entry *dle = de->subdirs->head;
    int start;
    for(start = 0; start < de->dir_size; start++) {
        unsigned char d_type = DT_UNKNOWN;
        dir_emit(ctx, de->dir_name, de->namelen, le32_to_cpu(de->ino), d_type);
        __le16 dlen;
        ctx->pos += ffs_rec_len_from_dtree(dlen);
    }

    return 0;
}

void resize_dir(unsigned long dir_ino)
{
    //TODO
}


/* 卸载文件系统时调用，释放整个目录树结构 */
void dir_exit(struct dir_entry *root)
{
    clear_dir_entry(root);
}



/*
 * 通过查询dir-idx计算出目标目录或文件的ino,如果是目录且存在，则直接返回dentry对应的ino; 如果是文件，则返回文件所在目录dir的ino
 * is_dir:若dentry是目录则返回1，是文件则返回0
*/
unsigned long flatfs_inode_by_name(struct flatfs_sb_info *sb_i, unsigned long parent_ino, struct qstr *child, int* is_dir) 
{
	struct dir_entry *dir = &(sb_i->dtree_root.de[parent_ino]);
    struct dir_list_entry *dir_node;
    const char *name = child->name;
	int namelen = child->len;
    int start = 0;
    unsigned long ino = 0;
    ino = parent_ino;
    *is_dir = 0;

    for(dir_node = dir->subdirs->head; start < dir->dir_size && dir_node != NULL; ) {
        if(namelen == dir_node->de->namelen && !memcmp(name, dir_node->de->dir_name, namelen)) {
            ino = dir_node->de->ino;
            *is_dir = 1;
            break;
        }
        else {
            start++;
            dir_node = dir_node->next;
            continue;
        }
    }

    return ino;
}

