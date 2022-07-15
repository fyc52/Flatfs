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


//常规文件data的lba；inode左移64位;
sector_t ffs_get_lba(struct inode *inode, sector_t iblock){
	//to do：
	sector_t base = inode->i_ino;
	sector_t lba = iblock + base;

	return lba;
}

int ffs_get_block_prep(struct inode *inode, sector_t iblock,
			   struct buffer_head *bh, int create)
{
	int ret = 0;

	BUG_ON(create == 0);
	BUG_ON(bh->b_size != inode->i_sb->s_blocksize);

 	sector_t pblk = ffs_get_lba(inode, iblock);
	printk("pblk: %d\n", pblk);

	map_bh(bh, inode->i_sb, pblk);//核心
	
	if (buffer_unwritten(bh)) {  //设置bh为mapped 和 new
		/* A delayed write to unwritten bh should be marked
		 * new and mapped.  Mapped ensures that we don't do
		 * get_block multiple times when we write to the same
		 * offset and new ensures that we do proper zero out
		 * for partial write.
		 */
		set_buffer_new(bh);
		set_buffer_mapped(bh);
	}
	return 0;

}

static int ffs_write_begin(struct file *file, struct address_space *mapping,
                 loff_t pos, unsigned len, unsigned flags,
                 struct page **pagep, void **fsdata)
{	int ret;
	//todo : 可以在这里实现inode-inlined data
	printk("write begin\n");

	ret = block_write_begin(mapping, pos, len, flags, pagep, ffs_get_block_prep);
	
	return ret;
}

// int ffs_writepages(struct address_space *mapping,
// 		       struct writeback_control *wbc)
// {
static int ffs_writepage(struct page *page, struct writeback_control *wbc)
{
	printk(KERN_INFO "writepage\n");
	return block_write_full_page(page, ffs_get_block_prep, wbc);
}

static int ffs_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
	printk(KERN_INFO "writepages\n");
	return mpage_writepages(mapping, wbc, ffs_get_block_prep);
}


static int ffs_readpage(struct file *file, struct page *page)
{
	printk(KERN_INFO "readpage\n");
	return mpage_readpage(page, ffs_get_block_prep);
}

static int
ffs_readpages(struct file *file, struct address_space *mapping,
		struct list_head *pages, unsigned nr_pages)
{
	printk(KERN_INFO "readpages\n");
	return mpage_readpages(mapping, pages, nr_pages, ffs_get_block_prep);
}

static ssize_t
ffs_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	printk(KERN_INFO "ffs_direct_IO\n");
	return blockdev_direct_IO(iocb, inode, iter, ffs_get_block_prep);
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
	// .fsync			= noop_fsync,
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