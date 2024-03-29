/*
 * Copyright (c) 2022 Institute of Parallel And Distributed Systems (IPADS)
 * ChCore-Lab is licensed under the Mulan PSL v1.
 * You can use this software according to the terms and conditions of the Mulan PSL v1.
 * You may obtain a copy of Mulan PSL v1 at:
 *     http://license.coscl.org.cn/MulanPSL
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v1 for more details.
 */

#include "cpio.h"
#include <chcore/fs/defs.h>
#include <chcore/ipc.h>
#include <chcore/memory.h>
#include <chcore/assert.h>
#include <stdio.h>

#include "../fs_base/fs_wrapper_defs.h"
#include "../fs_base/fs_vnode.h"
#include "tmpfs.h"
#include "tmpfs_ops.h"

struct inode *tmpfs_root = NULL;
struct dentry *tmpfs_root_dent = NULL;
struct id_manager fidman;
struct fid_record fid_records[MAX_NR_FID_RECORDS];
struct server_entry *server_entrys[MAX_SERVER_ENTRY_NUM];
struct list_head fs_vnode_list;
bool mounted;

/*
 * Helper functions to calucate hash value of string
 */
static inline u64 hash_chars(const char *str, ssize_t len)
{
	u64 seed = 131;		/* 31 131 1313 13131 131313 etc.. */
	u64 hash = 0;
	int i;

	if (len < 0) {
		while (*str) {
			hash = (hash * seed) + *str;
			str++;
		}
	} else {
		for (i = 0; i < len; ++i)
			hash = (hash * seed) + str[i];
	}

	return hash;
}

/* BKDR hash */
static inline u64 hash_string(struct string *s)
{
	return (s->hash = hash_chars(s->str, s->len));
}

static inline int init_string(struct string *s, const char *name, size_t len)
{
	int i;

	s->str = malloc(len + 1);
	if (!s->str)
		return -ENOMEM;
	s->len = len;

	for (i = 0; i < len; ++i)
		s->str[i] = name[i];
	s->str[len] = '\0';

	hash_string(s);
	return 0;
}

/*
 *  Helper functions to create instances of key structures
 */
static inline struct inode *new_inode(void)
{
	struct inode *inode = malloc(sizeof(*inode));

	if (!inode)
		return ERR_PTR(-ENOMEM);

	inode->type = 0;
	inode->size = 0;

	return inode;
}

struct inode *new_dir(void)
{
	struct inode *inode;

	inode = new_inode();
	if (IS_ERR(inode))
		return inode;
	inode->type = FS_DIR;
	init_htable(&inode->dentries, 1024);

	return inode;
}

static struct inode *new_reg(void)
{
	struct inode *inode;

	inode = new_inode();
	if (IS_ERR(inode))
		return inode;
	inode->type = FS_REG;
	init_radix_w_deleter(&inode->data, free);

	return inode;
}

struct dentry *new_dent(struct inode *inode, const char *name,
			       size_t len)
{
	struct dentry *dent;
	int err;

	dent = malloc(sizeof(*dent));
	if (!dent)
		return ERR_PTR(-ENOMEM);
	err = init_string(&dent->name, name, len);
	if (err) {
		free(dent);
		return ERR_PTR(err);
	}
	dent->inode = inode;

	return dent;
}

// this function create a file (directory if `mkdir` == true, otherwise regular
// file) and its size is `len`. You should create an inode and corresponding 
// dentry, then add dentey to `dir`'s htable by `htable_add`.
// Assume that no separator ('/') in `name`.
static int tfs_mknod(struct inode *dir, const char *name, size_t len, int mkdir)
{
	struct inode *inode;
	struct dentry *dent;

	BUG_ON(!name);

	if (len == 0) {
		WARN("mknod with len of 0");
		return -ENOENT;
	}
	/* LAB 5 TODO BEGIN */
        inode = mkdir ? new_dir() : new_reg();
        inode->size = len;
        dent = new_dent(inode, name, strlen(name));
        htable_add(&(dir->dentries),dent->name.hash,&(dent->node));
	/* LAB 5 TODO END */

	return 0;
}

int tfs_mkdir(struct inode *dir, const char *name, size_t len)
{
	return tfs_mknod(dir, name, len, 1 /* mkdir */ );
}

int tfs_creat(struct inode *dir, const char *name, size_t len)
{
	return tfs_mknod(dir, name, len, 0 /* mkdir */ );
}

// look up a file called `name` under the inode `dir` 
// and return the dentry of this file
struct dentry *tfs_lookup(struct inode *dir, const char *name,
				 size_t len)
{
	u64 hash = hash_chars(name, len);
	struct dentry *dent;
	struct hlist_head *head;

	head = htable_get_bucket(&dir->dentries, (u32) hash);

	for_each_in_hlist(dent, node, head) {
		if (dent->name.len == len && 0 == strcmp(dent->name.str, name))
			return dent;
	}
	return NULL;
}

// Walk the file system structure to locate a file with the pathname stored in `*name`
// and saves parent dir to `*dirat` and the filename to `*name`.
// If `mkdir_p` is true, you need to create intermediate directories when it missing.
// If the pathname `*name` starts with '/', then lookup starts from `tmpfs_root`, 
// else from `*dirat`.
// Note that when `*name` ends with '/', the inode of last component will be
// saved in `*dirat` regardless of its type (e.g., even when it's FS_REG) and
// `*name` will point to '\0'
int tfs_namex(struct inode **dirat, const char **name, int mkdir_p)
{
	BUG_ON(dirat == NULL);
	BUG_ON(name == NULL);
	BUG_ON(*name == NULL);

	char buff[MAX_FILENAME_LEN + 1];
	int i;
	struct dentry *dent;
	int err;

	if (**name == '/') {
		*dirat = tmpfs_root;
		// make sure `name` starts with actual name
		while (**name && **name == '/')
			++(*name);
	} else {
		BUG_ON(*dirat == NULL);
		BUG_ON((*dirat)->type != FS_DIR);
	}

	// make sure a child name exists
	if (!**name)
		return -EINVAL;

	// `tfs_lookup` and `tfs_mkdir` are useful here

	/* LAB 5 TODO BEGIN */
#ifdef TMPFS_DBG
        printf("[tfs_namex] name %s\n", *name);
#endif
        // Initialize the char array buff to store name
        memset(buff, '\0', sizeof(buff));
        i = 0;
        while(**name != '\0'){
                if(**name == '/'){
                        buff[i] = '\0';
#ifdef TMPFS_DBG
                printf("[tfs_namex] **name == '/', buf %s\n", buff);
#endif
                        dent = tfs_lookup(*dirat, buff, i);
                        if(dent){
                                // found
#ifdef TMPFS_DBG
                                printf("[tfs_namex] dent found\n");
#endif
                        } else {
                                // not found
#ifdef TMPFS_DBG
                                printf("[tfs_namex] dent not found\n");
#endif
                                if(mkdir_p){
                                        tfs_mkdir(*dirat, buff, dent->inode->size);
                                } else {
                                        return -EINVAL;
                                }
                                dent = tfs_lookup(*dirat, buff, i);
                                if(!dent) return -EINVAL;
                        }
                        *dirat = dent->inode;
                        i = -1;
                        if(**name) memset(buff, '\0', sizeof(buff));
                } else {
                        buff[i] = **name;
#ifdef TMPFS_DBG
                        printf("[tfs_namex] buff[i] = %c\n", **name);
#endif

                }
                ++i;
                ++(*name);
        }
        *name = *name - i;

#ifdef TMPFS_DBG
        printf("[tfs_namex] return name %s\n", *name);
#endif
	/* LAB 5 TODO END */

	/* we will never reach here? */
	return 0;
}

int tfs_remove(struct inode *dir, const char *name, size_t len)
{
	u64 hash = hash_chars(name, len);
	struct dentry *dent, *target = NULL;
	struct hlist_head *head;

	BUG_ON(!name);

	if (len == 0) {
		WARN("mknod with len of 0");
		return -ENOENT;
	}

	head = htable_get_bucket(&dir->dentries, (u32) hash);

	for_each_in_hlist(dent, node, head) {
		if (dent->name.len == len && 0 == strcmp(dent->name.str, name)) {
			target = dent;
			break;
		}
	}

	if (!target)
		return -ENOENT;

	BUG_ON(!target->inode);

	// remove only when file is closed by all processes
	if (target->inode->type == FS_REG) {
		// free radix tree
		radix_free(&target->inode->data);
		// free inode
		free(target->inode);
		// remove dentry from parent
		htable_del(&target->node);
		// free dentry
		free(target);
	} else if (target->inode->type == FS_DIR) {
		if (!htable_empty(&target->inode->dentries))
			return -ENOTEMPTY;

		// free htable
		htable_free(&target->inode->dentries);
		// free inode
		free(target->inode);
		// remove dentry from parent
		htable_del(&target->node);
		// free dentry
		free(target);
	} else {
		BUG("inode type that shall not exist");
	}

	return 0;
}

// write memory into `inode` at `offset` from `buf` for length is `size`
// it may resize the file
// `radix_get`, `radix_add` are used in this function
ssize_t tfs_file_write(struct inode * inode, off_t offset, const char *data,
		       size_t size)
{
	BUG_ON(inode->type != FS_REG);
	BUG_ON(offset > inode->size);

#ifdef TMPFS_DBG
        printf("[tfs_file_write] offset %d, data %s\n", offset, data);
#endif

	u64 page_no, page_off;
	u64 cur_off = offset;
	size_t to_write;
	void *page;

	/* LAB 5 TODO BEGIN */
        if(offset + size > inode->size){
#ifdef TMPFS_DBG
                printf("[tfs_file_write] exceed the file size %d, resize the file %d\n", inode->size, offset + size);
#endif
                // exceed the file size, resize the file
                inode->size = offset + size;
        }

        size_t total_size = size;
        while(size > 0){
                page_no = cur_off / PAGE_SIZE;
                page_off = cur_off % PAGE_SIZE;
                page = radix_get(&(inode->data), page_no);
                if(!page){
                        page = malloc(PAGE_SIZE);
                        radix_add(&(inode->data), page_no, page);
                }

                to_write = PAGE_SIZE - page_off;
                to_write = (size > to_write) ? to_write : size;

#ifdef TMPFS_DBG
                printf("[tfs_file_write] to_write %d\n", to_write);
#endif

                // write file
                memcpy(page + page_off, data + total_size - size, to_write);
                size -= to_write;
                cur_off += to_write;
        }

	/* LAB 5 TODO END */

	return cur_off - offset;
}

// read memory from `inode` at `offset` in to `buf` for length is `size`, do not
// exceed the file size
// `radix_get` is used in this function
// You can use memory functions defined in libc
ssize_t tfs_file_read(struct inode * inode, off_t offset, char *buff,
		      size_t size)
{
	BUG_ON(inode->type != FS_REG);
	BUG_ON(offset > inode->size);

	u64 page_no, page_off;
	u64 cur_off = offset;   // offset should maintain unchanged
	size_t to_read; // size to read in current page
	void *page;

	/* LAB 5 TODO BEGIN */
        if(offset + size > inode->size){
                // exceed the file size, read till EOF
                size = inode->size - offset;
        }

        size_t total_size = size;
        while(size > 0){
                page_no = cur_off / PAGE_SIZE;
                page_off = cur_off % PAGE_SIZE;
                page = radix_get(&(inode->data), page_no);

                to_read = PAGE_SIZE - page_off;
                to_read = (size > to_read) ? to_read : size;

                // read file
                memcpy(buff + total_size - size, page + page_off, to_read);
                size -= to_read;
                cur_off += to_read;
        }

	/* LAB 5 TODO END */

	return cur_off - offset;
}

// load the cpio archive into tmpfs with the begin address as `start` in memory
// You need to create directories and files if necessary. You also need to write
// the data into the tmpfs.
int tfs_load_image(const char *start)
{
	struct cpio_file *f;
	struct inode *dirat;
	struct dentry *dent;
	const char *leaf;
	size_t len;
	int err;
	ssize_t write_count;

	BUG_ON(start == NULL);

	cpio_init_g_files();
	cpio_extract(start, "/");

	for (f = g_files.head.next; f; f = f->next) {
                /* LAB 5 TODO BEGIN */
                dirat = tmpfs_root;
                dent = tmpfs_root_dent;
                leaf = f->name;
                len = f->header.c_filesize;
                if(!tfs_namex(&dirat, &leaf, true)){
                        // success
                        dent = tfs_lookup(dirat, leaf, strlen(leaf));
                        if(!dent){
                                // not found
                                if(len == 0){
                                        // dir
#ifdef TMPFS_DBG
                                        printf("not found, create dir\n");
#endif
                                        tfs_mkdir(dirat, leaf, 1);
                                        continue;
                                } else {
                                        // file
#ifdef TMPFS_DBG
                                        printf("not found, create file\n");
#endif
                                        tfs_creat(dirat, leaf, len);
                                        dent = tfs_lookup(dirat, leaf, strlen(leaf));
                                        if(!dent) return -1;
                                }
                        }
#ifdef TMPFS_DBG
                        printf("file %s write in %s   and len is %d\n", leaf, f->data, len);
#endif
                        write_count = tfs_file_write(dent->inode, 0, f->data, len);
#ifdef TMPFS_DBG
                        printf("size is %d\n", dent->inode->size);
#endif
                }
                /* LAB 5 TODO END */
	}

	return 0;
}

static int dirent_filler(void **dirpp, void *end, char *name, off_t off,
			 unsigned char type, ino_t ino)
{
	struct dirent *dirp = *(struct dirent **)dirpp;
	void *p = dirp;
	unsigned short len = strlen(name) + 1 +
	    sizeof(dirp->d_ino) +
	    sizeof(dirp->d_off) + sizeof(dirp->d_reclen) + sizeof(dirp->d_type);
	p += len;
	if (p > end)
		return -EAGAIN;
	dirp->d_ino = ino;
	dirp->d_off = off;
	dirp->d_reclen = len;
	dirp->d_type = type;
	strcpy(dirp->d_name, name);
	*dirpp = p;
	return len;
}

/* path[0] must be '/' */
struct inode *tfs_open_path(const char *path)
{
	struct inode *dirat = NULL;
	const char *leaf = path;
	struct dentry *dent;
	int err;

	if (*path == '/' && !*(path + 1))
		return tmpfs_root;

	err = tfs_namex(&dirat, &leaf, 0);
	if (err)
		return NULL;

	dent = tfs_lookup(dirat, leaf, strlen(leaf));
	return dent ? dent->inode : NULL;
}

int del_inode(struct inode *inode)
{
	if (inode->type == FS_REG) {
		// free radix tree
		radix_free(&inode->data);
		// free inode
		free(inode);
	} else if (inode->type == FS_DIR) {
		if (!htable_empty(&inode->dentries))
			return -ENOTEMPTY;
		// free htable
		htable_free(&inode->dentries);
		// free inode
		free(inode);
	} else {
		BUG("inode type that shall not exist");
	}
	return 0;
}


int init_tmpfs(void)
{
	tmpfs_root = new_dir();
	tmpfs_root_dent = new_dent(tmpfs_root, "/", 1);

	init_id_manager(&fidman, MAX_NR_FID_RECORDS, DEFAULT_INIT_ID);
	/**
	 * Allocate the first id, which should be 0.
	 * No request should use 0 as the fid.
	 */
	chcore_assert(alloc_id(&fidman) == 0);

	init_fs_wrapper();

	mounted = true;

	return 0;
}

