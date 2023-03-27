
// #include <malloc.h>
#include <linux/string.h>
#include <linux/module.h>
// #include <assert.h>
#ifndef _TEST_H_
#define _TEST_H_
#include "flatfs_d.h"
#endif
#include <linux/buffer_head.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/iversion.h>

typedef struct hashfs_dir_entry_2 hashfs_dirent;


void hashfs_error(struct super_block *sb, const char *function,
		const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	printk(KERN_CRIT "HASHFS-fs (%s): error: %s: %pV\n",
	       sb->s_id, function, &vaf);

	va_end(args);
}

/*
 * Tests against MAX_REC_LEN etc were put in place for 64k block
 * sizes; if that is not possible on this arch, we can skip
 * those tests and speed things up.
 */
static inline unsigned hashfs_rec_len_from_disk(__le16 dlen)
{
	unsigned len = le16_to_cpu(dlen);

#if (PAGE_SIZE >= 65536)
	if (len == HASHFS_MAX_REC_LEN)
		return 1 << 16;
#endif
	return len;
}

static inline __le16 hashfs_rec_len_to_disk(unsigned len)
{
#if (PAGE_SIZE >= 65536)
	if (len == (1 << 16))
		return cpu_to_le16(HASHFS_MAX_REC_LEN);
	else
		BUG_ON(len > (1 << 16));
#endif
	return cpu_to_le16(len);
}

/*
 * hashfs uses block-sized chunks. Arguably, sector-sized ones would be
 * more robust, but we have what we have
 */
static inline unsigned hashfs_chunk_size(struct inode *inode)
{
	return inode->i_sb->s_blocksize;
}

static inline void hashfs_put_page(struct page *page)
{
	kunmap(page);
	put_page(page);
}

/*
 * Return the offset into page `page_nr' of the last valid
 * byte in that page, plus one.
 */
static unsigned
hashfs_last_byte(struct inode *inode, unsigned long page_nr)
{
	unsigned last_byte = inode->i_size;

	last_byte -= page_nr << PAGE_SHIFT;
	if (last_byte > PAGE_SIZE)
		last_byte = PAGE_SIZE;
	return last_byte;
}

static int hashfs_commit_chunk(struct page *page, loff_t pos, unsigned len)
{
	struct address_space *mapping = page->mapping;
	struct inode *dir = mapping->host;
	int err = 0;

	inode_inc_iversion(dir);
	block_write_end(NULL, mapping, pos, len, len, page, NULL);

	if (pos+len > dir->i_size) {
		i_size_write(dir, pos+len);
		mark_inode_dirty(dir);
	}

	if (IS_DIRSYNC(dir)) {
		err = write_one_page(page);
		if (!err)
			err = sync_inode_metadata(dir, 1);
	} else {
		unlock_page(page);
	}

	return err;
}

static bool hashfs_check_page(struct page *page, int quiet)
{
	struct inode *dir = page->mapping->host;
	struct super_block *sb = dir->i_sb;
	unsigned chunk_size = hashfs_chunk_size(dir);
	char *kaddr = page_address(page);
	u32 max_inumber = MAX_INODE_NUM;
	unsigned offs, rec_len;
	unsigned limit = PAGE_SIZE;
	hashfs_dirent *p;
	char *error;

	if ((dir->i_size >> PAGE_SHIFT) == page->index) {
		limit = dir->i_size & ~PAGE_MASK;
		if (limit & (chunk_size - 1))
			goto Ebadsize;
		if (!limit)
			goto out;
	}
	for (offs = 0; offs <= limit - HASHFS_DIR_REC_LEN(1); offs += rec_len) {
		p = (hashfs_dirent *)(kaddr + offs);
		rec_len = hashfs_rec_len_from_disk(p->rec_len);

		if (unlikely(rec_len < HASHFS_DIR_REC_LEN(1)))
			goto Eshort;
		if (unlikely(rec_len & 3))
			goto Ealign;
		if (unlikely(rec_len < HASHFS_DIR_REC_LEN(p->name_len)))
			goto Enamelen;
		if (unlikely(((offs + rec_len - 1) ^ offs) & ~(chunk_size-1)))
			goto Espan;
		if (unlikely(le32_to_cpu(p->inode) > max_inumber))
			goto Einumber;
	}
	if (offs != limit)
		goto Eend;
out:
	SetPageChecked(page);
	return true;

	/* Too bad, we had an error */

Ebadsize:
	if (!quiet)
		hashfs_error(sb, __func__,
			"size of directory #%lu is not a multiple "
			"of chunk size", dir->i_ino);
	goto fail;
Eshort:
	error = "rec_len is smaller than minimal";
	goto bad_entry;
Ealign:
	error = "unaligned directory entry";
	goto bad_entry;
Enamelen:
	error = "rec_len is too small for name_len";
	goto bad_entry;
Espan:
	error = "directory entry across blocks";
	goto bad_entry;
Einumber:
	error = "inode out of bounds";
bad_entry:
	if (!quiet)
		hashfs_error(sb, __func__, "bad entry in directory #%lu: : %s - "
			"offset=%lu, inode=%lu, rec_len=%d, name_len=%d",
			dir->i_ino, error, (page->index<<PAGE_SHIFT)+offs,
			(unsigned long) le32_to_cpu(p->inode),
			rec_len, p->name_len);
	goto fail;
Eend:
	if (!quiet) {
		p = (hashfs_dirent *)(kaddr + offs);
		hashfs_error(sb, "hashfs_check_page",
			"entry in directory #%lu spans the page boundary"
			"offset=%lu, inode=%lu",
			dir->i_ino, (page->index<<PAGE_SHIFT)+offs,
			(unsigned long) le32_to_cpu(p->inode));
	}
fail:
	SetPageError(page);
	return false;
}

static struct page * hashfs_get_page(struct inode *dir, unsigned long n,
				   int quiet)
{
	struct address_space *mapping = dir->i_mapping;
	struct page *page = read_mapping_page(mapping, n, NULL);
	if (!IS_ERR(page)) {
		kmap(page);
		if (unlikely(!PageChecked(page))) {
			if (PageError(page) || !hashfs_check_page(page, quiet))
				goto fail;
		}
	}
	return page;

fail:
	hashfs_put_page(page);
	return ERR_PTR(-EIO);
}

/*
 * NOTE! unlike strncmp, hashfs_match returns 1 for success, 0 for failure.
 *
 * len <= HASHFS_NAME_LEN and de != NULL are guaranteed by caller.
 */
static inline int hashfs_match (int len, const char * const name,
					struct hashfs_dir_entry_2 * de)
{
	if (len != de->name_len)
		return 0;
	if (!de->inode)
		return 0;
	return !memcmp(name, de->name, len);
}

/*
 * p is at least 6 bytes before the end of page
 */
static inline hashfs_dirent *hashfs_next_entry(hashfs_dirent *p)
{
	return (hashfs_dirent *)((char *)p +
			hashfs_rec_len_from_disk(p->rec_len));
}

static inline unsigned 
hashfs_validate_entry(char *base, unsigned offset, unsigned mask)
{
	hashfs_dirent *de = (hashfs_dirent*)(base + offset);
	hashfs_dirent *p = (hashfs_dirent*)(base + (offset&mask));
	while ((char*)p < (char*)de) {
		if (p->rec_len == 0)
			break;
		p = hashfs_next_entry(p);
	}
	return (char *)p - base;
}

static int
hashfs_readdir(struct file *file, struct dir_context *ctx)
{
	loff_t pos = ctx->pos;
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;
	unsigned int offset = pos & ~PAGE_MASK;
	unsigned long n = pos >> PAGE_SHIFT;
	unsigned long npages = dir_pages(inode);
	unsigned chunk_mask = ~(hashfs_chunk_size(inode)-1);
	bool need_revalidate = !inode_eq_iversion(inode, file->f_version);

	if (pos > inode->i_size - HASHFS_DIR_REC_LEN(1))
		return 0;

	//printk("readdir: %lu\n", npages);

	for ( ; n < npages; n++, offset = 0) {
		char *kaddr, *limit;
		hashfs_dirent *de;
		struct page *page = hashfs_get_page(inode, n, 0);

		if (IS_ERR(page)) {
			hashfs_error(sb, __func__,
				   "bad page in #%lu",
				   inode->i_ino);
			ctx->pos += PAGE_SIZE - offset;
			return PTR_ERR(page);
		}
		kaddr = page_address(page);
		if (unlikely(need_revalidate)) {
			if (offset) {
				offset = hashfs_validate_entry(kaddr, offset, chunk_mask);
				ctx->pos = (n<<PAGE_SHIFT) + offset;
			}
			file->f_version = inode_query_iversion(inode);
			need_revalidate = false;
		}
		de = (hashfs_dirent *)(kaddr+offset);
		limit = kaddr + hashfs_last_byte(inode, n) - HASHFS_DIR_REC_LEN(1);
		for ( ;(char*)de <= limit; de = hashfs_next_entry(de)) {
			if (de->rec_len == 0) {
				hashfs_error(sb, __func__,
					"zero-length directory entry");
				hashfs_put_page(page);
				return -EIO;
			}
			if (de->inode) {
				unsigned char d_type = DT_UNKNOWN;


				if (!dir_emit(ctx, de->name, de->name_len,
						le32_to_cpu(de->inode),
						d_type)) {
					hashfs_put_page(page);
					//printk("de->name: %s\n", de->name);
					return 0;
				}
			}
			ctx->pos += hashfs_rec_len_from_disk(de->rec_len);
		}
		hashfs_put_page(page);
	}
	return 0;
}

/*
 *	hashfs_find_entry()
 *
 * finds an entry in the specified directory with the wanted name. It
 * returns the page in which the entry was found (as a parameter - res_page),
 * and the entry itself. Page is returned mapped and unlocked.
 * Entry is guaranteed to be valid.
 */
struct hashfs_dir_entry_2 *hashfs_find_entry (struct inode *dir,
			const struct qstr *child, struct page **res_page)
{
	const char *name = child->name;
	int namelen = child->len;
	unsigned reclen = HASHFS_DIR_REC_LEN(namelen);
	unsigned long start, n;
	unsigned long npages = dir_pages(dir);
	struct page *page = NULL;
	struct hashfs_inode_info *ei = HASHFS_I(dir);
	hashfs_dirent * de;
	int dir_has_error = 0;

	if (npages == 0)
		goto out;

	/* OFFSET_CACHE */
	*res_page = NULL;

	start = ei->i_dir_start_lookup;
	if (start >= npages)
		start = 0;
	n = start;
	do {
		char *kaddr;
		page = hashfs_get_page(dir, n, dir_has_error);
		if (!IS_ERR(page)) {
			kaddr = page_address(page);
			de = (hashfs_dirent *) kaddr;
			kaddr += hashfs_last_byte(dir, n) - reclen;
			while ((char *) de <= kaddr) {
				if (de->rec_len == 0) {
					hashfs_error(dir->i_sb, __func__,
						"zero-length directory entry");
					hashfs_put_page(page);
					goto out;
				}
				if (hashfs_match (namelen, name, de))
					goto found;
				de = hashfs_next_entry(de);
			}
			hashfs_put_page(page);
		} else
			dir_has_error = 1;

		if (++n >= npages)
			n = 0;
		/* next page is past the blocks we've got */
		if (unlikely(n > (dir->i_blocks >> (PAGE_SHIFT - 9)))) {
			hashfs_error(dir->i_sb, __func__,
				"dir %lu size %lld exceeds block count %llu",
				dir->i_ino, dir->i_size,
				(unsigned long long)dir->i_blocks);
			goto out;
		}
	} while (n != start);
out:
	return NULL;

found:
	*res_page = page;
	ei->i_dir_start_lookup = n;
	return de;
}

struct hashfs_dir_entry_2 * hashfs_dotdot (struct inode *dir, struct page **p)
{
	struct page *page = hashfs_get_page(dir, 0, 0);
	hashfs_dirent *de = NULL;

	if (!IS_ERR(page)) {
		de = hashfs_next_entry((hashfs_dirent *) page_address(page));
		*p = page;
	}
	return de;
}

ino_t hashfs_inode_by_name(struct inode *dir, const struct qstr *child)
{
	ino_t res = 0;
	struct hashfs_dir_entry_2 *de;
	struct page *page;
	
	de = hashfs_find_entry (dir, child, &page);
	if (de) {
		res = le32_to_cpu(de->inode);
		hashfs_put_page(page);
	}
	return res;
}

static int hashfs_prepare_chunk(struct page *page, loff_t pos, unsigned len)
{
	return __block_write_begin(page, pos, len, ffs_get_block_prep);
}

/*
 *	Parent is locked.
 */
int hashfs_add_link (struct dentry *dentry, struct inode *inode)
{
	struct inode *dir = d_inode(dentry->d_parent);
	const char *name = dentry->d_name.name;
	int namelen = dentry->d_name.len;
	unsigned chunk_size = hashfs_chunk_size(dir);
	unsigned reclen = HASHFS_DIR_REC_LEN(namelen);
	unsigned short rec_len, name_len;
	struct page *page = NULL;
	hashfs_dirent * de;
	unsigned long npages = dir_pages(dir);
	unsigned long n;
	char *kaddr;
	loff_t pos;
	int err;

	/*
	 * We take care of directory expansion in the same loop.
	 * This code plays outside i_size, so it locks the page
	 * to protect that region.
	 */
	for (n = 0; n <= npages; n++) {
		char *dir_end;

		page = hashfs_get_page(dir, n, 0);
		err = PTR_ERR(page);
		if (IS_ERR(page))
			goto out;
		lock_page(page);
		kaddr = page_address(page);
		dir_end = kaddr + hashfs_last_byte(dir, n);
		de = (hashfs_dirent *)kaddr;
		kaddr += PAGE_SIZE - reclen;
		while ((char *)de <= kaddr) {
			if ((char *)de == dir_end) {
				/* We hit i_size */
				name_len = 0;
				rec_len = chunk_size;
				de->rec_len = hashfs_rec_len_to_disk(chunk_size);
				de->inode = 0;
				goto got_it;
			}
			if (de->rec_len == 0) {
				hashfs_error(dir->i_sb, __func__,
					"zero-length directory entry");
				err = -EIO;
				goto out_unlock;
			}
			err = -EEXIST;
			if (hashfs_match (namelen, name, de))
				goto out_unlock;
			name_len = HASHFS_DIR_REC_LEN(de->name_len);
			rec_len = hashfs_rec_len_from_disk(de->rec_len);
			if (!de->inode && rec_len >= reclen)
				goto got_it;
			if (rec_len >= name_len + reclen)
				goto got_it;
			de = (hashfs_dirent *) ((char *) de + rec_len);
		}
		unlock_page(page);
		hashfs_put_page(page);
	}
	BUG();
	return -EINVAL;

got_it:
	pos = page_offset(page) +
		(char*)de - (char*)page_address(page);
	err = hashfs_prepare_chunk(page, pos, rec_len);
	if (err)
		goto out_unlock;
	if (de->inode) {
		hashfs_dirent *de1 = (hashfs_dirent *) ((char *) de + name_len);
		de1->rec_len = hashfs_rec_len_to_disk(rec_len - name_len);
		de->rec_len = hashfs_rec_len_to_disk(name_len);
		de = de1;
	}
	de->name_len = namelen;
	memcpy(de->name, name, namelen);
	//printk("mkdir: %s, %d\n", de->name, de->name_len);
	de->inode = cpu_to_le32(inode->i_ino);
	err = hashfs_commit_chunk(page, pos, rec_len);
	dir->i_mtime = dir->i_ctime = current_time(dir);
	dir->i_flags &= ~HASHFS_BTREE_FL;
	mark_inode_dirty(dir);
	/* OFFSET_CACHE */
out_put:
	hashfs_put_page(page);
out:
	return err;
out_unlock:
	unlock_page(page);
	goto out_put;
}

/*
 * hashfs_delete_entry deletes a directory entry by merging it with the
 * previous entry. Page is up-to-date. Releases the page.
 */
int hashfs_delete_entry (struct hashfs_dir_entry_2 * dir, struct page * page )
{
	struct inode *inode = page->mapping->host;
	char *kaddr = page_address(page);
	unsigned from = ((char*)dir - kaddr) & ~(hashfs_chunk_size(inode)-1);
	unsigned to = ((char *)dir - kaddr) +
				hashfs_rec_len_from_disk(dir->rec_len);
	loff_t pos;
	hashfs_dirent * pde = NULL;
	hashfs_dirent * de = (hashfs_dirent *) (kaddr + from);
	int err;

	while ((char*)de < (char*)dir) {
		if (de->rec_len == 0) {
			hashfs_error(inode->i_sb, __func__,
				"zero-length directory entry");
			err = -EIO;
			goto out;
		}
		pde = de;
		de = hashfs_next_entry(de);
	}
	if (pde)
		from = (char*)pde - (char*)page_address(page);
	pos = page_offset(page) + from;
	lock_page(page);
	err = hashfs_prepare_chunk(page, pos, to - from);
	BUG_ON(err);
	if (pde)
		pde->rec_len = hashfs_rec_len_to_disk(to - from);
	dir->inode = 0;
	err = hashfs_commit_chunk(page, pos, to - from);
	inode->i_ctime = inode->i_mtime = current_time(inode);
	inode->i_flags &= ~HASHFS_BTREE_FL;
	mark_inode_dirty(inode);
out:
	hashfs_put_page(page);
	return err;
}

/*
 * Set the first fragment of directory.
 */
int hashfs_make_empty(struct inode *inode, struct inode *parent)
{
	struct page *page = grab_cache_page(inode->i_mapping, 0);
	unsigned chunk_size = hashfs_chunk_size(inode);
	struct hashfs_dir_entry_2 * de;
	int err;
	void *kaddr;

	if (!page)
		return -ENOMEM;

	err = hashfs_prepare_chunk(page, 0, chunk_size);
	if (err) {
		unlock_page(page);
		goto fail;
	}
	kaddr = kmap_atomic(page);
	memset(kaddr, 0, chunk_size);
	de = (struct hashfs_dir_entry_2 *)kaddr;
	de->name_len = 1;
	de->rec_len = hashfs_rec_len_to_disk(HASHFS_DIR_REC_LEN(1));
	memcpy (de->name, ".\0\0", 4);
	de->inode = cpu_to_le32(inode->i_ino);

	de = (struct hashfs_dir_entry_2 *)(kaddr + HASHFS_DIR_REC_LEN(1));
	de->name_len = 2;
	de->rec_len = hashfs_rec_len_to_disk(chunk_size - HASHFS_DIR_REC_LEN(1));
	de->inode = cpu_to_le32(parent->i_ino);
	memcpy (de->name, "..\0", 4);
	kunmap_atomic(kaddr);
	err = hashfs_commit_chunk(page, 0, chunk_size);
fail:
	put_page(page);
	return err;
}

/*
 * routine to check that the specified directory is empty (for rmdir)
 */
int hashfs_empty_dir (struct inode * inode)
{
	struct page *page = NULL;
	unsigned long i, npages = dir_pages(inode);
	int dir_has_error = 0;

	for (i = 0; i < npages; i++) {
		char *kaddr;
		hashfs_dirent * de;
		page = hashfs_get_page(inode, i, dir_has_error);

		if (IS_ERR(page)) {
			dir_has_error = 1;
			continue;
		}

		kaddr = page_address(page);
		de = (hashfs_dirent *)kaddr;
		kaddr += hashfs_last_byte(inode, i) - HASHFS_DIR_REC_LEN(1);

		while ((char *)de <= kaddr) {
			if (de->rec_len == 0) {
				hashfs_error(inode->i_sb, __func__,
					"zero-length directory entry");
				printk("kaddr=%p, de=%p\n", kaddr, de);
				goto not_empty;
			}
			if (de->inode != 0) {
				/* check for . and .. */
				if (de->name[0] != '.')
					goto not_empty;
				if (de->name_len > 2)
					goto not_empty;
				if (de->name_len < 2) {
					if (de->inode !=
					    cpu_to_le32(inode->i_ino))
						goto not_empty;
				} else if (de->name[1] != '.')
					goto not_empty;
			}
			de = hashfs_next_entry(de);
		}
		hashfs_put_page(page);
	}
	return 1;

not_empty:
	hashfs_put_page(page);
	return 0;
}

int hashfs_add_nondir(struct dentry *dentry, struct inode *inode)
{
	int err = hashfs_add_link(dentry, inode);
	if (!err) {
		//printk("add dentry\n");
		d_instantiate_new(dentry, inode);
		return 0;
	}
	printk("add dentry error\n");
	inode_dec_link_count(inode);
	discard_new_inode(inode);
	return err;
}

struct file_operations ffs_dir_operations = {
	.llseek		= generic_file_llseek,
	.read			= generic_read_dir,
	.iterate		= hashfs_readdir,
	.iterate_shared = hashfs_readdir,
};
