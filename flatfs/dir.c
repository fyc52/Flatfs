
// #include <malloc.h>
#include <linux/string.h>
#include <linux/module.h>
// #include <assert.h>
#ifndef _TEST_H_
#define _TEST_H_
#include "flatfs_d.h"
#include "lba.h"
#endif


/* 创建文件系统时调用，初始化目录树结构 */
void init_dir_tree(struct dir_tree **dtree)
{
    struct dir_entry *de;
    int dir_id;
    printk(KERN_INFO "init_dir_tree 1\n");

    *dtree = kmalloc(sizeof(struct dir_tree), GFP_NOIO);
    (*dtree)->dir_entry_num = 1;
    for(dir_id = 0; dir_id < (1 << MAX_DIR_BITS); dir_id++) {
        de = &((*dtree)->de[dir_id]);
        de->dir_id = dir_id;
        de->dir_size = 0;
        de->namelen = 0;
        de->dir_name[0] = '\0';
        de->subdirs = kmalloc(sizeof(struct dir_list_entry), GFP_NOIO);
        de->subdirs->head = NULL;
        de->subdirs->tail = NULL;
    } 
    printk(KERN_INFO "init_dir_tree 2\n");
    init_dir_id_bitmap((*dtree)->dir_id_bitmap);
    printk(KERN_INFO "init_dir_tree 3\n"); 
}

void init_root_entry(struct flatfs_sb_info *sb_i, struct inode * ino)
{
    sb_i->root = kmalloc(sizeof(struct dir_entry), GFP_NOIO);
    printk(KERN_INFO "init_root_entry 1\n");
    sb_i->root->dir_id = FLATFS_ROOT_INO;
    // strcpy(root->dir_name, inode_to_name(ino));
    memcpy(sb_i->root->dir_name, "/", strlen("/"));
    sb_i->root->namelen = 1;
    printk(KERN_INFO "ino2name : %s\n", sb_i->root->dir_name);
    printk(KERN_INFO "init_root_entry 2\n");
    sb_i->root = &(sb_i->dtree_root->de[FLATFS_ROOT_INO]);
    sb_i->root->subdirs = kmalloc(sizeof(struct dir_list), GFP_KERNEL);
    printk(KERN_INFO "init_root_entry 3\n");
    sb_i->root->subdirs->head = NULL;
    sb_i->root->subdirs->tail = NULL;
    sb_i->root->dir_size = i_size_read(ino);
}

/*  根据相关参数，创建一个新的目录项   */

void insert_dir(struct flatfs_sb_info *sb_i, unsigned long parent_dir_id, unsigned long insert_dir_id) 
{
    printk("insert_dir_id: %d\n", insert_dir_id);
    struct dir_entry *dir = &(sb_i->dtree_root->de[parent_dir_id]);                            // 父目录entry
    struct dir_entry *inserted_dir = &(sb_i->dtree_root->de[insert_dir_id]);                   // 插入目录的entry
    struct dir_list_entry *dle = (struct dir_list_entry *)kzalloc(sizeof(struct dir_list_entry), GFP_KERNEL);
    dle->de = inserted_dir;
    dle->last = dle->next = NULL;
    printk("inserted dir name: %s\n", inserted_dir->dir_name);

    if(dir->dir_size == 0) {
        dir->subdirs->head = dir->subdirs->tail = dle;
    }
    else {
        dir->subdirs->tail->next = dle;
        dle->last = dir->subdirs->tail;
        dir->subdirs->tail = dle;
    }
    
    dir->dir_size++;
}

unsigned long fill_one_dir_entry(struct flatfs_sb_info *sb_i, char *dir_name)
{
    struct dir_tree *dtree = sb_i->dtree_root;
    unsigned long dir_id = get_unused_dir_id(dtree->dir_id_bitmap);
    if(!dir_id) return dir_id;
    struct dir_entry *de = &(dtree->de[dir_id]);

    // de->subdirs = kmalloc(sizeof(struct dir_list), GFP_KERNEL);
    // de->subdirs->head = de->subdirs->tail = NULL;
    de->dir_size = 0;
    de->namelen = my_strlen(dir_name);
    if(de->namelen > 0) {
        printk("fill_one_dir_entry: %2s\n", dir_name);
        memcpy(de->dir_name, dir_name, de->namelen);
    }
    sb_i->dtree_root->dir_entry_num++;
    return dir_id;
}

/* 递归释放所有目录结点 */
static inline void clear_dir_entry(struct dir_entry *dir)
{
    struct dir_list_entry *p;
    struct dir_list_entry *dle;
    int dir_num;
    for(dle = dir->subdirs->head, dir_num = 0; dir_num < dir->dir_size && dle != NULL; dle = dle->next, dir_num ++) {
        clear_dir_entry(dle->de);
        p = dle;
        kfree(p);
    }
    // dir->subdirs->head = dir->subdirs->tail = NULL;
    dir->namelen = 0;
    dir->dir_size = 0;
    kfree(dir->subdirs);
}

void remove_dir(struct flatfs_sb_info *sb_i, unsigned long ino)
{
    unsigned long dir_id = inode_to_dir_id(ino);
    struct dir_entry *dir = &(sb_i->dtree_root->de[dir_id]);
    clear_dir_entry(dir);
}

void delete_dir(struct flatfs_sb_info *sb_i, unsigned long ino, struct qstr *child)
{
    struct dir_entry *dir = &(sb_i->dtree_root->de[inode_to_dir_id(ino)]);
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
    unsigned long dir_ino = (ino - 1) >> (MIN_FILE_BUCKET_BITS + FILE_SLOT_BITS);
    printk("fyc_test, ls");

    struct dir_entry *de = &(sb_i->dtree_root->de[dir_ino]);
    struct dir_list_entry *dle;
    int start;
    printk("de->dir_size = %d, dir name = %s", de->dir_size, de->dir_name);
    for(dle = de->subdirs->head, start = 0; start < de->dir_size && dle != NULL; start ++, dle = dle->next) {
        unsigned char d_type = DT_UNKNOWN;
        printk("dle->de->dir_name: %s", dle->de->dir_name);
        dir_emit(ctx, dle->de->dir_name, dle->de->namelen, le32_to_cpu(dir_id_to_inode(dle->de->dir_id)), d_type);
        __le16 dlen = 10;
        ctx->pos += ffs_rec_len_from_dtree(dlen);
    }

    return 0;
}

void resize_dir(unsigned long dir_ino)
{
    //TODO
}


/* 卸载文件系统时调用，释放整个目录树结构 */
void dir_exit(struct flatfs_sb_info *sb_i)
{
    clear_dir_entry(sb_i->root);
}



/*
 * 通过查询dir-idx计算出目标目录或文件的ino,如果是目录且存在，则直接返回dentry对应的ino; 如果是文件，则返回文件所在目录dir的ino
 * is_dir:若dentry是目录则返回1，是文件则返回0
*/
unsigned long flatfs_inode_by_name(struct flatfs_sb_info *sb_i, unsigned long parent_dir_id, struct qstr *child, int* is_dir) 
{
    parent_dir_id = inode_to_dir_id(parent_dir_id);
	struct dir_entry *dir = &(sb_i->dtree_root->de[parent_dir_id]);
    struct dir_list_entry *dir_node;
    const char *name = child->name;
    unsigned long ino = parent_dir_id;
    int namelen = child->len;
    int start;
    // *is_dir = 0;
    printk("fyc_test fsname: %s, parent_ino = %ld, name = %s, namelen = %d", sb_i->name, parent_dir_id, name, namelen);
    for(dir_node = dir->subdirs->head, start = 0; start < dir->dir_size && dir_node != NULL; start ++, dir_node = dir_node->next) {
        if(namelen == dir_node->de->namelen && !strncmp(name, dir_node->de->dir_name, namelen)) {
            ino = dir_node->de->dir_id;
            *is_dir = 1;
            break;
        }
    }
    return dir_id_to_inode(ino);
}

