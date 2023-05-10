/*
 *   fs/samplefs/file.c
 *
 *   Copyright (C) International Business Machines  Corp., 2006
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 *   Sample File System
 *
 *   Primitive example to show how to create a Linux filesystem module
 *
 *   File struct (file instance) related functions
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/buffer_head.h>
#include <linux/pagemap.h>
#include <linux/mpage.h>
#include <linux/mpage.h>
#ifndef _TEST_H_
#define _TEST_H_
#include "flatfs_d.h"
#include "lba.h"
#endif

DECLARE_BITMAP(ls_slot_bitmap, SLOT_NUM * TT_BUCKET_NUM);

int ffs_get_block_prep(struct inode *inode, sector_t iblock,
			   struct buffer_head *bh, int create)
{
	int ret = 0;
 	lba_t pblk = ffs_get_data_lba(inode, iblock);
	bool new = false, boundary = false;
	//printk("pblk: %lld\n", pblk);

	/* todo: if pblk is a new block or update */
	if((iblock << FFS_BLOCK_SIZE_BITS) > FFS_I(inode)->size) {
		new = true;
	}
	if(iblock > FILE_BLOCK_SIZE) {
		boundary = true;
	}
	/* todo: if pblk is out of field */

	map_bh(bh, inode->i_sb, pblk);//核心
	if (new)
		set_buffer_new(bh);
	if (boundary)
		set_buffer_boundary(bh);
	return ret;

}

static int ffs_write_begin(struct file *file, struct address_space *mapping,
                 loff_t pos, unsigned len, unsigned flags,
                 struct page **pagep, void **fsdata)
{	
	int ret;
	//("write begin\n");

	ret = block_write_begin(mapping, pos, len, flags, pagep, ffs_get_block_prep);

	/* update ffs_inode size */
	loff_t end = pos + len;
	if (end >= FFS_I(mapping->host)->size) {
		FFS_I(mapping->host)->size = end;
	}
	
	return ret;
}

static sector_t ffs_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping, block, ffs_get_block_prep);
}

static int ffs_writepage(struct page *page, struct writeback_control *wbc)
{
	//printk(KERN_INFO "writepage\n");
	// dump_stack();
	return block_write_full_page(page, ffs_get_block_prep, wbc);
}

static int ffs_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
	//printk(KERN_INFO "writepages\n");
	// dump_stack();
	return mpage_writepages(mapping, wbc, ffs_get_block_prep);
}


static int ffs_readpage(struct file *file, struct page *page)
{
	//printk(KERN_INFO "readpage\n");
	return mpage_readpage(page, ffs_get_block_prep);
}

static int
ffs_readpages(struct file *file, struct address_space *mapping,
		struct list_head *pages, unsigned nr_pages)
{
	//printk(KERN_INFO "readpages\n");
	return mpage_readpages(mapping, pages, nr_pages, ffs_get_block_prep);
}

static ssize_t
ffs_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	//printk(KERN_INFO "ffs_direct_IO\n");
	return blockdev_direct_IO(iocb, inode, iter, ffs_get_block_prep);
}


int ffs_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	//printk(KERN_INFO "ffs file fsync");
	return  generic_file_fsync(file, start, end, datasync);
}

struct address_space_operations ffs_aops = {// page cache访问接口,未自定义的接口会调用vfs的generic方法
	.readpages	     = ffs_readpages,
	.readpage	     = ffs_readpage,
	.write_begin	 = ffs_write_begin,
	.write_end	     = generic_write_end,
	.bmap            = ffs_bmap,
	.set_page_dirty	 = __set_page_dirty_nobuffers,
	.writepages      = ffs_writepages,
	.writepage       = ffs_writepage,
	.direct_IO       = ffs_direct_IO,
};

static unsigned long flatfs_mmu_get_unmapped_area(struct file *file,
		unsigned long addr, unsigned long len, unsigned long pgoff,
		unsigned long flags)
{
	return current->mm->get_unmapped_area(file, addr, len, pgoff, flags);
}
struct file_operations ffs_file_file_ops = {
	.read_iter		= generic_file_read_iter,
	.write_iter		= generic_file_write_iter,
	.mmap           = generic_file_mmap,
	.fsync			= ffs_fsync,
	.llseek         = generic_file_llseek,
};

static int ffs_readdir(struct file *file, struct dir_context *ctx){
	//printk(KERN_INFO "flatfs read dir");
	loff_t pos;/*文件的偏移*/
	struct inode *ino = file_inode(file);
	struct super_block *sb = ino->i_sb;
	struct ffs_ino ffs_ino;
	struct flatfs_sb_info *ffs_sb = sb->s_fs_info;
	ffs_ino.ino = ino->i_ino;
	int bkt, slt;

	if(!ctx->pos)
	{
		bitmap_fill(ls_slot_bitmap, SLOT_NUM * TT_BUCKET_NUM);
	}
	read_dir_dirs(ffs_sb, ffs_ino.ino, ctx);
	read_dir_files(ffs_sb->hashtbl[ffs_ino.dir_seg.dir], ino, ffs_ino.ino, ctx, ls_slot_bitmap);

	return 0;
}

struct file_operations ffs_dir_operations = {
	.read			= generic_read_dir,
	.iterate		= ffs_readdir,//ls
	.iterate_shared = ffs_readdir,
	//.fsync			= ffs_fsync,
	//.release		= lightfs_dir_release,
};