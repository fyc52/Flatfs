
#include <malloc.h>
#include <string.h>
#include "flatfs_d.h"


/*
 * 通过查询dir-idx计算出目标目录或文件的ino,如果是目录且存在，则直接返回dentry对应的ino; 如果是文件，则返回文件所在目录dir的ino
 *  is_dir:若dentry是目录则返回1，是文件则返回0
*/
unsigned long flatfs_inode_by_name(struct dir_entry *dir, struct dentry *dentry, int *is_dir) 
{
	struct dir_entry *dir_node;
    const char *name = child->name;
	int namelen = child->len;
    int start = 0;
    unsigned long ino = 0;

    for(dir_node = *dir->subdirs; start < dir->dir_size; ) {
        if(ffs_match(namelen, name, dir_node->dir_name)) {
            start++;
            dir_node = *(dir->subdirs + start);
            continue;
        }
        else {
            ino = dir_node->ino;
            break;
        }
    }

    return ino;
}


void init_root_dir_entry(struct dir_entry *root, unsigned long root_ino)
{
    root = (struct dir_entry *)malloc(sizeof(struct dir_entry));
    /* init root LBA seg.*/
    root->dir_bits    = MAX_DIR_BITS;
    root->bucket_bits = MIN_FILE_BUCKET_BITS;
    root->slot_bits   = FILE_SLOT_BITS;
    root->block_bits  = DEFAULT_FILE_BLOCK_BITS;
    
    root->ino = root_ino;
    root->dir_size = 0;
    root->space = INIT_SPACE;
    root->subdirs = (struct dir_entry **)malloc(sizeof(struct dir_entry *) * INIT_SPACE);
}

void insert_dir(unsigned long dir_ino, struct dir_entry *inserted_dir) 
{
    //TODO
}

void delete_dir(unsigned long deleted_dir_ino)
{
    //TODO
}

void ls_dir(unsigned long dir_ino)
{
    //TODO
}

void resize_dir(unsigned long dir_ino)
{
    //TODO
}