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

int ext4_da_get_block_prep(struct inode *inode, sector_t iblock,
			   struct buffer_head *bh, int create)
{
	

}

static int ffs_write_begin(struct file *file, struct address_space *mapping,
                 loff_t pos, unsigned len, unsigned flags,
                 struct page **pagep, void **fsdata)
{	int ret, retries = 0;
	struct page *page;
	pgoff_t index;
	struct inode *inode = mapping->host;

	index = pos >> PAGE_SHIFT;
	*fsdata = (void *)0;
	//todo : 可以在这里实现inode-inlined data

retry_grab:
	page = grab_cache_page_write_begin(mapping, index, flags);
	if (!page) {
		ret = -ENOMEM;
	}
	lock_page(page);
	if (page->mapping != mapping) {
		/* The page got truncated from under us */
		unlock_page(page);
		put_page(page);
		goto retry_grab;
	}
	/* In case writeback began while the page was unlocked */
	/**
 	* wait_for_stable_page() - wait for writeback to finish, if necessary.
	 * page:	The page to wait on.
	 *
	 * This function determines if the given page is related to a backing device
	 * that requires page contents to be held stable during writeback.  If so, then
	 * it will wait for any pending writeback to complete.
	 */
	wait_for_stable_page(page);

	ret = __block_write_begin(page, pos, len, fss_get_block_prep);
	*pagep = page;
	return 0;

}




struct address_space_operations ffs_aops = {// page cache访问接口,未自定义的接口会调用vfs的generic方法
	.readpage	= simple_readpage,
	.write_begin	= simple_write_begin,
	.write_end	= simple_write_end,
	.set_page_dirty	= __set_page_dirty_nobuffers,
	//.writepages = ffs_writepage,
	//.writepage = ffs_writepage,
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
	.fsync			= noop_fsync,
	.llseek         = generic_file_llseek,
};

static int ffs_readdir(struct file *file, struct dir_context *ctx){
	printk(KERN_INFO "flatfs read dir");
	return 0;
}

struct file_operations ffs_dir_operations = {
	.read			= generic_read_dir,
	//.iterate		= ffs_readdir,//ls
	//.fsync		= lightfs_fsync,
	//.release		= lightfs_dir_release,
};