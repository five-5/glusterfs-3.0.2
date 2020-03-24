/*
  Copyright (c) 2007-2009 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "io-cache.h"
#include <assert.h>
#include <sys/time.h>

int ioc_log2_page_size;

uint32_t
ioc_get_priority (ioc_table_t *table, const char *path);

uint32_t
ioc_get_priority (ioc_table_t *table, const char *path);


inline uint32_t
ioc_hashfn (void *data, int len)
{
        off_t offset;

        offset = *(off_t *) data;

        return (offset >> ioc_log2_page_size);
}

inline ioc_inode_t *
ioc_inode_reupdate (ioc_inode_t *ioc_inode)
{
	ioc_table_t *table = ioc_inode->table;

	list_add_tail (&ioc_inode->inode_lru, 
		       &table->inode_lru[ioc_inode->weight]);
  
	return ioc_inode;
}

inline ioc_inode_t *
ioc_get_inode (dict_t *dict, char *name)
{
	ioc_inode_t *ioc_inode = NULL;
	data_t      *ioc_inode_data = dict_get (dict, name);
	ioc_table_t *table = NULL;

	if (ioc_inode_data) {
		ioc_inode = data_to_ptr (ioc_inode_data);
		table = ioc_inode->table;

		ioc_table_lock (table);
		{
			if (list_empty (&ioc_inode->inode_lru)) {
				ioc_inode = ioc_inode_reupdate (ioc_inode);
			}
		}
		ioc_table_unlock (table);
	}
  
	return ioc_inode;
}

int32_t
ioc_inode_need_revalidate (ioc_inode_t *ioc_inode)
{
	int8_t         need_revalidate = 0;
	struct timeval tv = {0,};
	int32_t        ret = -1;
	ioc_table_t    *table = ioc_inode->table;

	ret = gettimeofday (&tv, NULL);

	if (time_elapsed (&tv, &ioc_inode->cache.tv) >= table->cache_timeout)
		need_revalidate = 1;

	return need_revalidate;
}

/*
 * __ioc_inode_flush - flush all the cached pages of the given inode
 *
 * @ioc_inode: 
 *
 * assumes lock is held
 */
int64_t
__ioc_inode_flush (ioc_inode_t *ioc_inode)
{
	ioc_page_t *curr = NULL, *next = NULL;
	int64_t    destroy_size = 0;
	int64_t    ret = 0;

	list_for_each_entry_safe (curr, next, &ioc_inode->cache.page_lru,
                                  page_lru) {
		ret = ioc_page_destroy (curr);
    
		if (ret != -1) 
			destroy_size += ret;
	}
  
	return destroy_size;
}

void
ioc_inode_flush (ioc_inode_t *ioc_inode)
{
	int64_t destroy_size = 0;    

	ioc_inode_lock (ioc_inode);
	{
		destroy_size = __ioc_inode_flush (ioc_inode);
	}
	ioc_inode_unlock (ioc_inode);
  
	if (destroy_size) {
		ioc_table_lock (ioc_inode->table);
		{
			ioc_inode->table->cache_used -= destroy_size;
		}
		ioc_table_unlock (ioc_inode->table);
	}

	return;
}

int32_t
ioc_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno,
                 struct stat *preop, struct stat *postop)
{
 	STACK_UNWIND_STRICT (setattr, frame, op_ret, op_errno, preop, postop);
 	return 0;
}

int32_t
ioc_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
             struct stat *stbuf, int32_t valid)
{
 	uint64_t ioc_inode = 0;

 	inode_ctx_get (loc->inode, this, &ioc_inode);

 	if (ioc_inode
            && ((valid & GF_SET_ATTR_ATIME)
                || (valid & GF_SET_ATTR_MTIME)))
 		ioc_inode_flush ((ioc_inode_t *)(long)ioc_inode);

 	STACK_WIND (frame, ioc_setattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->setattr, loc, stbuf, valid);

 	return 0;
}

int32_t
ioc_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		int32_t op_ret,	int32_t op_errno, inode_t *inode,
		struct stat *stbuf, dict_t *dict, struct stat *postparent)
{
	ioc_inode_t   *ioc_inode = NULL;
	ioc_table_t   *table = this->private;
	uint8_t       cache_still_valid = 0;
	uint64_t      tmp_ioc_inode = 0;
	
	if (op_ret != 0) 
		goto out;

	inode_ctx_get (inode, this, &tmp_ioc_inode);
	ioc_inode = (ioc_inode_t *)(long)tmp_ioc_inode;
	if (ioc_inode) {
		ioc_inode_lock (ioc_inode);
		{
                        if (ioc_inode->cache.mtime == 0) {
                                ioc_inode->cache.mtime = stbuf->st_mtime;
                        }
		}
		ioc_inode_unlock (ioc_inode);

		cache_still_valid = ioc_cache_still_valid (ioc_inode, 
							   stbuf);
		
		if (!cache_still_valid) {
			ioc_inode_flush (ioc_inode);
		} 
		
		ioc_table_lock (ioc_inode->table);
		{
			list_move_tail (&ioc_inode->inode_lru,
					&table->inode_lru[ioc_inode->weight]);
		}
		ioc_table_unlock (ioc_inode->table);
	}
	
out:
	STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno, inode, stbuf, dict,
                      postparent);
	return 0;
}

int32_t 
ioc_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc,
	    dict_t *xattr_req)
{
	STACK_WIND (frame, ioc_lookup_cbk, FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->lookup, loc, xattr_req);

	return 0;
}

/*
 * ioc_forget - 
 *
 * @frame:
 * @this:
 * @inode:
 *
 */
int32_t
ioc_forget (xlator_t *this, inode_t *inode)
{
	uint64_t ioc_inode = 0;

	inode_ctx_get (inode, this, &ioc_inode);

	if (ioc_inode)
		ioc_inode_destroy ((ioc_inode_t *)(long)ioc_inode);
	    
	return 0;
}


/* 
 * ioc_cache_validate_cbk - 
 *
 * @frame:
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 * @buf
 *
 */
int32_t
ioc_cache_validate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			int32_t op_ret, int32_t op_errno, struct stat *stbuf)
{
	ioc_local_t *local = NULL;
	ioc_inode_t *ioc_inode = NULL;
	size_t      destroy_size = 0;
	struct stat *local_stbuf = NULL;

        local = frame->local;
	ioc_inode = local->inode;
        local_stbuf = stbuf;

	if ((op_ret == -1) || 
	    ((op_ret >= 0) && !ioc_cache_still_valid(ioc_inode, stbuf))) {
		gf_log (ioc_inode->table->xl->name, GF_LOG_DEBUG,
			"cache for inode(%p) is invalid. flushing all pages",
			ioc_inode);
		/* NOTE: only pages with no waiting frames are flushed by 
		 * ioc_inode_flush. page_fault will be generated for all 
		 * the pages which have waiting frames by ioc_inode_wakeup()
		 */
		ioc_inode_lock (ioc_inode);
		{
			destroy_size = __ioc_inode_flush (ioc_inode);
			if (op_ret >= 0)
				ioc_inode->cache.mtime = stbuf->st_mtime;
		}
		ioc_inode_unlock (ioc_inode);
		local_stbuf = NULL;
	}

	if (destroy_size) {
		ioc_table_lock (ioc_inode->table);
		{
			ioc_inode->table->cache_used -= destroy_size;
		}
		ioc_table_unlock (ioc_inode->table);
	}

	if (op_ret < 0)
		local_stbuf = NULL;
  
	ioc_inode_lock (ioc_inode);
	{
		gettimeofday (&ioc_inode->cache.tv, NULL);
	}
	ioc_inode_unlock (ioc_inode);

	ioc_inode_wakeup (frame, ioc_inode, local_stbuf);
  
	/* any page-fault initiated by ioc_inode_wakeup() will have its own 
	 * fd_ref on fd, safe to unref validate frame's private copy 
	 */
	fd_unref (local->fd);

	STACK_DESTROY (frame->root);

	return 0;
}

int32_t
ioc_wait_on_inode (ioc_inode_t *ioc_inode, ioc_page_t *page)
{
	ioc_waitq_t *waiter = NULL, *trav = NULL;
	uint32_t    page_found = 0;
        int32_t     ret = 0;

	trav = ioc_inode->waitq;

	while (trav) {
		if (trav->data == page) {
			page_found = 1;
			break;
		}
		trav = trav->next;
	}
  
	if (!page_found) {
		waiter = CALLOC (1, sizeof (ioc_waitq_t));
                if (waiter == NULL) {
                        gf_log (ioc_inode->table->xl->name, GF_LOG_ERROR,
                                "out of memory");
                        ret = -ENOMEM;
                        goto out;
                }

		waiter->data = page;
		waiter->next = ioc_inode->waitq;
		ioc_inode->waitq = waiter;
	}

out:  
	return ret;
}

/*
 * ioc_cache_validate -
 *
 * @frame:
 * @ioc_inode:
 * @fd:
 *
 */
int32_t
ioc_cache_validate (call_frame_t *frame, ioc_inode_t *ioc_inode, fd_t *fd,
		    ioc_page_t *page)
{
	call_frame_t *validate_frame = NULL;
	ioc_local_t  *validate_local = NULL;
        ioc_local_t  *local = NULL;
        int32_t      ret = 0;

        local = frame->local;
	validate_local = CALLOC (1, sizeof (ioc_local_t));
        if (validate_local == NULL) {
                ret = -1;
                local->op_ret = -1;
                local->op_errno = ENOMEM;
                gf_log (ioc_inode->table->xl->name, GF_LOG_ERROR,
                        "out of memory");
                goto out;
        }

	validate_frame = copy_frame (frame);
        if (validate_frame == NULL) {
                ret = -1;
                local->op_ret = -1;
                local->op_errno = ENOMEM;
                FREE (validate_local);
                gf_log (ioc_inode->table->xl->name, GF_LOG_ERROR,
                        "out of memory");
                goto out;
        }

	validate_local->fd = fd_ref (fd);
	validate_local->inode = ioc_inode;
	validate_frame->local = validate_local;
    
	STACK_WIND (validate_frame, ioc_cache_validate_cbk,
                    FIRST_CHILD (frame->this),
                    FIRST_CHILD (frame->this)->fops->fstat, fd);

out:
	return ret;
}

inline uint32_t
is_match (const char *path, const char *pattern)
{
	int32_t ret = 0;

	ret = fnmatch (pattern, path, FNM_NOESCAPE);
  
	return (ret == 0);
}

uint32_t
ioc_get_priority (ioc_table_t *table, const char *path)
{
	uint32_t            priority = 0;
	struct ioc_priority *curr = NULL;
  
	if (list_empty(&table->priority_list)) {
		priority = 1;
	}
	else {
		list_for_each_entry (curr, &table->priority_list, list) {
			if (is_match (path, curr->pattern)) 
				priority = curr->priority;
		}
	}

	return priority;
}

/* 
 * ioc_open_cbk - open callback for io cache
 *
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 * @fd:
 *
 */
int32_t
ioc_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
	      int32_t op_errno, fd_t *fd)
{
	uint64_t    tmp_ioc_inode = 0;
	ioc_local_t *local = NULL;
	ioc_table_t *table = NULL;
	ioc_inode_t *ioc_inode = NULL;
	inode_t     *inode = NULL;
	uint32_t    weight = 0xffffffff;
	const char  *path = NULL;

        local = frame->local;
        table = this->private;
        inode = local->file_loc.inode;
        path = local->file_loc.path;

	if (op_ret != -1) {
		/* look for ioc_inode corresponding to this fd */
		LOCK (&fd->inode->lock);
		{
                        __inode_ctx_get (fd->inode, this, &tmp_ioc_inode);
                        ioc_inode = (ioc_inode_t *)(long)tmp_ioc_inode;
                        
                        if (!ioc_inode) {
                                /*
                                  this is the first time someone is opening
                                  this file, assign weight 
                                */
                                weight = ioc_get_priority (table, path);
 
                                ioc_inode = ioc_inode_update (table, inode,
                                                              weight);

                                __inode_ctx_put (fd->inode, this, 
                                                 (uint64_t)(long)ioc_inode);
                        } else {
                                ioc_table_lock (ioc_inode->table);
                                {
                                        list_move_tail (&ioc_inode->inode_lru,
                                                        &table->inode_lru[ioc_inode->weight]);
                                }
                                ioc_table_unlock (ioc_inode->table);
                        }

		}
		UNLOCK (&fd->inode->lock);

		/* If mandatory locking has been enabled on this file,
		   we disable caching on it */
		if (((inode->st_mode & S_ISGID)
                     && !(inode->st_mode & S_IXGRP))) {
			fd_ctx_set (fd, this, 1);
		}
  
		/* If O_DIRECT open, we disable caching on it */
		if ((local->flags & O_DIRECT)){
			/* O_DIRECT is only for one fd, not the inode 
			 * as a whole 
			 */
			fd_ctx_set (fd, this, 1);
		}

		/* weight = 0, we disable caching on it */
		if (weight == 0) {
			/* we allow a pattern-matched cache disable this way 
			 */
			fd_ctx_set (fd, this, 1);
		}
	}

	FREE (local);
	frame->local = NULL;

	STACK_UNWIND_STRICT (open, frame, op_ret, op_errno, fd);

	return 0;
}

/*
 * ioc_create_cbk - create callback for io cache
 *
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 * @fd:
 * @inode:
 * @buf:
 *
 */
int32_t
ioc_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret,	int32_t op_errno, fd_t *fd,
		inode_t *inode,	struct stat *buf, struct stat *preparent,
                struct stat *postparent)
{
	ioc_local_t *local = NULL;
	ioc_table_t *table = NULL;
	ioc_inode_t *ioc_inode = NULL;
	uint32_t    weight = 0xffffffff;
	const char  *path = NULL;

        local = frame->local;
        table = this->private;
        path = local->file_loc.path;

	if (op_ret != -1) {
		{
			/* assign weight */
			weight = ioc_get_priority (table, path);

			ioc_inode = ioc_inode_update (table, inode, weight);

                        ioc_inode_lock (ioc_inode);
                        {
                                ioc_inode->cache.mtime = buf->st_mtime;
                        }
                        ioc_inode_unlock (ioc_inode);

                        inode_ctx_put (fd->inode, this,
                                       (uint64_t)(long)ioc_inode);
		}
		/*
                 * If mandatory locking has been enabled on this file,
                 * we disable caching on it
                 */
		if ((inode->st_mode & S_ISGID) && 
		    !(inode->st_mode & S_IXGRP)) {
			fd_ctx_set (fd, this, 1);
		}

		/* If O_DIRECT open, we disable caching on it */
		if (local->flags & O_DIRECT){
			/*
                         * O_DIRECT is only for one fd, not the inode 
			 * as a whole 
			 */
			fd_ctx_set (fd, this, 1);
		}
    
		/* weight = 0, we disable caching on it */
		if (weight == 0) {
			/* we allow a pattern-matched cache disable this way 
			 */
			fd_ctx_set (fd, this, 1);
		}
	}
  
	frame->local = NULL;
	FREE (local);

        STACK_UNWIND_STRICT (create, frame, op_ret, op_errno, fd, inode, buf,
                             preparent, postparent);

	return 0;
}

/*
 * ioc_open - open fop for io cache
 * @frame:
 * @this:
 * @loc:
 * @flags:
 *
 */
int32_t
ioc_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
	  fd_t *fd, int32_t wbflags)
{
  
	ioc_local_t *local = NULL;

        local = CALLOC (1, sizeof (ioc_local_t));
        if (local == NULL) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                STACK_UNWIND_STRICT (open, frame, -1, ENOMEM, NULL);
                return 0;
        }

	local->flags = flags;
	local->file_loc.path = loc->path;
	local->file_loc.inode = loc->inode;
  
	frame->local = local;
        
	STACK_WIND (frame, ioc_open_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->open, loc, flags, fd, wbflags);

	return 0;
}

/*
 * ioc_create - create fop for io cache
 * 
 * @frame:
 * @this:
 * @pathname:
 * @flags:
 * @mode:
 *
 */
int32_t
ioc_create (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
	    mode_t mode, fd_t *fd)
{
	ioc_local_t *local = NULL;

        local = CALLOC (1, sizeof (ioc_local_t));
        if (local == NULL) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                STACK_UNWIND_STRICT (create, frame, -1, ENOMEM, NULL, NULL,
                                     NULL, NULL, NULL);
                return 0;
        }

	local->flags = flags;
	local->file_loc.path = loc->path;
	frame->local = local;

	STACK_WIND (frame, ioc_create_cbk, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->create, loc, flags, mode, fd);

	return 0;
}




/*
 * ioc_release - release fop for io cache
 * 
 * @frame:
 * @this:
 * @fd:
 *
 */
int32_t
ioc_release (xlator_t *this, fd_t *fd)
{
	return 0;
}

/* 
 * ioc_readv_disabled_cbk 
 * @frame:
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 * @vector:
 * @count:
 *
 */ 
int32_t
ioc_readv_disabled_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret,	int32_t op_errno, struct iovec *vector,
			int32_t count, struct stat *stbuf,
                        struct iobref *iobref)
{
	STACK_UNWIND_STRICT (readv, frame, op_ret, op_errno, vector, count,
                             stbuf, iobref);
	return 0;
}


int32_t
ioc_need_prune (ioc_table_t *table)
{
	int64_t cache_difference = 0;
  
	ioc_table_lock (table);
	{
		cache_difference = table->cache_used - table->cache_size;
	}
	ioc_table_unlock (table);

	if (cache_difference > 0)
		return 1;
	else 
		return 0;
}

/*
 * ioc_dispatch_requests -
 * 
 * @frame:
 * @inode:
 *
 * 
 */
void
ioc_dispatch_requests (call_frame_t *frame, ioc_inode_t *ioc_inode, fd_t *fd,
                       off_t offset, size_t size)
{
	ioc_local_t *local = NULL;
	ioc_table_t *table = NULL;
	ioc_page_t  *trav = NULL;
	ioc_waitq_t *waitq = NULL;
	off_t       rounded_offset = 0;
	off_t       rounded_end = 0;
	off_t       trav_offset = 0;
	int32_t     fault = 0;
        size_t      trav_size = 0;
        off_t       local_offset = 0;
        int32_t     ret = -1;
	int8_t      need_validate = 0;
	int8_t      might_need_validate = 0;  /*
                                               * if a page exists, do we need 
                                               * to validate it?
                                               */
        local = frame->local;
        table = ioc_inode->table;

	rounded_offset = floor (offset, table->page_size);
	rounded_end = roof (offset + size, table->page_size);
	trav_offset = rounded_offset;

	/* once a frame does read, it should be waiting on something */
	local->wait_count++;

	/* Requested region can fall in three different pages,
	 * 1. Ready - region is already in cache, we just have to serve it.
	 * 2. In-transit - page fault has been generated on this page, we need
	 *    to wait till the page is ready
	 * 3. Fault - page is not in cache, we have to generate a page fault
	 */

	might_need_validate = ioc_inode_need_revalidate (ioc_inode);

	while (trav_offset < rounded_end) {
		ioc_inode_lock (ioc_inode);
		//{

		/* look for requested region in the cache */
		trav = ioc_page_get (ioc_inode, trav_offset);

		local_offset = max (trav_offset, offset);
		trav_size = min (((offset+size) - local_offset), 
				 table->page_size);

		if (!trav) {
			/* page not in cache, we need to generate page fault */
			trav = ioc_page_create (ioc_inode, trav_offset);
			fault = 1;
			if (!trav) {
				gf_log (frame->this->name, GF_LOG_CRITICAL,
					"out of memory");
                                local->op_ret = -1;
                                local->op_errno = ENOMEM;
                                goto out;
			}
		} 

		ioc_wait_on_page (trav, frame, local_offset, trav_size);

		if (trav->ready) {
			/* page found in cache */
			if (!might_need_validate && !ioc_inode->waitq) {
				/* fresh enough */
				gf_log (frame->this->name, GF_LOG_TRACE,
					"cache hit for trav_offset=%"PRId64""
					"/local_offset=%"PRId64"",
					trav_offset, local_offset);
				waitq = ioc_page_wakeup (trav);
			} else {
				/* if waitq already exists, fstat revalidate is
				   already on the way */
				if (!ioc_inode->waitq) {
					need_validate = 1;
				}

				ret = ioc_wait_on_inode (ioc_inode, trav);
                                if (ret < 0) {
                                        local->op_ret = -1;
                                        local->op_errno = -ret;
                                        need_validate = 0;

                                        waitq = ioc_page_wakeup (trav);
                                        ioc_inode_unlock (ioc_inode);

                                        ioc_waitq_return (waitq);
                                        waitq = NULL;
                                        goto out;
                                }
			}
		}

		//}
		ioc_inode_unlock (ioc_inode);
    
		ioc_waitq_return (waitq);
		waitq = NULL;

		if (fault) {
			fault = 0;
			/* new page created, increase the table->cache_used */
			ioc_page_fault (ioc_inode, frame, fd, trav_offset);
		}

		if (need_validate) {
			need_validate = 0;
			gf_log (frame->this->name, GF_LOG_TRACE,
				"sending validate request for "
				"inode(%"PRId64") at offset=%"PRId64"",
				fd->inode->ino, trav_offset);
			ret = ioc_cache_validate (frame, ioc_inode, fd, trav);
                        if (ret == -1) {
                                ioc_inode_lock (ioc_inode);
                                {
                                        waitq = ioc_page_wakeup (trav);
                                }
                                ioc_inode_unlock (ioc_inode);

                                ioc_waitq_return (waitq);
                                waitq = NULL;
                                goto out;
                        }
		}
    
		trav_offset += table->page_size;
	}

out:
	ioc_frame_return (frame);

	if (ioc_need_prune (ioc_inode->table)) {
		ioc_prune (ioc_inode->table);
	}

	return;
}


/*
 * ioc_readv -
 * 
 * @frame:
 * @this:
 * @fd:
 * @size:
 * @offset:
 *
 */
int32_t
ioc_readv (call_frame_t *frame, xlator_t *this, fd_t *fd,
	   size_t size, off_t offset)
{
	uint64_t     tmp_ioc_inode = 0;
	ioc_inode_t  *ioc_inode = NULL;
	ioc_local_t  *local = NULL;
	uint32_t     weight = 0;
        ioc_table_t  *table = NULL;
        uint32_t     num_pages = 0;
        int32_t      op_errno = -1;

        if (!this) {
                goto out;
        }

	inode_ctx_get (fd->inode, this, &tmp_ioc_inode);
	ioc_inode = (ioc_inode_t *)(long)tmp_ioc_inode;
	if (!ioc_inode) {
		/* caching disabled, go ahead with normal readv */
		STACK_WIND (frame, ioc_readv_disabled_cbk,
                            FIRST_CHILD (frame->this), 
			    FIRST_CHILD (frame->this)->fops->readv, fd, size,
                            offset);
		return 0;
	}


        table = this->private;

        if (!table) {
                gf_log (this->name, GF_LOG_ERROR, "table is null");
                op_errno = EINVAL;
                goto out;
        }


        ioc_table_lock (table);
        {
                if (!table->mem_pool) {

                        num_pages = (table->cache_size / table->page_size)
                                + ((table->cache_size % table->page_size) 
                                ? 1 : 0);

                        table->mem_pool
                                =  mem_pool_new (rbthash_entry_t, num_pages);

                        if (!table->mem_pool) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Unable to allocate mem_pool");
                                op_errno = ENOMEM;
                                ioc_table_unlock (table);
                                goto out;
                        }
                }
        }
        ioc_table_unlock (table);

        ioc_inode_lock (ioc_inode);
        {
                if (!ioc_inode->cache.page_table) {
                        ioc_inode->cache.page_table
                                = rbthash_table_init 
                                                (IOC_PAGE_TABLE_BUCKET_COUNT,
                                                 ioc_hashfn, NULL, 0,
                                                 table->mem_pool);

                        if (ioc_inode->cache.page_table == NULL) {
                                op_errno = ENOMEM;
                                ioc_inode_unlock (ioc_inode);
                                goto out;
                        }
                }
        }
        ioc_inode_unlock (ioc_inode);

	if (!fd_ctx_get (fd, this, NULL)) {
		/* disable caching for this fd, go ahead with normal readv */
		STACK_WIND (frame, ioc_readv_disabled_cbk,
                            FIRST_CHILD (frame->this), 
			    FIRST_CHILD (frame->this)->fops->readv, fd, size,
                            offset);
		return 0;
	}

	local = (ioc_local_t *) CALLOC (1, sizeof (ioc_local_t));
        if (local == NULL) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                op_errno = ENOMEM;
                goto out;
        }

	INIT_LIST_HEAD (&local->fill_list);

	frame->local = local;  
	local->pending_offset = offset;
	local->pending_size = size;
	local->offset = offset;
	local->size = size;
	local->inode = ioc_inode;

	gf_log (this->name, GF_LOG_TRACE,
		"NEW REQ (%p) offset = %"PRId64" && size = %"GF_PRI_SIZET"", 
		frame, offset, size);

	weight = ioc_inode->weight;

	ioc_table_lock (ioc_inode->table);
	{
		list_move_tail (&ioc_inode->inode_lru, 
				&ioc_inode->table->inode_lru[weight]);
	}
	ioc_table_unlock (ioc_inode->table);

	ioc_dispatch_requests (frame, ioc_inode, fd, offset, size);
	return 0;

out:
        STACK_UNWIND_STRICT (readv, frame, -1, op_errno, NULL, 0, NULL, NULL);
        return 0;
}

/*
 * ioc_writev_cbk -
 * 
 * @frame:
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 *
 */
int32_t
ioc_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		int32_t op_ret,	int32_t op_errno, struct stat *prebuf,
                struct stat *postbuf)
{
	ioc_local_t *local     = NULL;
	uint64_t    ioc_inode = 0;

        local = frame->local;
	inode_ctx_get (local->fd->inode, this, &ioc_inode);
  
	if (ioc_inode)
		ioc_inode_flush ((ioc_inode_t *)(long)ioc_inode);

	STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, prebuf, postbuf);
	return 0;
}

/*
 * ioc_writev
 * 
 * @frame:
 * @this:
 * @fd:
 * @vector:
 * @count:
 * @offset:
 *
 */
int32_t
ioc_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
	    struct iovec *vector, int32_t count, off_t offset,
            struct iobref *iobref)
{
	ioc_local_t *local     = NULL;
	uint64_t    ioc_inode = 0;

	local = CALLOC (1, sizeof (ioc_local_t));
        if (local == NULL) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");

                STACK_UNWIND_STRICT (writev, frame, -1, ENOMEM, NULL, NULL);
                return 0;
        }

	/* TODO: why is it not fd_ref'ed */
	local->fd = fd;
	frame->local = local;

	inode_ctx_get (fd->inode, this, &ioc_inode);
	if (ioc_inode)
		ioc_inode_flush ((ioc_inode_t *)(long)ioc_inode);

	STACK_WIND (frame, ioc_writev_cbk, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->writev, fd, vector, count, offset,
                    iobref);

	return 0;
}

/*
 * ioc_truncate_cbk -
 * 
 * @frame:
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 * @buf:
 *
 */
int32_t 
ioc_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct stat *prebuf,
                  struct stat *postbuf)
{

	STACK_UNWIND_STRICT (truncate, frame, op_ret, op_errno, prebuf,
                             postbuf);
	return 0;
}


/*
 * ioc_ftruncate_cbk -
 *
 * @frame:
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 * @buf:
 *
 */
int32_t
ioc_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct stat *prebuf,
                  struct stat *postbuf)
{

	STACK_UNWIND_STRICT (ftruncate, frame, op_ret, op_errno, prebuf,
                             postbuf);
	return 0;
}


/*
 * ioc_truncate -
 * 
 * @frame:
 * @this:
 * @loc:
 * @offset:
 *
 */
int32_t 
ioc_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset)
{
	uint64_t ioc_inode = 0;
	inode_ctx_get (loc->inode, this, &ioc_inode);

	if (ioc_inode)
		ioc_inode_flush ((ioc_inode_t *)(long)ioc_inode);

	STACK_WIND (frame, ioc_truncate_cbk, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->truncate, loc, offset);
	return 0;
}

/*
 * ioc_ftruncate -
 * 
 * @frame:
 * @this:
 * @fd:
 * @offset:
 *
 */
int32_t
ioc_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset)
{
	uint64_t ioc_inode = 0;
	inode_ctx_get (fd->inode, this, &ioc_inode);

	if (ioc_inode)
		ioc_inode_flush ((ioc_inode_t *)(long)ioc_inode);

	STACK_WIND (frame, ioc_ftruncate_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->ftruncate, fd, offset);
	return 0;
}

int32_t
ioc_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
	    int32_t op_errno, struct flock *lock)
{
	STACK_UNWIND_STRICT (lk, frame, op_ret, op_errno, lock);
	return 0;
}

int32_t 
ioc_lk (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t cmd,
        struct flock *lock)
{
	ioc_inode_t  *ioc_inode = NULL;
	uint64_t     tmp_inode = 0;

	inode_ctx_get (fd->inode, this, &tmp_inode);
	ioc_inode = (ioc_inode_t *)(long)tmp_inode;
	if (!ioc_inode) {
		gf_log (this->name, GF_LOG_DEBUG,
			"inode context is NULL: returning EBADFD");
		STACK_UNWIND_STRICT (lk, frame, -1, EBADFD, NULL);
		return 0;
	}

	ioc_inode_lock (ioc_inode);
	{
		gettimeofday (&ioc_inode->cache.tv, NULL);
	}
	ioc_inode_unlock (ioc_inode);

	STACK_WIND (frame, ioc_lk_cbk, FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->lk, fd, cmd, lock);

	return 0;
}

int32_t
ioc_get_priority_list (const char *opt_str, struct list_head *first)
{
	int32_t              max_pri = 1;
	char                *tmp_str = NULL;
	char                *tmp_str1 = NULL;
	char                *tmp_str2 = NULL;
	char                *dup_str = NULL;
	char                *stripe_str = NULL;
	char                *pattern = NULL;
	char                *priority = NULL;
	char                *string = NULL;
	struct ioc_priority *curr = NULL, *tmp = NULL;

        string = strdup (opt_str);
        if (string == NULL) {
                max_pri = -1;
                goto out;
        }
                
	/* Get the pattern for cache priority. 
	 * "option priority *.jpg:1,abc*:2" etc 
	 */
	/* TODO: inode_lru in table is statically hard-coded to 5, 
	 * should be changed to run-time configuration 
	 */
	stripe_str = strtok_r (string, ",", &tmp_str);
	while (stripe_str) {
		curr = CALLOC (1, sizeof (struct ioc_priority));
                if (curr == NULL) {
                        max_pri = -1;
                        goto out;
                }

		list_add_tail (&curr->list, first);

		dup_str = strdup (stripe_str);
                if (dup_str == NULL) {
                        max_pri = -1;
                        goto out;
                }

		pattern = strtok_r (dup_str, ":", &tmp_str1);
		if (!pattern) {
                        max_pri = -1;
                        goto out;
                }

		priority = strtok_r (NULL, ":", &tmp_str1);
		if (!priority) {
                        max_pri = -1;
                        goto out;
                }

		gf_log ("io-cache", GF_LOG_TRACE,
			"ioc priority : pattern %s : priority %s",
			pattern,
			priority);

		curr->pattern = strdup (pattern);
                if (curr->pattern == NULL) {
                        max_pri = -1;
                        goto out;
                }

		curr->priority = strtol (priority, &tmp_str2, 0);
		if (tmp_str2 && (*tmp_str2)) {
                        max_pri = -1;
                        goto out;
                } else {
 			max_pri = max (max_pri, curr->priority);
                }

                free (dup_str);
                dup_str = NULL;

		stripe_str = strtok_r (NULL, ",", &tmp_str);
	}
out:
        if (string != NULL) {
                free (string);
        }

        if (dup_str != NULL) {
                free (dup_str);
        }

        if (max_pri == -1) {
                list_for_each_entry_safe (curr, tmp, first, list) {
                        list_del_init (&curr->list);
                        free (curr->pattern);
                        free (curr);
                }
        }

	return max_pri;
}

/*
 * init - 
 * @this:
 *
 */
int32_t 
init (xlator_t *this)
{
	ioc_table_t     *table = NULL;
	dict_t          *options = this->options;
	uint32_t         index = 0;
	char            *cache_size_string = NULL;
        int32_t          ret = -1;
        glusterfs_ctx_t *ctx = NULL;

	if (!this->children || this->children->next) {
		gf_log (this->name, GF_LOG_ERROR,
			"FATAL: io-cache not configured with exactly "
			"one child");
                goto out;
	}

	if (!this->parents) {
		gf_log (this->name, GF_LOG_WARNING,
			"dangling volume. check volfile ");
	}

	table = (void *) CALLOC (1, sizeof (*table));
        if (table == NULL) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                goto out;
        }
  
	table->xl = this;
	table->page_size = this->ctx->page_size;
	table->cache_size = IOC_CACHE_SIZE;

	if (dict_get (options, "cache-size"))
		cache_size_string = data_to_str (dict_get (options, 
							   "cache-size"));
	if (cache_size_string) {
		if (gf_string2bytesize (cache_size_string, 
					&table->cache_size) != 0) {
			gf_log ("io-cache", GF_LOG_ERROR, 
				"invalid number format \"%s\" of "
				"\"option cache-size\"", 
				cache_size_string);
                        goto out;
		}
      
		gf_log (this->name, GF_LOG_TRACE, 
			"using cache-size %"PRIu64"", table->cache_size);
	}
  
	table->cache_timeout = 1;

	if (dict_get (options, "cache-timeout")) {
		table->cache_timeout = 
			data_to_uint32 (dict_get (options,
						  "cache-timeout"));
		gf_log (this->name, GF_LOG_TRACE,
			"Using %d seconds to revalidate cache",
			table->cache_timeout);
	}

	INIT_LIST_HEAD (&table->priority_list);
	table->max_pri = 1;
	if (dict_get (options, "priority")) {
		char *option_list = data_to_str (dict_get (options, 
							   "priority"));
		gf_log (this->name, GF_LOG_TRACE,
			"option path %s", option_list);
		/* parse the list of pattern:priority */
		table->max_pri = ioc_get_priority_list (option_list, 
							&table->priority_list);
    
		if (table->max_pri == -1) {
                        goto out;
                }
	}
	table->max_pri ++;
	INIT_LIST_HEAD (&table->inodes);
  
	table->inode_lru = CALLOC (table->max_pri, sizeof (struct list_head));
        if (table->inode_lru == NULL) {
                goto out;
        }

	for (index = 0; index < (table->max_pri); index++)
		INIT_LIST_HEAD (&table->inode_lru[index]);

	pthread_mutex_init (&table->table_lock, NULL);
	this->private = table;
        ret = 0;

        ctx = this->ctx;
        ioc_log2_page_size = gf_log2 (ctx->page_size);

out:
        if (ret == -1) {
                if (table != NULL) {
                        free (table->inode_lru);
                        free (table);
                }
        }

	return ret;
}

/*
 * fini -
 * 
 * @this:
 *
 */
void
fini (xlator_t *this)
{
	ioc_table_t *table = this->private;

        if (table == NULL)
                return;

	pthread_mutex_destroy (&table->table_lock);
	FREE (table);

	this->private = NULL;
	return;
}

struct xlator_fops fops = {
	.open        = ioc_open,
	.create      = ioc_create,
	.readv       = ioc_readv,
	.writev      = ioc_writev,
	.truncate    = ioc_truncate,
	.ftruncate   = ioc_ftruncate,
	.lookup      = ioc_lookup,
	.lk          = ioc_lk,
        .setattr     = ioc_setattr
};

struct xlator_mops mops = {
};

struct xlator_cbks cbks = {
	.forget      = ioc_forget,
  	.release     = ioc_release
};

struct volume_options options[] = {
	{ .key  = {"priority"}, 
	  .type = GF_OPTION_TYPE_ANY 
	},
	{ .key  = {"cache-timeout", "force-revalidate-timeout"},
	  .type = GF_OPTION_TYPE_INT,
	  .min  = 0, 
	  .max  = 60 
	}, 
	{ .key  = {"cache-size"}, 
	  .type = GF_OPTION_TYPE_SIZET,
	  .min  = 4 * GF_UNIT_MB, 
	  .max  = 6 * GF_UNIT_GB 
	},
	{ .key = {NULL} },
};
