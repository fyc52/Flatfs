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
#endif



int ffs_get_block_prep(struct inode *inode, sector_t iblock,
			   struct buffer_head *bh, int create)
{
	int ret = 0;
 	sector_t pblk;
	bool new = false, boundary = false;

	if(iblock >= (inode->i_blocks >> (FFS_BLOCK_SIZE_BITS - 9))) {
		new = true;
		pblk = hashfs_set_data_lba(inode, iblock + 1);
		inode->i_blocks = iblock << (FFS_BLOCK_SIZE_BITS - 9);
	}
	else {
		pblk = hashfs_get_data_lba(inode->i_sb, inode->i_ino, iblock + 1);
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
	//todo : 可以在这里实现inode-inlined data
	//("write begin\n");

	ret = block_write_begin(mapping, pos, len, flags, pagep, ffs_get_block_prep);

	/* update ffs_inode size */
	// loff_t end = pos + len;
	// if (end >= mapping->host->i_blocks) {
	// 	mapping->host->i_blocks = end;
	// }
	
	return ret;
}

// int ffs_writepages(struct address_space *mapping,
// 		       struct writeback_control *wbc)
// {
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
//	.mmap           = generic_file_mmap,
	.fsync			= ffs_fsync,
	.llseek         = generic_file_llseek,
};