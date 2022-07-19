
#include <malloc.h>
#include <string.h>
#include "flatfs_d.h"
#include "block.h"


/*
 * NOTE! unlike strncmp, ffs_match returns 1 for success, 0 for failure.
 *
*/
static inline ffs_match(int len, const char * const name,
					struct ffs_dir_name * dn)
{
    if (len != dn->name_len)
		return 0;
	return !memcmp(name, dn->name, len);
}

/*
 * return 0 while *child* not found
 *  
*/
unsigned long flatfs_inode_by_name(struct dir_entry *dir, struct qstr *child) 
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