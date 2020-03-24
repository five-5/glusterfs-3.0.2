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


#include <libgen.h>
#include <unistd.h>
#include <fnmatch.h>
#include <sys/time.h>
#include <stdlib.h>
#include <signal.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "afr.h"
#include "dict.h"
#include "xlator.h"
#include "hashfn.h"
#include "logging.h"
#include "stack.h"
#include "list.h"
#include "call-stub.h"
#include "defaults.h"
#include "common-utils.h"
#include "compat-errno.h"
#include "compat.h"

#include "afr.h"
#include "afr-transaction.h"


/* {{{ writev */

int
afr_writev_unwind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;
	call_frame_t   *main_frame = NULL;

	local = frame->local;
	priv  = this->private;

	LOCK (&frame->lock);
	{
		if (local->transaction.main_frame)
			main_frame = local->transaction.main_frame;
		local->transaction.main_frame = NULL;
	}
	UNLOCK (&frame->lock);

	if (main_frame) {
                local->cont.writev.prebuf.st_ino  = local->cont.writev.ino;
                local->cont.writev.postbuf.st_ino = local->cont.writev.ino;

		AFR_STACK_UNWIND (writev, main_frame,
                                  local->op_ret, local->op_errno,
                                  &local->cont.writev.prebuf,
                                  &local->cont.writev.postbuf);
	}
	return 0;
}


int
afr_writev_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
		     int32_t op_ret, int32_t op_errno, struct stat *prebuf,
                     struct stat *postbuf)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int child_index = (long) cookie;
	int call_count  = -1;
	int need_unwind = 0;
        int read_child  = 0;

	local = frame->local;
	priv = this->private;

        read_child = afr_read_child (this, local->fd->inode);

	LOCK (&frame->lock);
	{
                if (child_index == read_child) {
                        local->read_child_returned = _gf_true;
                }

		if (afr_fop_failed (op_ret, op_errno))
			afr_transaction_fop_failed (frame, this, child_index);

		if (op_ret != -1) {
			if (local->success_count == 0) {
				local->op_ret              = op_ret;
				local->cont.writev.prebuf  = *prebuf;
				local->cont.writev.postbuf = *postbuf;
			}

                        if (child_index == read_child) {
                                local->cont.writev.prebuf  = *prebuf;
                                local->cont.writev.postbuf = *postbuf;
                        }

			local->success_count++;

			if ((local->success_count >= priv->wait_count)
                            && local->read_child_returned) {
				need_unwind = 1;
			}
		}

		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0) {
		local->transaction.unwind (frame, this);

		local->transaction.resume (frame, this);
	}
	
	return 0;
}


int
afr_writev_wind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	
	int i = 0;
	int call_count = -1;

	local = frame->local;
	priv = this->private;

	call_count = afr_up_children_count (priv->child_count, local->child_up);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
		return 0;
	}

	local->call_count = call_count;

	for (i = 0; i < priv->child_count; i++) {				
		if (local->child_up[i]) {
			STACK_WIND_COOKIE (frame, afr_writev_wind_cbk, 
					   (void *) (long) i,	
					   priv->children[i], 
					   priv->children[i]->fops->writev,
					   local->fd, 
					   local->cont.writev.vector,
					   local->cont.writev.count, 
					   local->cont.writev.offset,
                                           local->cont.writev.iobref);
		
			if (!--call_count)
				break;
		}
	}
	
	return 0;
}


int
afr_writev_done (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;

	local = frame->local;

        iobref_unref (local->cont.writev.iobref);
	local->cont.writev.iobref = NULL;

	local->transaction.unwind (frame, this);

	AFR_STACK_DESTROY (frame);

	return 0;
}


int
afr_do_writev (call_frame_t *frame, xlator_t *this)
{
        call_frame_t * transaction_frame = NULL;
        afr_local_t *  local             = NULL;

        int op_ret   = -1;
        int op_errno = 0;

        local = frame->local;

	transaction_frame = copy_frame (frame);
	if (!transaction_frame) {
		gf_log (this->name, GF_LOG_ERROR,
			"Out of memory.");
                op_errno = ENOMEM;
		goto out;
	}

	transaction_frame->local = local;
        frame->local = NULL;

	local->op = GF_FOP_WRITE;

        local->success_count      = 0;

	local->transaction.fop    = afr_writev_wind;
	local->transaction.done   = afr_writev_done;
	local->transaction.unwind = afr_writev_unwind;

	local->transaction.main_frame = frame;
	if (local->fd->flags & O_APPEND) {
		local->transaction.start   = 0;
		local->transaction.len     = 0;
	} else {
		local->transaction.start   = local->cont.writev.offset;
		local->transaction.len     = iov_length (local->cont.writev.vector,
                                                         local->cont.writev.count);
	}

	afr_transaction (transaction_frame, this, AFR_DATA_TRANSACTION);

        op_ret = 0;
out:
	if (op_ret == -1) {
		if (transaction_frame)
			AFR_STACK_DESTROY (transaction_frame);
		AFR_STACK_UNWIND (writev, frame, op_ret, op_errno, NULL, NULL);
	}

        return 0;
}


int
afr_writev (call_frame_t *frame, xlator_t *this, fd_t *fd, 
	    struct iovec *vector, int32_t count, off_t offset,
            struct iobref *iobref)
{
	afr_private_t * priv  = NULL;
	afr_local_t   * local = NULL;

	int ret = -1;

	int op_ret   = -1;
	int op_errno = 0;

        uint64_t ctx;
        afr_fd_ctx_t *fd_ctx = NULL;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv = this->private;

	ALLOC_OR_GOTO (local, afr_local_t, out);

	ret = AFR_LOCAL_INIT (local, priv);
	if (ret < 0) {
		op_errno = -ret;
		goto out;
	}

        frame->local = local;

	local->cont.writev.vector     = iov_dup (vector, count);
	local->cont.writev.count      = count;
	local->cont.writev.offset     = offset;
	local->cont.writev.ino        = fd->inode->ino;
        local->cont.writev.iobref     = iobref_ref (iobref);

	local->fd                = fd_ref (fd);

        ret = fd_ctx_get (fd, this, &ctx);
        if (ret < 0) {
                goto out;
        }

        fd_ctx = (afr_fd_ctx_t *)(long) ctx;

        if (fd_ctx->down_count < priv->down_count) {
                local->up_down_flush_cbk = afr_do_writev;
                afr_up_down_flush (frame, this, fd, AFR_CHILD_DOWN_FLUSH);

        } else if (fd_ctx->up_count < priv->up_count) {
                local->up_down_flush_cbk = afr_do_writev;
                afr_up_down_flush (frame, this, fd, AFR_CHILD_UP_FLUSH);

        } else {
                afr_do_writev (frame, this);
        }

	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (writev, frame, op_ret, op_errno, NULL, NULL);
	}

	return 0;
}


/* }}} */

/* {{{ truncate */

int
afr_truncate_unwind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;
	call_frame_t   *main_frame = NULL;

	local = frame->local;
	priv  = this->private;

	LOCK (&frame->lock);
	{
		if (local->transaction.main_frame)
			main_frame = local->transaction.main_frame;
		local->transaction.main_frame = NULL;
	}
	UNLOCK (&frame->lock);

	if (main_frame) {
                local->cont.truncate.prebuf.st_ino  = local->cont.truncate.ino;
                local->cont.truncate.postbuf.st_ino = local->cont.truncate.ino;

                AFR_STACK_UNWIND (truncate, main_frame, local->op_ret,
                                  local->op_errno,
                                  &local->cont.truncate.prebuf,
                                  &local->cont.truncate.postbuf);
        }

	return 0;
}


int
afr_truncate_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
		       int32_t op_ret, int32_t op_errno, struct stat *prebuf,
                       struct stat *postbuf)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int child_index = (long) cookie;
        int read_child  = 0;
	int call_count  = -1;
	int need_unwind = 0;

	local = frame->local;
	priv  = this->private;

        read_child = afr_read_child (this, local->loc.inode);

	LOCK (&frame->lock);
	{
                if (child_index == read_child) {
                        local->read_child_returned = _gf_true;
                }

		if (afr_fop_failed (op_ret, op_errno))
			afr_transaction_fop_failed (frame, this, child_index);

		if (op_ret != -1) {
			if (local->success_count == 0) {
				local->op_ret = op_ret;
				local->cont.truncate.prebuf  = *prebuf;
				local->cont.truncate.postbuf = *postbuf;
			}

                        if (child_index == read_child) {
                                local->cont.truncate.prebuf  = *prebuf;
                                local->cont.truncate.postbuf = *postbuf;
                        }

			local->success_count++;

			if ((local->success_count >= priv->wait_count)
                            && local->read_child_returned) {
				need_unwind = 1;
			}
		}
		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	if (need_unwind)
		local->transaction.unwind (frame, this);

	call_count = afr_frame_return (frame);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
	}
	
	return 0;
}


int32_t
afr_truncate_wind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	
	int call_count = -1;
	int i = 0;

	local = frame->local;
	priv = this->private;

	call_count = afr_up_children_count (priv->child_count, local->child_up);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
		return 0;
	}

	local->call_count = call_count;

	for (i = 0; i < priv->child_count; i++) {				
		if (local->child_up[i]) {
			STACK_WIND_COOKIE (frame, afr_truncate_wind_cbk,
					   (void *) (long) i,	
					   priv->children[i], 
					   priv->children[i]->fops->truncate,
					   &local->loc, 
					   local->cont.truncate.offset);

			if (!--call_count)
				break;
		}
	}
	
	return 0;
}


int
afr_truncate_done (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;

	local = frame->local;

	local->transaction.unwind (frame, this);

	AFR_STACK_DESTROY (frame);

	return 0;
}


int
afr_truncate (call_frame_t *frame, xlator_t *this,
	      loc_t *loc, off_t offset)
{
	afr_private_t * priv  = NULL;
	afr_local_t   * local = NULL;
	call_frame_t   *transaction_frame = NULL;

	int ret = -1;

	int op_ret   = -1;
	int op_errno = 0;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv = this->private;

	transaction_frame = copy_frame (frame);
	if (!transaction_frame) {
		gf_log (this->name, GF_LOG_ERROR,
			"Out of memory.");
		goto out;
	}

	ALLOC_OR_GOTO (local, afr_local_t, out);

	ret = AFR_LOCAL_INIT (local, priv);
	if (ret < 0) {
		op_errno = -ret;
		goto out;
	}

	transaction_frame->local = local;

	local->op_ret = -1;

	local->cont.truncate.offset  = offset;
	local->cont.truncate.ino     = loc->inode->ino;

	local->transaction.fop    = afr_truncate_wind;
	local->transaction.done   = afr_truncate_done;
	local->transaction.unwind = afr_truncate_unwind;

	loc_copy (&local->loc, loc);

	local->transaction.main_frame = frame;
	local->transaction.start   = 0;
	local->transaction.len     = offset;

	afr_transaction (transaction_frame, this, AFR_DATA_TRANSACTION);

	op_ret = 0;
out:
	if (op_ret == -1) {
		if (transaction_frame)
			AFR_STACK_DESTROY (transaction_frame);
		AFR_STACK_UNWIND (truncate, frame, op_ret, op_errno, NULL, NULL);
	}

	return 0;
}


/* }}} */

/* {{{ ftruncate */


int
afr_ftruncate_unwind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;
	call_frame_t   *main_frame = NULL;

	local = frame->local;
	priv  = this->private;

	LOCK (&frame->lock);
	{
		if (local->transaction.main_frame)
			main_frame = local->transaction.main_frame;
		local->transaction.main_frame = NULL;
	}
	UNLOCK (&frame->lock);

	if (main_frame) {
                local->cont.ftruncate.prebuf.st_ino  = local->cont.ftruncate.ino;
                local->cont.ftruncate.postbuf.st_ino = local->cont.ftruncate.ino;

		AFR_STACK_UNWIND (ftruncate, main_frame, local->op_ret,
                                  local->op_errno,
                                  &local->cont.ftruncate.prebuf,
                                  &local->cont.ftruncate.postbuf);
	}
	return 0;
}


int
afr_ftruncate_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
			int32_t op_ret, int32_t op_errno, struct stat *prebuf,
                        struct stat *postbuf)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int child_index = (long) cookie;
	int call_count  = -1;
	int need_unwind = 0;
        int read_child  = 0;

	local = frame->local;
	priv  = this->private;

        read_child = afr_read_child (this, local->fd->inode);

	LOCK (&frame->lock);
	{
                if (child_index == read_child) {
                        local->read_child_returned = _gf_true;
                }

		if (afr_fop_failed (op_ret, op_errno))
			afr_transaction_fop_failed (frame, this, child_index);

		if (op_ret != -1) {
			if (local->success_count == 0) {
				local->op_ret = op_ret;
				local->cont.ftruncate.prebuf  = *prebuf;
				local->cont.ftruncate.postbuf = *postbuf;
			}

                        if (child_index == read_child) {
                                local->cont.ftruncate.prebuf  = *prebuf;
                                local->cont.ftruncate.postbuf = *postbuf;
                        }

			local->success_count++;

			if ((local->success_count >= priv->wait_count)
                            && local->read_child_returned) {
				need_unwind = 1;
			}
		}
		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	if (need_unwind)
		local->transaction.unwind (frame, this);

	call_count = afr_frame_return (frame);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
	}
	
	return 0;
}


int
afr_ftruncate_wind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	
	int call_count = -1;
	int i = 0;

	local = frame->local;
	priv = this->private;

	call_count = afr_up_children_count (priv->child_count, local->child_up);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
		return 0;
	}

	local->call_count = call_count;

	for (i = 0; i < priv->child_count; i++) {				
		if (local->child_up[i]) {
			STACK_WIND_COOKIE (frame, afr_ftruncate_wind_cbk,
					   (void *) (long) i,	
					   priv->children[i], 
					   priv->children[i]->fops->ftruncate,
					   local->fd, local->cont.ftruncate.offset);

			if (!--call_count)
				break;
		}
	}
	
	return 0;
}


int
afr_ftruncate_done (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;

	local = frame->local;

	local->transaction.unwind (frame, this);

	AFR_STACK_DESTROY (frame);

	return 0;
}


int
afr_do_ftruncate (call_frame_t *frame, xlator_t *this)
{
        call_frame_t * transaction_frame = NULL;
        afr_local_t *  local             = NULL;

        int op_ret   = -1;
        int op_errno = 0;

        local = frame->local;

	transaction_frame = copy_frame (frame);
	if (!transaction_frame) {
		gf_log (this->name, GF_LOG_ERROR,
			"Out of memory.");
		goto out;
	}

        transaction_frame->local = local;
        frame->local = NULL;

	local->op = GF_FOP_FTRUNCATE;

	local->transaction.fop    = afr_ftruncate_wind;
	local->transaction.done   = afr_ftruncate_done;
	local->transaction.unwind = afr_ftruncate_unwind;

	local->transaction.main_frame = frame;

	local->transaction.start   = 0;
	local->transaction.len     = local->cont.ftruncate.offset;

	afr_transaction (transaction_frame, this, AFR_DATA_TRANSACTION);

        op_ret = 0;
out:
	if (op_ret == -1) {
		if (transaction_frame)
			AFR_STACK_DESTROY (transaction_frame);
		AFR_STACK_UNWIND (ftruncate, frame, op_ret, op_errno, NULL, NULL);
	}

        return 0;
}


int
afr_ftruncate (call_frame_t *frame, xlator_t *this,
	       fd_t *fd, off_t offset)
{
	afr_private_t * priv  = NULL;
	afr_local_t   * local = NULL;
	call_frame_t   *transaction_frame = NULL;

	int ret = -1;

	int op_ret   = -1;
	int op_errno = 0;

        uint64_t ctx;
        afr_fd_ctx_t *fd_ctx = NULL;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv = this->private;

	ALLOC_OR_GOTO (local, afr_local_t, out);

	ret = AFR_LOCAL_INIT (local, priv);
	if (ret < 0) {
		op_errno = -ret;
		goto out;
	}

        frame->local = local;

	local->cont.ftruncate.offset  = offset;
	local->cont.ftruncate.ino     = fd->inode->ino;

	local->fd = fd_ref (fd);

        ret = fd_ctx_get (fd, this, &ctx);
        if (ret < 0) {
                goto out;
        }

        fd_ctx = (afr_fd_ctx_t *)(long) ctx;

        if (fd_ctx->down_count < priv->down_count) {
                local->up_down_flush_cbk = afr_do_ftruncate;
                afr_up_down_flush (frame, this, fd, AFR_CHILD_DOWN_FLUSH);
        } else {
                afr_do_ftruncate (frame, this);
        }

	op_ret = 0;
out:
	if (op_ret == -1) {
		if (transaction_frame)
			AFR_STACK_DESTROY (transaction_frame);
		AFR_STACK_UNWIND (ftruncate, frame, op_ret, op_errno, NULL, NULL);
	}

	return 0;
}

/* }}} */

/* {{{ setattr */

int
afr_setattr_unwind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;
	call_frame_t   *main_frame = NULL;

	local = frame->local;
	priv  = this->private;

	LOCK (&frame->lock);
	{
		if (local->transaction.main_frame)
			main_frame = local->transaction.main_frame;
		local->transaction.main_frame = NULL;
	}
	UNLOCK (&frame->lock);

	if (main_frame) {
                local->cont.setattr.preop_buf.st_ino  = local->cont.setattr.ino;
                local->cont.setattr.postop_buf.st_ino = local->cont.setattr.ino;

                AFR_STACK_UNWIND (setattr, main_frame, local->op_ret,
                                  local->op_errno,
                                  &local->cont.setattr.preop_buf,
                                  &local->cont.setattr.postop_buf);
        }

	return 0;
}


int
afr_setattr_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno,
                      struct stat *preop, struct stat *postop)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int child_index = (long) cookie;
        int read_child  = 0;
	int call_count  = -1;
	int need_unwind = 0;

	local = frame->local;
	priv  = this->private;

        read_child = afr_read_child (this, local->loc.inode);

	LOCK (&frame->lock);
	{
                if (child_index == read_child) {
                        local->read_child_returned = _gf_true;
                }

		if (afr_fop_failed (op_ret, op_errno))
			afr_transaction_fop_failed (frame, this, child_index);

		if (op_ret != -1) {
			if (local->success_count == 0) {
				local->op_ret = op_ret;
				local->cont.setattr.preop_buf  = *preop;
                                local->cont.setattr.postop_buf = *postop;
			}

                        if (child_index == read_child) {
                                local->cont.setattr.preop_buf  = *preop;
                                local->cont.setattr.postop_buf = *postop;
                        }

			local->success_count++;

			if ((local->success_count >= priv->wait_count)
                            && local->read_child_returned) {
				need_unwind = 1;
			}
		}
		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	if (need_unwind)
		local->transaction.unwind (frame, this);

	call_count = afr_frame_return (frame);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
	}

	return 0;
}


int32_t
afr_setattr_wind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;

	int call_count = -1;
	int i = 0;

	local = frame->local;
	priv = this->private;

	call_count = afr_up_children_count (priv->child_count, local->child_up);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
		return 0;
	}

	local->call_count = call_count;

	for (i = 0; i < priv->child_count; i++) {
		if (local->child_up[i]) {
			STACK_WIND_COOKIE (frame, afr_setattr_wind_cbk,
					   (void *) (long) i,
					   priv->children[i],
					   priv->children[i]->fops->setattr,
					   &local->loc,
					   &local->cont.setattr.in_buf,
                                           local->cont.setattr.valid);

			if (!--call_count)
				break;
		}
	}

	return 0;
}


int
afr_setattr_done (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;

	local = frame->local;

	local->transaction.unwind (frame, this);

	AFR_STACK_DESTROY (frame);

	return 0;
}


int
afr_setattr (call_frame_t *frame, xlator_t *this,
             loc_t *loc, struct stat *buf, int32_t valid)
{
	afr_private_t * priv  = NULL;
	afr_local_t   * local = NULL;
	call_frame_t   *transaction_frame = NULL;

	int ret = -1;

	int op_ret   = -1;
	int op_errno = 0;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv = this->private;

	transaction_frame = copy_frame (frame);
	if (!transaction_frame) {
		gf_log (this->name, GF_LOG_ERROR,
			"Out of memory.");
		goto out;
	}

	ALLOC_OR_GOTO (local, afr_local_t, out);

	ret = AFR_LOCAL_INIT (local, priv);
	if (ret < 0) {
		op_errno = -ret;
		goto out;
	}

	transaction_frame->local = local;

	local->op_ret = -1;

	local->cont.setattr.ino     = loc->inode->ino;

        local->cont.setattr.in_buf = *buf;
        local->cont.setattr.valid  = valid;

	local->transaction.fop    = afr_setattr_wind;
	local->transaction.done   = afr_setattr_done;
	local->transaction.unwind = afr_setattr_unwind;

	loc_copy (&local->loc, loc);

	local->transaction.main_frame = frame;
	local->transaction.start   = LLONG_MAX - 1;
	local->transaction.len     = 0;

	afr_transaction (transaction_frame, this, AFR_METADATA_TRANSACTION);

	op_ret = 0;
out:
	if (op_ret == -1) {
		if (transaction_frame)
			AFR_STACK_DESTROY (transaction_frame);
		AFR_STACK_UNWIND (setattr, frame, op_ret, op_errno, NULL, NULL);
	}

	return 0;
}

/* {{{ fsetattr */

int
afr_fsetattr_unwind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;
	call_frame_t   *main_frame = NULL;

	local = frame->local;
	priv  = this->private;

	LOCK (&frame->lock);
	{
		if (local->transaction.main_frame)
			main_frame = local->transaction.main_frame;
		local->transaction.main_frame = NULL;
	}
	UNLOCK (&frame->lock);

	if (main_frame) {
                local->cont.fsetattr.preop_buf.st_ino  =
                        local->cont.fsetattr.ino;
                local->cont.fsetattr.postop_buf.st_ino =
                        local->cont.fsetattr.ino;

                AFR_STACK_UNWIND (fsetattr, main_frame, local->op_ret,
                                  local->op_errno,
                                  &local->cont.fsetattr.preop_buf,
                                  &local->cont.fsetattr.postop_buf);
        }

	return 0;
}


int
afr_fsetattr_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno,
                      struct stat *preop, struct stat *postop)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int child_index = (long) cookie;
        int read_child  = 0;
	int call_count  = -1;
	int need_unwind = 0;

	local = frame->local;
	priv  = this->private;

        read_child = afr_read_child (this, local->loc.inode);

	LOCK (&frame->lock);
	{
                if (child_index == read_child) {
                        local->read_child_returned = _gf_true;
                }

		if (afr_fop_failed (op_ret, op_errno))
			afr_transaction_fop_failed (frame, this, child_index);

		if (op_ret != -1) {
			if (local->success_count == 0) {
				local->op_ret = op_ret;
				local->cont.fsetattr.preop_buf  = *preop;
                                local->cont.fsetattr.postop_buf = *postop;
			}

                        if (child_index == read_child) {
                                local->cont.fsetattr.preop_buf  = *preop;
                                local->cont.fsetattr.postop_buf = *postop;
                        }

			local->success_count++;

			if ((local->success_count >= priv->wait_count)
                            && local->read_child_returned) {
				need_unwind = 1;
			}
		}
		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	if (need_unwind)
		local->transaction.unwind (frame, this);

	call_count = afr_frame_return (frame);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
	}

	return 0;
}


int32_t
afr_fsetattr_wind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;

	int call_count = -1;
	int i = 0;

	local = frame->local;
	priv = this->private;

	call_count = afr_up_children_count (priv->child_count, local->child_up);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
		return 0;
	}

	local->call_count = call_count;

	for (i = 0; i < priv->child_count; i++) {
		if (local->child_up[i]) {
			STACK_WIND_COOKIE (frame, afr_fsetattr_wind_cbk,
					   (void *) (long) i,
					   priv->children[i],
					   priv->children[i]->fops->fsetattr,
					   local->fd,
					   &local->cont.fsetattr.in_buf,
                                           local->cont.fsetattr.valid);

			if (!--call_count)
				break;
		}
	}

	return 0;
}


int
afr_fsetattr_done (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;

	local = frame->local;

	local->transaction.unwind (frame, this);

	AFR_STACK_DESTROY (frame);

	return 0;
}


int
afr_fsetattr (call_frame_t *frame, xlator_t *this,
              fd_t *fd, struct stat *buf, int32_t valid)
{
	afr_private_t * priv  = NULL;
	afr_local_t   * local = NULL;
	call_frame_t   *transaction_frame = NULL;

	int ret = -1;

	int op_ret   = -1;
	int op_errno = 0;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv = this->private;

	transaction_frame = copy_frame (frame);
	if (!transaction_frame) {
		gf_log (this->name, GF_LOG_ERROR,
			"Out of memory.");
		goto out;
	}

	ALLOC_OR_GOTO (local, afr_local_t, out);

	ret = AFR_LOCAL_INIT (local, priv);
	if (ret < 0) {
		op_errno = -ret;
		goto out;
	}

	transaction_frame->local = local;

	local->op_ret = -1;

	local->cont.fsetattr.ino     = fd->inode->ino;

        local->cont.fsetattr.in_buf = *buf;
        local->cont.fsetattr.valid  = valid;

	local->transaction.fop    = afr_fsetattr_wind;
	local->transaction.done   = afr_fsetattr_done;
	local->transaction.unwind = afr_fsetattr_unwind;

        local->fd                 = fd_ref (fd);

	local->transaction.main_frame = frame;
	local->transaction.start   = LLONG_MAX - 1;
	local->transaction.len     = 0;

	afr_transaction (transaction_frame, this, AFR_METADATA_TRANSACTION);

	op_ret = 0;
out:
	if (op_ret == -1) {
		if (transaction_frame)
			AFR_STACK_DESTROY (transaction_frame);
		AFR_STACK_UNWIND (fsetattr, frame, op_ret, op_errno, NULL, NULL);
	}

	return 0;
}


/* {{{ setxattr */


int
afr_setxattr_unwind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;
	call_frame_t   *main_frame = NULL;

	local = frame->local;
	priv  = this->private;

	LOCK (&frame->lock);
	{
		if (local->transaction.main_frame)
			main_frame = local->transaction.main_frame;
		local->transaction.main_frame = NULL;
	}
	UNLOCK (&frame->lock);

	if (main_frame) {
		AFR_STACK_UNWIND (setxattr, main_frame,
                                  local->op_ret, local->op_errno)
	}
	return 0;
}


int
afr_setxattr_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
		       int32_t op_ret, int32_t op_errno)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int call_count  = -1;
	int need_unwind = 0;

	local = frame->local;
	priv = this->private;

	LOCK (&frame->lock);
	{
		if (op_ret != -1) {
			if (local->success_count == 0) {
				local->op_ret = op_ret;
			}
			local->success_count++;

			if (local->success_count == priv->wait_count) {
				need_unwind = 1;
			}
		}

		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	if (need_unwind)
		local->transaction.unwind (frame, this);

	call_count = afr_frame_return (frame);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
	}
	
	return 0;
}


int
afr_setxattr_wind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;

	int call_count = -1;
	int i = 0;

	local = frame->local;
	priv = this->private;

	call_count = afr_up_children_count (priv->child_count, local->child_up);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
		return 0;
	}

	local->call_count = call_count;

	for (i = 0; i < priv->child_count; i++) {				
		if (local->child_up[i]) {
			STACK_WIND_COOKIE (frame, afr_setxattr_wind_cbk,
					   (void *) (long) i,	
					   priv->children[i], 
					   priv->children[i]->fops->setxattr,
					   &local->loc, 
					   local->cont.setxattr.dict,
					   local->cont.setxattr.flags); 

			if (!--call_count)
				break;
		}
	}
	
	return 0;
}


int
afr_setxattr_done (call_frame_t *frame, xlator_t *this)
{
	afr_local_t * local = frame->local;

	local->transaction.unwind (frame, this);

	AFR_STACK_DESTROY (frame);
	
	return 0;
}


int
afr_setxattr (call_frame_t *frame, xlator_t *this,
	      loc_t *loc, dict_t *dict, int32_t flags)
{
	afr_private_t * priv  = NULL;
	afr_local_t   * local = NULL;
	call_frame_t   *transaction_frame = NULL;

	int ret = -1;

	int op_ret   = -1;
	int op_errno = 0;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv = this->private;

	transaction_frame = copy_frame (frame);
	if (!transaction_frame) {
		gf_log (this->name, GF_LOG_ERROR,
			"Out of memory.");
		goto out;
	}

	ALLOC_OR_GOTO (local, afr_local_t, out);

	ret = AFR_LOCAL_INIT (local, priv);
	if (ret < 0) {
		op_errno = -ret;
		goto out;
	}

	transaction_frame->local = local;

	local->op_ret = -1;

	local->cont.setxattr.dict  = dict_ref (dict);
	local->cont.setxattr.flags = flags;

	local->transaction.fop    = afr_setxattr_wind;
	local->transaction.done   = afr_setxattr_done;
	local->transaction.unwind = afr_setxattr_unwind;

	loc_copy (&local->loc, loc);

	local->transaction.main_frame = frame;
	local->transaction.start   = LLONG_MAX - 1;
	local->transaction.len     = 0;

	afr_transaction (transaction_frame, this, AFR_METADATA_TRANSACTION);

	op_ret = 0;
out:
	if (op_ret == -1) {
		if (transaction_frame)
			AFR_STACK_DESTROY (transaction_frame);
		AFR_STACK_UNWIND (setxattr, frame, op_ret, op_errno);
	}

	return 0;
}

/* }}} */

/* {{{ removexattr */


int
afr_removexattr_unwind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;
	call_frame_t   *main_frame = NULL;

	local = frame->local;
	priv  = this->private;

	LOCK (&frame->lock);
	{
		if (local->transaction.main_frame)
			main_frame = local->transaction.main_frame;
		local->transaction.main_frame = NULL;
	}
	UNLOCK (&frame->lock);

	if (main_frame) {
		AFR_STACK_UNWIND (removexattr, main_frame,
                                  local->op_ret, local->op_errno)
	}
	return 0;
}


int
afr_removexattr_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
			  int32_t op_ret, int32_t op_errno)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int call_count  = -1;
	int need_unwind = 0;

	local = frame->local;
	priv = this->private;

	LOCK (&frame->lock);
	{
		if (op_ret != -1) {
			if (local->success_count == 0) {
				local->op_ret = op_ret;
			}
			local->success_count++;

			if (local->success_count == priv->wait_count) {
				need_unwind = 1;
			}
		}

		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	if (need_unwind)
		local->transaction.unwind (frame, this);

	call_count = afr_frame_return (frame);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
	}
	
	return 0;
}


int32_t
afr_removexattr_wind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;

	int call_count = -1;
	int i = 0;

	local = frame->local;
	priv = this->private;

	call_count = afr_up_children_count (priv->child_count, local->child_up);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
		return 0;
	}

	local->call_count = call_count;

	for (i = 0; i < priv->child_count; i++) {				
		if (local->child_up[i]) {
			STACK_WIND_COOKIE (frame, afr_removexattr_wind_cbk, 
					   (void *) (long) i,	
					   priv->children[i], 
					   priv->children[i]->fops->removexattr,
					   &local->loc, 
					   local->cont.removexattr.name);

			if (!--call_count)
				break;
		}
	}
	
	return 0;
}


int
afr_removexattr_done (call_frame_t *frame, xlator_t *this)
{
	afr_local_t * local = frame->local;

	local->transaction.unwind (frame, this);

	AFR_STACK_DESTROY (frame);
	
	return 0;
}


int
afr_removexattr (call_frame_t *frame, xlator_t *this,
		 loc_t *loc, const char *name)
{
	afr_private_t * priv  = NULL;
	afr_local_t   * local = NULL;
	call_frame_t   *transaction_frame = NULL;

	int ret = -1;

	int op_ret   = -1;
	int op_errno = 0;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);
	VALIDATE_OR_GOTO (loc, out);

	priv = this->private;

	transaction_frame = copy_frame (frame);
	if (!transaction_frame) {
		gf_log (this->name, GF_LOG_ERROR,
			"Out of memory.");
		goto out;
	}

	ALLOC_OR_GOTO (local, afr_local_t, out);

	ret = AFR_LOCAL_INIT (local, priv);
	if (ret < 0) {
		op_errno = -ret;
		goto out;
	}

	transaction_frame->local = local;

	local->op_ret = -1;

	local->cont.removexattr.name = strdup (name);

	local->transaction.fop    = afr_removexattr_wind;
	local->transaction.done   = afr_removexattr_done;
	local->transaction.unwind = afr_removexattr_unwind;

	loc_copy (&local->loc, loc);

	local->transaction.main_frame = frame;
	local->transaction.start   = LLONG_MAX - 1;
	local->transaction.len     = 0;

	afr_transaction (transaction_frame, this, AFR_METADATA_TRANSACTION);

	op_ret = 0;
out:
	if (op_ret == -1) {
		if (transaction_frame)
			AFR_STACK_DESTROY (transaction_frame);
		AFR_STACK_UNWIND (removexattr, frame, op_ret, op_errno);
	}

	return 0;
}
