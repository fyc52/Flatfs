
#include <malloc.h>
#include <string.h>
#include <assert.h>
#include "flatfs_d.h"


/*  根据相关参数，创建一个新的目录项   */
unsigned long fill_one_dir_entry(struct flatfs_sb_info *sb_i, struct qstr *dir_name)
{
    struct dir_tree *dtree = &(sb_i -> root);
    unsigned long ino = get_unused_ino(dtree->ino_bitmap);
    struct dir_entry *de = &(dtree->de[ino]);

    assert(de->ino == ino);
    de->dir_size = 0;
    de->subdirs = (struct dir_list *) malloc(sizeof(struct dir_list));
    de->subdirs->head = root->subdirs->tail = NULL;

    de->namelen = dir_name->len;
    if(dir_name->len > 0) {
        memcpy(de->dir_name->name, dir_name->name, dir_name->len);
    }

    return ino;
}


/* 创建文件系统时调用，初始化目录树结构 */
void init_dir_tree(struct flatfs_sb_info *sb_i)
{
    struct dir_tree *dtree = &(sb_i -> root);
    dtree->dir_entry_num = 0;
    init_ino_bitmap(dtree->ino_bitmap);
    unsigned long ino = get_unused_ino(dtree->ino_bitmap);
    assert(ino == 0);

    struct dir_entry *de;
    for(ino = 0; ino < (1 << MAX_DIR_BITS); ino++) {
        de = &(dtree->de[ino]);
        de->ino = ino;
        de->dir_size = 0;
        de->subdirs = (struct dir_list *) malloc(sizeof(struct dir_list));
        de->subdirs->head = root->subdirs->tail = NULL;
    }
    
}


void insert_dir(struct flatfs_sb_info *sb_i, unsigned long ino, struct dir_entry *inserted_dir) 
{
    struct dir_entry *dir = &(sb_i->root.de[ino]);
    struct dir_list_entry *dle = (struct dir_list_entry *)malloc(struct dir_list_entry);
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
    sb_i->root.dir_entry_num++;
}


/* 递归释放所有目录结点 */
static inline void clear_dir_entry(struct dir_entry *dir)
{
    struct dir_list_entry *p;
    for(struct dir_list_entry *dle = dir->subdirs->head; dle != NULL; ) {
        clear_dir_entry(dle->de);
        p = dle;
        dle = dle->next;
        free(p);
    }
    dir->subdirs->head = dir->subdirs->tial = NULL;
    dir->namelen = 0;
    dir->dir_size = 0;
}


void delete_dir(struct flatfs_sb_info *sb_i, unsigned long ino, struct qstr *child)
{
    struct dir_entry *dir = &(sb_i->root.de[ino]);
    struct dir_list_entry *dle;
    const char *name = child->name;
	int namelen = child->len;
    int start = 0;

    for(dle = dir->subdirs->head; start < dir->dir_size && dle != NULL; ) {
        if(namelen == dle->de->namelen && !memcmp(name, dle->de->dir_name, len)) {
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
            free(dle);
            break;
        }
    }
}

int read_dir(struct flatfs_sb_info *sb_i, unsigned long ino, struct dir_context *ctx)
{
    struct dir_entry *de = &(sb_i->root.de[ino]);
    struct dir_list_entry *dle = de->subdirs->head;

    for(int start = 0; start < de->dir_size; start++) {
        dir_emit(ctx, de->dir_name, de->namelen, le32_to_cpu(de->ino), d_type);
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
    free_dir_entry(root);
}



/*
 * 通过查询dir-idx计算出目标目录或文件的ino,如果是目录且存在，则直接返回dentry对应的ino; 如果是文件，则返回文件所在目录dir的ino
 * is_dir:若dentry是目录则返回1，是文件则返回0
*/
unsigned long flatfs_inode_by_name(struct flatfs_sb_info *sb_i, unsigned long parent_ino, struct qstr *child, int* is_dir) 
{
	struct dir_entry *dir = &(sb_i->root.de[ino]);
    struct dir_list_entry *dir_node;
    const char *name = child->name;
	int namelen = child->len;
    int start = 0;
    unsigned long ino = 0;
    ino = parent_ino;
    *is_dir = 0;

    for(dir_node = dir->subdirs->head; start < dir->dir_size && dir_node != NULL; ) {
        if(namelen == dle->de->namelen && !memcmp(name, dle->de->dir_name, len)) {
            ino = dir_node->de->i_ino;
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

