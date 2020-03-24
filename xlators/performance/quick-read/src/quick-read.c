/*
  Copyright (c) 2009-2010 Gluster, Inc. <http://www.gluster.com>
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

#include "quick-read.h"
#include "statedump.h"

int32_t
qr_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
          off_t offset);


static void
qr_loc_wipe (loc_t *loc)
{
        if (loc == NULL) {
                goto out;
        }

        if (loc->path) {
                FREE (loc->path);
                loc->path = NULL;
        }

        if (loc->inode) {
                inode_unref (loc->inode);
                loc->inode = NULL;
        }

        if (loc->parent) {
                inode_unref (loc->parent);
                loc->parent = NULL;
        }

out:
        return;
}


static int32_t
qr_loc_fill (loc_t *loc, inode_t *inode, char *path)
{
        int32_t  ret = -1;
        char    *parent = NULL;

        if ((loc == NULL) || (inode == NULL) || (path == NULL)) {
                ret = -1;
                errno = EINVAL;
                goto out;
        }

        loc->inode = inode_ref (inode);
        loc->path = strdup (path);
        loc->ino = inode->ino;

        parent = strdup (path);
        if (parent == NULL) {
                ret = -1;
                goto out;
        }

        parent = dirname (parent);

        loc->parent = inode_from_path (inode->table, parent);
        if (loc->parent == NULL) {
                ret = -1;
                errno = EINVAL;
                goto out;
        }

        loc->name = strrchr (loc->path, '/');
        ret = 0;
out:
        if (ret == -1) {
                qr_loc_wipe (loc);

        }

        if (parent) {
                FREE (parent);
        }
        
        return ret;
}


void
qr_resume_pending_ops (qr_fd_ctx_t *qr_fd_ctx)
{
        struct list_head  waiting_ops;
        call_stub_t      *stub = NULL, *tmp = NULL;  
        
        if (qr_fd_ctx == NULL) {
                goto out;
        }

        INIT_LIST_HEAD (&waiting_ops);

        LOCK (&qr_fd_ctx->lock);
        {
                list_splice_init (&qr_fd_ctx->waiting_ops,
                                  &waiting_ops);
        }
        UNLOCK (&qr_fd_ctx->lock);

        if (!list_empty (&waiting_ops)) {
                list_for_each_entry_safe (stub, tmp, &waiting_ops, list) {
                        list_del_init (&stub->list);
                        call_resume (stub);
                }
        }

out:
        return;
}


static void
qr_fd_ctx_free (qr_fd_ctx_t *qr_fd_ctx)
{
        if (qr_fd_ctx == NULL) {
                goto out;
        }

        assert (list_empty (&qr_fd_ctx->waiting_ops));

        FREE (qr_fd_ctx->path);
        FREE (qr_fd_ctx);

out:
        return;
}

        
int32_t
qr_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, inode_t *inode,
               struct stat *buf, dict_t *dict, struct stat *postparent)
{
        data_t    *content = NULL;
        qr_file_t *qr_file = NULL;
        uint64_t   value = 0;
        int        ret = -1;
        qr_conf_t *conf = NULL;

        if ((op_ret == -1) || (dict == NULL)) {
                goto out;
        }

        conf = this->private;

        if (buf->st_size > conf->max_file_size) {
                goto out;
        }

        if (S_ISDIR (buf->st_mode)) {
                goto out;
        }

        if (inode == NULL) {
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        content = dict_get (dict, GLUSTERFS_CONTENT_KEY);

        LOCK (&inode->lock);
        {
                ret = __inode_ctx_get (inode, this, &value);
                if (ret == -1) {
                        qr_file = CALLOC (1, sizeof (*qr_file));
                        if (qr_file == NULL) {
                                op_ret = -1;
                                op_errno = ENOMEM;
                                goto unlock;
                        }
                
                        LOCK_INIT (&qr_file->lock);
                        ret = __inode_ctx_put (inode, this,
                                               (uint64_t)(long)qr_file);
                        if (ret == -1) {
                                FREE (qr_file);
                                qr_file = NULL;
                                op_ret = -1;
                                op_errno = EINVAL;
                        }
                } else {
                        qr_file = (qr_file_t *)(long)value;
                        if (qr_file == NULL) {
                                op_ret = -1;
                                op_errno = EINVAL;
                        }
                }
        }
unlock:
        UNLOCK (&inode->lock);

        if (qr_file != NULL) {
                LOCK (&qr_file->lock);
                {
                        if (qr_file->xattr
                            && (qr_file->stbuf.st_mtime != buf->st_mtime)) {
                                dict_unref (qr_file->xattr);
                                qr_file->xattr = NULL;
                        }

                        if (content) {
                                if (qr_file->xattr) {
                                        dict_unref (qr_file->xattr);
                                        qr_file->xattr = NULL;
                                }

                                qr_file->xattr = dict_ref (dict);
                                qr_file->stbuf = *buf;
                        }

                        gettimeofday (&qr_file->tv, NULL);
                }
                UNLOCK (&qr_file->lock);
        }

out:
        /*
         * FIXME: content size in dict can be greater than the size application 
         * requested for. Applications need to be careful till this is fixed.
         */
	STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno, inode, buf, dict,
                             postparent);
        return 0;
}


int32_t
qr_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xattr_req)
{
        qr_conf_t *conf = NULL;
        dict_t    *new_req_dict = NULL;
        int32_t    op_ret = -1, op_errno = -1;
        data_t    *content = NULL; 
        uint64_t   requested_size = 0, size = 0, value = 0; 
        char       cached = 0;
        qr_file_t *qr_file = NULL; 

        conf = this->private;
        if (conf == NULL) {
                op_ret = -1;
                op_errno = EINVAL;
                goto unwind;
        }

        op_ret = inode_ctx_get (loc->inode, this, &value);
        if (op_ret == 0) {
                qr_file = (qr_file_t *)(long)value;
        }

        if (qr_file != NULL) {
                LOCK (&qr_file->lock);
                {
                        if (qr_file->xattr) {
                                cached = 1;
                        }
                }
                UNLOCK (&qr_file->lock);
        }

        if ((xattr_req == NULL) && (conf->max_file_size > 0)) {
                new_req_dict = xattr_req = dict_new ();
                if (xattr_req == NULL) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                        gf_log (this->name, GF_LOG_ERROR, "out of memory");
                        goto unwind;
                }
        }

        if (!cached) {
                if (xattr_req) {
                        content = dict_get (xattr_req, GLUSTERFS_CONTENT_KEY);
                        if (content) {
                                requested_size = data_to_uint64 (content);
                        }
                }

                if ((conf->max_file_size > 0)
                    && (conf->max_file_size != requested_size)) {
                        size = (conf->max_file_size > requested_size) ?
                                conf->max_file_size : requested_size;

                        op_ret = dict_set (xattr_req, GLUSTERFS_CONTENT_KEY,
                                           data_from_uint64 (size));
                        if (op_ret < 0) {
                                op_ret = -1;
                                op_errno = ENOMEM;
                                goto unwind;
                        }
                }
        }

	STACK_WIND (frame, qr_lookup_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup, loc, xattr_req);

        if (new_req_dict) {
                dict_unref (new_req_dict);
        }

        return 0;

unwind:
        STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno, NULL, NULL, NULL,
                             NULL);

        if (new_req_dict) {
                dict_unref (new_req_dict);
        }

        return 0;
}


int32_t
qr_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
             int32_t op_errno, fd_t *fd)
{
        uint64_t         value = 0;
        int32_t          ret = -1;
        struct list_head waiting_ops;
        qr_local_t      *local = NULL;
        qr_file_t       *qr_file = NULL;
        qr_fd_ctx_t     *qr_fd_ctx = NULL;
        call_stub_t     *stub = NULL, *tmp = NULL;
        char             is_open = 0;

        local = frame->local;
        if (local != NULL) {
                local->op_ret = op_ret;
                local->op_errno = op_errno;
                is_open = local->is_open;
        }

        INIT_LIST_HEAD (&waiting_ops);

        ret = fd_ctx_get (fd, this, &value);
        if ((ret == -1) && (op_ret != -1)) {
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        if (value) {
                qr_fd_ctx = (qr_fd_ctx_t *) (long)value;
        }

        if (qr_fd_ctx) {
                LOCK (&qr_fd_ctx->lock);
                {
                        qr_fd_ctx->open_in_transit = 0;

                        if (op_ret == 0) {
                                qr_fd_ctx->opened = 1;
                        }
                        list_splice_init (&qr_fd_ctx->waiting_ops,
                                          &waiting_ops);
                }
                UNLOCK (&qr_fd_ctx->lock);

                if (local && local->is_open
                    && ((local->open_flags & O_TRUNC) == O_TRUNC)) { 
                        ret = inode_ctx_get (fd->inode, this, &value);
                        if (ret == 0) {
                                qr_file = (qr_file_t *)(long) value;

                                if (qr_file) {
                                        LOCK (&qr_file->lock);
                                        {
                                                dict_unref (qr_file->xattr);
                                                qr_file->xattr = NULL;
                                        }
                                        UNLOCK (&qr_file->lock);
                                }
                        }
                }

                if (!list_empty (&waiting_ops)) {
                        list_for_each_entry_safe (stub, tmp, &waiting_ops,
                                                  list) {
                                list_del_init (&stub->list);
                                call_resume (stub);
                        }
                }
        }
out: 
        if (is_open) {
                STACK_UNWIND_STRICT (open, frame, op_ret, op_errno, fd);
        }

        return 0;
}


int32_t
qr_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
         fd_t *fd, int32_t wbflags)
{
        qr_file_t   *qr_file = NULL;
        int32_t      ret = -1;
        uint64_t     filep = 0;
        char         content_cached = 0;
        qr_fd_ctx_t *qr_fd_ctx = NULL, *tmp_fd_ctx = NULL;
        int32_t      op_ret = -1, op_errno = -1;
        qr_local_t  *local = NULL;
        qr_conf_t   *conf = NULL;

        conf = this->private;

        tmp_fd_ctx = qr_fd_ctx = CALLOC (1, sizeof (*qr_fd_ctx));
        if (qr_fd_ctx == NULL) {
                op_ret = -1;
                op_errno = ENOMEM;
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                goto unwind;
        }

        LOCK_INIT (&qr_fd_ctx->lock);
        INIT_LIST_HEAD (&qr_fd_ctx->waiting_ops);

        qr_fd_ctx->path = strdup (loc->path);
        qr_fd_ctx->flags = flags;

        ret = fd_ctx_set (fd, this, (uint64_t)(long)qr_fd_ctx);
        if (ret == -1) {
                op_ret = -1;
                op_errno = EINVAL;
                goto unwind;
        }
        tmp_fd_ctx = NULL;

        local = CALLOC (1, sizeof (*local));
        if (local == NULL) {
                op_ret = -1;
                op_errno = ENOMEM;
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                goto unwind;
        }

        local->is_open = 1;
        local->open_flags = flags; 
        frame->local = local;
        local = NULL;

        ret = inode_ctx_get (fd->inode, this, &filep);
        if (ret == 0) {
                qr_file = (qr_file_t *)(long) filep;
                if (qr_file) {
                        LOCK (&qr_file->lock);
                        {
                                if (qr_file->xattr) {
                                        content_cached = 1;
                                }
                        }
                        UNLOCK (&qr_file->lock);
                }
        }

        if (content_cached && ((flags & O_DIRECTORY) == O_DIRECTORY)) {
                op_ret = -1;
                op_errno = ENOTDIR;
                goto unwind;
        }

        if (!content_cached || ((flags & O_WRONLY) == O_WRONLY) 
            || ((flags & O_TRUNC) == O_TRUNC)) {
                LOCK (&qr_fd_ctx->lock);
                {
                        /*
                         * we really need not set this flag, since open is 
                         * not yet unwounded.
                         */
                           
                        qr_fd_ctx->open_in_transit = 1;
                }
                UNLOCK (&qr_fd_ctx->lock);
                goto wind;
        } else {
                op_ret = 0;
                op_errno = 0;
                goto unwind;
        }

unwind:
        if (tmp_fd_ctx != NULL) {
                qr_fd_ctx_free (tmp_fd_ctx);
        }

        if (local != NULL) {
                FREE (local);
        }

        STACK_UNWIND_STRICT (open, frame, op_ret, op_errno, fd);
        return 0;

wind:
        STACK_WIND (frame, qr_open_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->open, loc, flags, fd, wbflags);
        return 0;
}


static inline char
qr_time_elapsed (struct timeval *now, struct timeval *then)
{
        return now->tv_sec - then->tv_sec;
}


static inline char
qr_need_validation (qr_conf_t *conf, qr_file_t *file)
{
        struct timeval now = {0, };
        char           need_validation = 0;
        
        gettimeofday (&now, NULL);

        if (qr_time_elapsed (&now, &file->tv) >= conf->cache_timeout)
                need_validation = 1;

        return need_validation;
}


static int32_t
qr_validate_cache_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, struct stat *buf)
{
        qr_file_t  *qr_file = NULL;
        qr_local_t *local = NULL;
        uint64_t    value = 0;
        int32_t     ret = 0;

        local = frame->local; 
        if ((local == NULL) || ((local->fd) == NULL)) {
                op_ret = -1;
                op_errno = EINVAL;
                goto unwind;
        }
         
        if (op_ret == -1) {
                goto unwind;
        }

        ret = inode_ctx_get (local->fd->inode, this, &value);
        if (ret == -1) {
                op_ret = -1;
                op_errno = EINVAL;
                goto unwind;
        }

        qr_file = (qr_file_t *)(long) value;
        if (qr_file == NULL) {
                op_ret = -1;
                op_errno = EINVAL;
                goto unwind;
        }

        LOCK (&qr_file->lock);
        {
                if (qr_file->stbuf.st_mtime != buf->st_mtime) {
                        dict_unref (qr_file->xattr);
                        qr_file->xattr = NULL;
                }

                gettimeofday (&qr_file->tv, NULL);
        }
        UNLOCK (&qr_file->lock);

        frame->local = NULL;

        call_resume (local->stub);

        FREE (local);
        return 0;

unwind:
        if (local && local->stub) {
                call_stub_destroy (local->stub);
                local->stub = NULL;
        }

        /* this is actually unwind of readv */
        STACK_UNWIND_STRICT (readv, frame, op_ret, op_errno, NULL, -1, NULL,
                             NULL);
        return 0;
}


int32_t
qr_validate_cache_helper (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
        qr_local_t *local = NULL;
        int32_t     op_ret = -1, op_errno = -1;

        local = frame->local;
        if (local == NULL) {
                op_ret = -1;
                op_errno = EINVAL;
        } else {
                op_ret = local->op_ret;
                op_errno = local->op_errno;
        }

        if (op_ret == -1) {
                qr_validate_cache_cbk (frame, NULL, this, op_ret, op_errno,
                                       NULL);
        } else {
                STACK_WIND (frame, qr_validate_cache_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->fstat, fd);
        }

        return 0;
}


int
qr_validate_cache (call_frame_t *frame, xlator_t *this, fd_t *fd,
                   call_stub_t *stub)
{
        int          ret = -1;
        int          flags = 0;
        uint64_t     value = 0; 
        loc_t        loc = {0, };
        char        *path = NULL;
        qr_local_t  *local = NULL;
        qr_fd_ctx_t *qr_fd_ctx = NULL;
        call_stub_t *validate_stub = NULL;
        char         need_open = 0, can_wind = 0;

        local = CALLOC (1, sizeof (*local));
        if (local == NULL) {
                goto out;
        }

        local->fd = fd;
        local->stub = stub;
        frame->local = local;

        ret = fd_ctx_get (fd, this, &value);
        if (ret == 0) {
                qr_fd_ctx = (qr_fd_ctx_t *)(long) value;
        }

        if (qr_fd_ctx) {
                LOCK (&qr_fd_ctx->lock);
                {
                        path = qr_fd_ctx->path;
                        flags = qr_fd_ctx->flags;

                        if (!(qr_fd_ctx->opened
                              || qr_fd_ctx->open_in_transit)) {
                                need_open = 1;
                                qr_fd_ctx->open_in_transit = 1;
                        } 

                        if (qr_fd_ctx->opened) {
                                can_wind = 1;
                        } else {
                                validate_stub = fop_fstat_stub (frame,
                                                                qr_validate_cache_helper,
                                                                fd);
                                if (validate_stub == NULL) {
                                        ret = -1;
                                        qr_fd_ctx->open_in_transit = 0;
                                        goto unlock;
                                }
                                
                                list_add_tail (&validate_stub->list,
                                               &qr_fd_ctx->waiting_ops);
                        } 
                }
        unlock:
                UNLOCK (&qr_fd_ctx->lock);

                if (ret == -1) {
                        goto out;
                }
        } else {
                can_wind = 1;
        }

        if (need_open) {
                ret = qr_loc_fill (&loc, fd->inode, path);
                if (ret == -1) {
                        qr_resume_pending_ops (qr_fd_ctx);
                        goto out;
                }

                STACK_WIND (frame, qr_open_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open,
                            &loc, flags, fd, 0);
                        
                qr_loc_wipe (&loc);
        } else if (can_wind) {
                STACK_WIND (frame, qr_validate_cache_cbk,
                            FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->fstat, fd);
        }

        ret = 0;
out:
        return ret; 
}


int32_t
qr_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, struct iovec *vector, int32_t count,
              struct stat *stbuf, struct iobref *iobref)
{
	STACK_UNWIND_STRICT (readv, frame, op_ret, op_errno, vector, count,
                             stbuf, iobref);
	return 0;
}


int32_t
qr_readv_helper (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                 off_t offset)
{
        STACK_WIND (frame, qr_readv_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->readv, fd, size, offset);
        return 0;
}


int32_t
qr_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
          off_t offset)
{
        qr_file_t         *file = NULL;
        int32_t            ret = -1, op_ret = -1, op_errno = -1;
        uint64_t           value = 0;
        int                count = -1, flags = 0, i = 0;
        char               content_cached = 0, need_validation = 0;
        char               need_open = 0, can_wind = 0, need_unwind = 0;
        struct iobuf      *iobuf = NULL;
        struct iobref     *iobref = NULL; 
        struct stat        stbuf = {0, }; 
        data_t            *content = NULL;
        qr_fd_ctx_t       *qr_fd_ctx = NULL; 
        call_stub_t       *stub = NULL;
        loc_t              loc = {0, };
        qr_conf_t         *conf = NULL;
        struct iovec      *vector = NULL;
        char              *path = NULL;
        glusterfs_ctx_t   *ctx = NULL;
        off_t              start = 0, end = 0;
        size_t             len = 0;
        struct iobuf_pool *iobuf_pool = NULL; 

        op_ret = 0;
        conf = this->private;

        ret = fd_ctx_get (fd, this, &value);
        if (ret == 0) {
                qr_fd_ctx = (qr_fd_ctx_t *)(long) value;
        }

        iobuf_pool = this->ctx->iobuf_pool;

        ret = inode_ctx_get (fd->inode, this, &value);
        if (ret == 0) {
                file = (qr_file_t *)(long)value;
                if (file) {
                        LOCK (&file->lock);
                        {
                                if (file->xattr){
                                        if (qr_need_validation (conf,file)) {
                                                need_validation = 1;
                                                goto unlock;
                                        }

                                        content = dict_get (file->xattr,
                                                            GLUSTERFS_CONTENT_KEY);

                                        
                                        stbuf = file->stbuf;
                                        content_cached = 1;

                                        if (offset > content->len) {
                                                op_ret = 0;
                                                end = content->len;
                                        } else {
                                                if ((offset + size)
                                                    > content->len) {
                                                        op_ret = content->len - offset;
                                                        end = content->len;
                                                } else {
                                                        op_ret = size;
                                                        end =  offset + size;
                                                }
                                        }

                                        ctx = this->ctx;
                                        count = (op_ret / iobuf_pool->page_size);
                                        if ((op_ret % iobuf_pool->page_size)
                                            != 0) {
                                                count++;
                                        }
 
                                        if (count == 0) {
                                                op_ret = 0;
                                                goto unlock;
                                        }

                                        vector = CALLOC (count,
                                                         sizeof (*vector));
                                        if (vector == NULL) {
                                                op_ret = -1;
                                                op_errno = ENOMEM;
                                                need_unwind = 1;
                                                goto unlock;
                                        }

                                        iobref = iobref_new ();
                                        if (iobref == NULL) {
                                                op_ret = -1;
                                                op_errno = ENOMEM;
                                                need_unwind = 1;
                                                goto unlock;
                                        }

                                        for (i = 0; i < count; i++) {
                                                iobuf = iobuf_get (iobuf_pool);
                                                if (iobuf == NULL) {
                                                        op_ret = -1;
                                                        op_errno = ENOMEM;
                                                        need_unwind = 1;
                                                        goto unlock;
                                                }
                                        
                                                start = offset + (iobuf_pool->page_size * i);
                                                if (start > end) {
                                                        len = 0;
                                                } else {
                                                        len = (iobuf_pool->page_size
                                                               > (end - start))
                                                                ? (end - start)
                                                                : iobuf_pool->page_size;

                                                        memcpy (iobuf->ptr,
                                                                content->data + start,
                                                                len);
                                                }

                                                iobref_add (iobref, iobuf);
                                                iobuf_unref (iobuf);

                                                vector[i].iov_base = iobuf->ptr;
                                                vector[i].iov_len = len;
                                        }
                                }
                        }
                unlock:
                        UNLOCK (&file->lock);
                }
        }

out:
        if (content_cached || need_unwind) {
                STACK_UNWIND_STRICT (readv, frame, op_ret, op_errno, vector,
                                     count, &stbuf, iobref);

        } else if (need_validation) {
                stub = fop_readv_stub (frame, qr_readv, fd, size, offset);
                if (stub == NULL) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                        goto out;
                }

                op_ret = qr_validate_cache (frame, this, fd, stub);
                if (op_ret == -1) {
                        need_unwind = 1;
                        op_errno = errno;
                        call_stub_destroy (stub);
                        goto out;
                }
        } else {
                if (qr_fd_ctx) {
                        LOCK (&qr_fd_ctx->lock);
                        {
                                path = qr_fd_ctx->path;
                                flags = qr_fd_ctx->flags;

                                if (!(qr_fd_ctx->opened
                                      || qr_fd_ctx->open_in_transit)) {
                                        need_open = 1;
                                        qr_fd_ctx->open_in_transit = 1;
                                } 

                                if (qr_fd_ctx->opened) {
                                        can_wind = 1;
                                } else {
                                        stub = fop_readv_stub (frame,
                                                               qr_readv_helper,
                                                               fd, size,
                                                               offset);
                                        if (stub == NULL) {
                                                op_ret = -1;
                                                op_errno = ENOMEM;
                                                need_unwind = 1;
                                                qr_fd_ctx->open_in_transit = 0;
                                                goto fdctx_unlock;
                                        }
                                
                                        list_add_tail (&stub->list,
                                                       &qr_fd_ctx->waiting_ops);
                                } 
                        }
                fdctx_unlock:
                        UNLOCK (&qr_fd_ctx->lock);
                        
                        if (op_ret == -1) {
                                need_unwind = 1;
                                goto out;
                        }
                } else {
                        can_wind = 1;
                }

                if (need_open) {
                        op_ret = qr_loc_fill (&loc, fd->inode, path);
                        if (op_ret == -1) {
                                qr_resume_pending_ops (qr_fd_ctx);
                                goto out;
                        }

                        STACK_WIND (frame, qr_open_cbk, FIRST_CHILD(this),
                                    FIRST_CHILD(this)->fops->open,
                                    &loc, flags, fd, 0);
                        
                        qr_loc_wipe (&loc);
                } else if (can_wind) {
                        STACK_WIND (frame, qr_readv_cbk,
                                    FIRST_CHILD (this),
                                    FIRST_CHILD (this)->fops->readv, fd, size,
                                    offset);
                }

        }

        if (vector) {
                FREE (vector);
        }

        if (iobref) {
                iobref_unref (iobref);
        }

        return 0;
}


int32_t
qr_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct stat *prebuf,
               struct stat *postbuf)
{
	STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, prebuf, postbuf);
	return 0;
}


int32_t
qr_writev_helper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                  struct iovec *vector, int32_t count, off_t off,
                  struct iobref *iobref)
{
        STACK_WIND (frame, qr_writev_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->writev, fd, vector, count, off,
                    iobref);
        return 0;
}


int32_t
qr_writev (call_frame_t *frame, xlator_t *this, fd_t *fd, struct iovec *vector,
           int32_t count, off_t off, struct iobref *iobref)
{
        uint64_t     value = 0;
        int          flags = 0;
        call_stub_t *stub = NULL; 
        char        *path = NULL;
        loc_t        loc = {0, };
        qr_file_t   *qr_file = NULL;
        qr_fd_ctx_t *qr_fd_ctx = NULL;
        int32_t      op_ret = -1, op_errno = -1, ret = -1;
        char         can_wind = 0, need_unwind = 0, need_open = 0; 
        
        ret = fd_ctx_get (fd, this, &value);

        if (ret == 0) {
                qr_fd_ctx = (qr_fd_ctx_t *)(long) value;
        }

        ret = inode_ctx_get (fd->inode, this, &value);
        if (ret == 0) {
                qr_file = (qr_file_t *)(long)value;
        }

        if (qr_file) {
                LOCK (&qr_file->lock);
                {
                        if (qr_file->xattr) {
                                dict_unref (qr_file->xattr);
                                qr_file->xattr = NULL;
                        }
                }
                UNLOCK (&qr_file->lock);
        }
            
        if (qr_fd_ctx) {
                LOCK (&qr_fd_ctx->lock);
                {
                        path = qr_fd_ctx->path;
                        flags = qr_fd_ctx->flags;
                
                        if (!(qr_fd_ctx->opened
                              || qr_fd_ctx->open_in_transit)) {
                                need_open = 1;
                                qr_fd_ctx->open_in_transit = 1;
                        }

                        if (qr_fd_ctx->opened) {
                                can_wind = 1;
                        } else {
                                stub = fop_writev_stub (frame, qr_writev_helper,
                                                        fd, vector, count, off,
                                                        iobref);
                                if (stub == NULL) {
                                        op_ret = -1;
                                        op_errno = ENOMEM;
                                        need_unwind = 1;
                                        qr_fd_ctx->open_in_transit = 0;
                                        goto unlock;
                                }

                                list_add_tail (&stub->list,
                                               &qr_fd_ctx->waiting_ops);
                        }
                }
        unlock:
                UNLOCK (&qr_fd_ctx->lock);
        } else {
                can_wind = 1;
        }

out:
        if (need_unwind) {
                STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, NULL,
                                     NULL);
        } else if (can_wind) {
                STACK_WIND (frame, qr_writev_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->writev, fd, vector, count,
                            off, iobref);
        } else if (need_open) {
                op_ret = qr_loc_fill (&loc, fd->inode, path);
                if (op_ret == -1) {
                        qr_resume_pending_ops (qr_fd_ctx);
                        goto out;
                }

                STACK_WIND (frame, qr_open_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open, &loc, flags, fd, 0);

                qr_loc_wipe (&loc);
        }

        return 0;
}


int32_t
qr_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
		   int32_t op_errno, struct stat *buf)
{
	STACK_UNWIND_STRICT (fstat, frame, op_ret, op_errno, buf);
	return 0;
}


int32_t
qr_fstat_helper (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
        STACK_WIND (frame, qr_fstat_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fstat, fd);
        return 0;
}


int32_t
qr_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
        qr_fd_ctx_t *qr_fd_ctx = NULL;
        char         need_open = 0, can_wind = 0, need_unwind = 0;
        uint64_t     value = 0;
        int32_t      ret = -1, op_ret = -1, op_errno = -1;
        call_stub_t *stub = NULL;  
        loc_t        loc = {0, };
        char        *path = NULL; 
        int          flags = 0;

        ret = fd_ctx_get (fd, this, &value);
        if (ret == 0) {
                qr_fd_ctx = (qr_fd_ctx_t *)(long) value;
        }

        if (qr_fd_ctx) {
                LOCK (&qr_fd_ctx->lock);
                {
                        path = qr_fd_ctx->path;
                        flags = qr_fd_ctx->flags;

                        if (!(qr_fd_ctx->opened
                              || qr_fd_ctx->open_in_transit)) {
                                need_open = 1;
                                qr_fd_ctx->open_in_transit = 1;
                        }

                        if (qr_fd_ctx->opened) {
                                can_wind = 1;
                        } else {
                                stub = fop_fstat_stub (frame, qr_fstat_helper,
                                                       fd);
                                if (stub == NULL) {
                                        op_ret = -1;
                                        op_errno = ENOMEM;
                                        need_unwind = 1;
                                        qr_fd_ctx->open_in_transit = 0;
                                        goto unlock;
                                }

                                list_add_tail (&stub->list,
                                               &qr_fd_ctx->waiting_ops);
                        }
                }
        unlock:
                UNLOCK (&qr_fd_ctx->lock);
        } else {
                can_wind = 1;
        }

out:
        if (need_unwind) {
                STACK_UNWIND_STRICT (fstat, frame, op_ret, op_errno, NULL);
        } else if (can_wind) {
                STACK_WIND (frame, qr_fstat_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->fstat, fd);
        } else if (need_open) {
                op_ret = qr_loc_fill (&loc, fd->inode, path);
                if (op_ret == -1) {
                        qr_resume_pending_ops (qr_fd_ctx);
                        goto out;
                }

                STACK_WIND (frame, qr_open_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open, &loc, flags, fd, 0);

                qr_loc_wipe (&loc);
        }
        
        return 0;
}




int32_t
qr_fsetattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno,
                 struct stat *preop, struct stat *postop)
{
	STACK_UNWIND_STRICT (fsetattr, frame, op_ret, op_errno, preop, postop);
	return 0;
}


int32_t
qr_fsetattr_helper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                    struct stat *stbuf, int32_t valid)
{
        STACK_WIND(frame, qr_fsetattr_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->fsetattr, fd, stbuf,
                   valid);
        return 0;
}


int32_t
qr_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
             struct stat *stbuf, int32_t valid)
{
        uint64_t     value = 0;
        int          flags = 0;
        call_stub_t *stub = NULL;
        char        *path = NULL;
        loc_t        loc = {0, };
        qr_fd_ctx_t *qr_fd_ctx = NULL;
        int32_t      ret = -1, op_ret = -1, op_errno = -1;
        char         need_open = 0, can_wind = 0, need_unwind = 0;

        ret = fd_ctx_get (fd, this, &value);
        if (ret == 0) {
                qr_fd_ctx = (qr_fd_ctx_t *)(long) value;
        }

        if (qr_fd_ctx) {
                LOCK (&qr_fd_ctx->lock);
                {
                        path = qr_fd_ctx->path;
                        flags = qr_fd_ctx->flags;
                        if (!(qr_fd_ctx->opened
                              || qr_fd_ctx->open_in_transit)) {
                                need_open = 1;
                                qr_fd_ctx->open_in_transit = 1;
                        }

                        if (qr_fd_ctx->opened) {
                                can_wind = 1;
                        } else {
                                stub = fop_fsetattr_stub (frame,
                                                          qr_fsetattr_helper,
                                                          fd, stbuf, valid);
                                if (stub == NULL) {
                                        op_ret = -1;
                                        op_errno = ENOMEM;
                                        need_unwind = 1;
                                        qr_fd_ctx->open_in_transit = 0;
                                        goto unlock;
                                }

                                list_add_tail (&stub->list,
                                               &qr_fd_ctx->waiting_ops);
                        }
                }
        unlock:
                UNLOCK (&qr_fd_ctx->lock);
        } else {
                can_wind = 1;
        }

out:
        if (need_unwind) {
                STACK_UNWIND_STRICT (fsetattr, frame, op_ret, op_errno, NULL,
                                     NULL);
        } else if (can_wind) {
                STACK_WIND (frame, qr_fsetattr_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->fsetattr, fd, stbuf,
                            valid);
        } else if (need_open) {
                op_ret = qr_loc_fill (&loc, fd->inode, path);
                if (op_ret == -1) {
                        qr_resume_pending_ops (qr_fd_ctx);
                        goto out;
                }

                STACK_WIND (frame, qr_open_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open, &loc, flags, fd, 0);

                qr_loc_wipe (&loc);
        }

        return 0;
}


int32_t
qr_fsetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno)
{
	STACK_UNWIND_STRICT (fsetxattr, frame, op_ret, op_errno);
	return 0;
}


int32_t
qr_fsetxattr_helper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                     dict_t *dict, int32_t flags)
{
        STACK_WIND (frame, qr_fsetxattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fsetxattr, fd, dict, flags);
        return 0;
}


int32_t
qr_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *dict,
              int32_t flags)
{
        uint64_t     value = 0;
        call_stub_t *stub = NULL;  
        char        *path = NULL;
        loc_t        loc = {0, };
        int          open_flags = 0;
        qr_fd_ctx_t *qr_fd_ctx = NULL;
        int32_t      ret = -1, op_ret = -1, op_errno = -1;
        char         need_open = 0, can_wind = 0, need_unwind = 0;

        ret = fd_ctx_get (fd, this, &value);
        if (ret == 0) {
                qr_fd_ctx = (qr_fd_ctx_t *)(long) value;
        }

        if (qr_fd_ctx) {
                LOCK (&qr_fd_ctx->lock);
                {
                        path = qr_fd_ctx->path;
                        open_flags = qr_fd_ctx->flags;

                        if (!(qr_fd_ctx->opened
                              || qr_fd_ctx->open_in_transit)) {
                                need_open = 1;
                                qr_fd_ctx->open_in_transit = 1;
                        }

                        if (qr_fd_ctx->opened) {
                                can_wind = 1;
                        } else {
                                stub = fop_fsetxattr_stub (frame,
                                                           qr_fsetxattr_helper,
                                                           fd, dict, flags);
                                if (stub == NULL) {
                                        op_ret = -1;
                                        op_errno = ENOMEM;
                                        need_unwind = 1;
                                        qr_fd_ctx->open_in_transit = 0;
                                        goto unlock;
                                }

                                list_add_tail (&stub->list,
                                               &qr_fd_ctx->waiting_ops);
                        }
                }
        unlock:
                UNLOCK (&qr_fd_ctx->lock);
        } else {
                can_wind = 1;
        }

out:
        if (need_unwind) {
                STACK_UNWIND_STRICT (fsetxattr, frame, op_ret, op_errno);
        } else if (can_wind) {
                STACK_WIND (frame, qr_fsetxattr_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->fsetxattr, fd, dict,
                            flags);
        } else if (need_open) {
                op_ret = qr_loc_fill (&loc, fd->inode, path);
                if (op_ret == -1) {
                        qr_resume_pending_ops (qr_fd_ctx);
                        goto out;
                }

                STACK_WIND (frame, qr_open_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open, &loc, open_flags,
                            fd, 0);

                qr_loc_wipe (&loc);
        } 
        
        return 0;
}


int32_t
qr_fgetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *dict)
{
	STACK_UNWIND_STRICT (fgetxattr, frame, op_ret, op_errno, dict);
	return 0;
}


int32_t
qr_fgetxattr_helper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                     const char *name)
{
        STACK_WIND (frame, qr_fgetxattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fgetxattr, fd, name);
        return 0;
}


int32_t
qr_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, const char *name)
{
        int          flags = 0;
        uint64_t     value = 0;
        call_stub_t *stub = NULL;  
        char        *path = NULL;
        loc_t        loc = {0, };
        qr_fd_ctx_t *qr_fd_ctx = NULL;
        int32_t      ret = -1, op_ret = -1, op_errno = -1;
        char         need_open = 0, can_wind = 0, need_unwind = 0;

        /*
         * FIXME: Can quick-read use the extended attributes stored in the
         * cache? this needs to be discussed.
         */

        ret = fd_ctx_get (fd, this, &value);
        if (ret == 0) {
                qr_fd_ctx = (qr_fd_ctx_t *)(long) value;
        }

        if (qr_fd_ctx) {
                LOCK (&qr_fd_ctx->lock);
                {
                        path = qr_fd_ctx->path;
                        flags = qr_fd_ctx->flags;

                        if (!(qr_fd_ctx->opened
                              || qr_fd_ctx->open_in_transit)) {
                                need_open = 1;
                                qr_fd_ctx->open_in_transit = 1;
                        }

                        if (qr_fd_ctx->opened) {
                                can_wind = 1;
                        } else {
                                stub = fop_fgetxattr_stub (frame,
                                                           qr_fgetxattr_helper,
                                                           fd, name);
                                if (stub == NULL) {
                                        op_ret = -1;
                                        op_errno = ENOMEM;
                                        need_unwind = 1;
                                        qr_fd_ctx->open_in_transit = 0;
                                        goto unlock;
                                }

                                list_add_tail (&stub->list,
                                               &qr_fd_ctx->waiting_ops);
                        }
                }
        unlock:
                UNLOCK (&qr_fd_ctx->lock);
        } else {
                can_wind = 1;
        }

out:
        if (need_unwind) {
                STACK_UNWIND_STRICT (open, frame, op_ret, op_errno, NULL);
        } else if (can_wind) {
                STACK_WIND (frame, qr_fgetxattr_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->fgetxattr, fd, name);
        } else if (need_open) {
                op_ret = qr_loc_fill (&loc, fd->inode, path);
                if (op_ret == -1) {
                        qr_resume_pending_ops (qr_fd_ctx);
                        goto out;
                }
                
                STACK_WIND (frame, qr_open_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open, &loc, flags, fd, 0);

                qr_loc_wipe (&loc);
        }
        
        return 0;
}


int32_t
qr_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno)
{
	STACK_UNWIND_STRICT (flush, frame, op_ret, op_errno);
	return 0;
}


int32_t
qr_flush_helper (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
        STACK_WIND (frame, qr_flush_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->flush, fd);
        return 0; 
}


int32_t
qr_flush (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
        uint64_t     value = 0;
        call_stub_t *stub = NULL;  
        qr_fd_ctx_t *qr_fd_ctx = NULL;
        int32_t      ret = -1, op_ret = -1, op_errno = -1;
        char         can_wind = 0, need_unwind = 0;

        ret = fd_ctx_get (fd, this, &value);
        if (ret == 0) {
                qr_fd_ctx = (qr_fd_ctx_t *)(long)value;
        }

        if (qr_fd_ctx) {
                LOCK (&qr_fd_ctx->lock);
                {
                        if (qr_fd_ctx->opened) {
                                can_wind = 1;
                        } else if (qr_fd_ctx->open_in_transit) {
                                stub = fop_flush_stub (frame, qr_flush_helper,
                                                       fd);
                                if (stub == NULL) {
                                        op_ret = -1;
                                        op_errno = ENOMEM;
                                        need_unwind = 1;
                                        qr_fd_ctx->open_in_transit = 0;
                                        goto unlock;
                                }

                                list_add_tail (&stub->list,
                                               &qr_fd_ctx->waiting_ops);
                        } else {
                                op_ret = 0;
                                need_unwind = 1;
                        }
                }
        unlock:
                UNLOCK (&qr_fd_ctx->lock);
        } else {
                can_wind = 1;
        }

        if (need_unwind) {
                STACK_UNWIND_STRICT (flush, frame, op_ret, op_errno);
        } else if (can_wind) {
                STACK_WIND (frame, qr_flush_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->flush, fd);
        }

        return 0;
}


int32_t
qr_fentrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno)
{
	STACK_UNWIND_STRICT (fentrylk, frame, op_ret, op_errno);
	return 0;
}

int32_t
qr_fentrylk_helper (call_frame_t *frame, xlator_t *this, const char *volume,
                    fd_t *fd, const char *basename, entrylk_cmd cmd,
                    entrylk_type type)
{
        STACK_WIND(frame, qr_fentrylk_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->fentrylk, volume, fd, basename,
                   cmd, type);
        return 0;
}


int32_t
qr_fentrylk (call_frame_t *frame, xlator_t *this, const char *volume, fd_t *fd,
             const char *basename, entrylk_cmd cmd, entrylk_type type)
{
        int          flags = 0;
        uint64_t     value = 0;
        call_stub_t *stub = NULL;  
        char        *path = NULL;
        loc_t        loc = {0, };
        qr_fd_ctx_t *qr_fd_ctx = NULL;
        int32_t      ret = -1, op_ret = -1, op_errno = -1;
        char         need_open = 0, can_wind = 0, need_unwind = 0;

        ret = fd_ctx_get (fd, this, &value);
        if (ret == 0) {
                qr_fd_ctx = (qr_fd_ctx_t *)(long)value;
        }

        if (qr_fd_ctx) {
                LOCK (&qr_fd_ctx->lock);
                {
                        path = qr_fd_ctx->path;
                        flags = qr_fd_ctx->flags;
                        
                        if (!(qr_fd_ctx->opened
                              || qr_fd_ctx->open_in_transit)) {
                                need_open = 1;
                                qr_fd_ctx->open_in_transit = 1;
                        }

                        if (qr_fd_ctx->opened) {
                                can_wind = 1;
                        } else {
                                stub = fop_fentrylk_stub (frame,
                                                          qr_fentrylk_helper,
                                                          volume, fd, basename,
                                                          cmd, type);
                                if (stub == NULL) {
                                        op_ret = -1;
                                        op_errno = ENOMEM;
                                        need_unwind = 1;
                                        qr_fd_ctx->open_in_transit = 0;
                                        goto unlock;
                                }

                                list_add_tail (&stub->list,
                                               &qr_fd_ctx->waiting_ops);
                        }
                }
        unlock:
                UNLOCK (&qr_fd_ctx->lock);
        } else {
                can_wind = 1;
        }

out:
        if (need_unwind) {
                STACK_UNWIND_STRICT (fentrylk, frame, op_ret, op_errno);
        } else if (can_wind) {
                STACK_WIND (frame, qr_fentrylk_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fentrylk, volume, fd,
                            basename, cmd, type);
        } else if (need_open) {
                op_ret = qr_loc_fill (&loc, fd->inode, path);
                if (op_ret == -1) {
                        qr_resume_pending_ops (qr_fd_ctx);
                        goto out;
                }

                STACK_WIND (frame, qr_open_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open, &loc, flags, fd, 0);

                qr_loc_wipe (&loc);
        }
        
        return 0;
}


int32_t
qr_finodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno)

{
	STACK_UNWIND_STRICT (finodelk, frame, op_ret, op_errno);
	return 0;
}


int32_t
qr_finodelk_helper (call_frame_t *frame, xlator_t *this, const char *volume,
                    fd_t *fd, int32_t cmd, struct flock *lock)
{
        STACK_WIND (frame, qr_finodelk_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->finodelk, volume, fd, cmd, lock);
        return 0;
}


int32_t
qr_finodelk (call_frame_t *frame, xlator_t *this, const char *volume, fd_t *fd,
             int32_t cmd, struct flock *lock)
{
        int          flags = 0; 
        uint64_t     value = 0;
        call_stub_t *stub = NULL;  
        char        *path = NULL;
        loc_t        loc = {0, };
        qr_fd_ctx_t *qr_fd_ctx = NULL;
        int32_t      ret = -1, op_ret = -1, op_errno = -1;
        char         need_open = 0, can_wind = 0, need_unwind = 0;

        ret = fd_ctx_get (fd, this, &value);
        if (ret == 0) {
                qr_fd_ctx = (qr_fd_ctx_t *)(long)value;
        }

        if (qr_fd_ctx) {
                LOCK (&qr_fd_ctx->lock);
                {
                        path = qr_fd_ctx->path;
                        flags = qr_fd_ctx->flags;

                        if (!(qr_fd_ctx->opened
                              || qr_fd_ctx->open_in_transit)) {
                                need_open = 1;
                                qr_fd_ctx->open_in_transit = 1;
                        }

                        if (qr_fd_ctx->opened) {
                                can_wind = 1;
                        } else {
                                stub = fop_finodelk_stub (frame,
                                                          qr_finodelk_helper,
                                                          volume, fd, cmd,
                                                          lock);
                                if (stub == NULL) {
                                        op_ret = -1;
                                        op_errno = ENOMEM;
                                        need_unwind = 1;
                                        qr_fd_ctx->open_in_transit = 0;
                                        goto unlock;
                                }

                                list_add_tail (&stub->list,
                                               &qr_fd_ctx->waiting_ops);
                        }
                }
        unlock:
                UNLOCK (&qr_fd_ctx->lock);
        } else {
                can_wind = 1;
        }

out:
        if (need_unwind) {
                STACK_UNWIND_STRICT (finodelk, frame, op_ret, op_errno);
        } else if (can_wind) {
                STACK_WIND (frame, qr_finodelk_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->finodelk, volume, fd,
                            cmd, lock);
        } else if (need_open) {
                op_ret = qr_loc_fill (&loc, fd->inode, path);
                if (op_ret == -1) {
                        qr_resume_pending_ops (qr_fd_ctx);
                        goto out;
                }

                STACK_WIND (frame, qr_open_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open, &loc, flags, fd, 0);
                
                qr_loc_wipe (&loc);
        }
        
        return 0;
}


int32_t
qr_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, struct stat *prebuf, struct stat *postbuf)
{
	STACK_UNWIND_STRICT (fsync, frame, op_ret, op_errno, prebuf, postbuf);
	return 0;
}


int32_t
qr_fsync_helper (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags)
{
        STACK_WIND (frame, qr_fsync_cbk, FIRST_CHILD (this),
                    FIRST_CHILD(this)->fops->fsync, fd, flags);
        return 0;
}

int32_t
qr_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags)
{
        uint64_t     value = 0;
        call_stub_t *stub = NULL;  
        char        *path = NULL;
        loc_t        loc = {0, };
        int          open_flags = 0;
        qr_fd_ctx_t *qr_fd_ctx = NULL;
        int32_t      ret = -1, op_ret = -1, op_errno = -1;
        char         need_open = 0, can_wind = 0, need_unwind = 0;
 
        ret = fd_ctx_get (fd, this, &value);
        if (ret == 0) {
                qr_fd_ctx = (qr_fd_ctx_t *)(long)value;
        }

        if (qr_fd_ctx) {
                LOCK (&qr_fd_ctx->lock);
                {
                        path = qr_fd_ctx->path;
                        open_flags = qr_fd_ctx->flags;

                        if (!(qr_fd_ctx->opened
                              || qr_fd_ctx->open_in_transit)) {
                                need_open = 1;
                                qr_fd_ctx->open_in_transit = 1;
                        }

                        if (qr_fd_ctx->opened) {
                                can_wind = 1;
                        } else {
                                stub = fop_fsync_stub (frame, qr_fsync_helper,
                                                       fd, flags);
                                if (stub == NULL) {
                                        op_ret = -1;
                                        op_errno = ENOMEM;
                                        need_unwind = 1;
                                        qr_fd_ctx->open_in_transit = 0;
                                        goto unlock;
                                }

                                list_add_tail (&stub->list,
                                               &qr_fd_ctx->waiting_ops);
                        }
                }
        unlock:
                UNLOCK (&qr_fd_ctx->lock);
        } else {
                can_wind = 1;
        }

out:
        if (need_unwind) {
                STACK_UNWIND_STRICT (fsync, frame, op_ret, op_errno, NULL,
                                     NULL);
        } else if (can_wind) {
                STACK_WIND (frame, qr_fsync_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->fsync, fd, flags);
        } else if (need_open) {
                op_ret = qr_loc_fill (&loc, fd->inode, path);
                if (op_ret == -1) {
                        qr_resume_pending_ops (qr_fd_ctx);
                        goto out;
                }

                STACK_WIND (frame, qr_open_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open, &loc, open_flags,
                            fd, 0);

                qr_loc_wipe (&loc);
        }

        return 0;
}


int32_t
qr_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct stat *prebuf,
                  struct stat *postbuf)
{
        int32_t     ret = 0;
        uint64_t    value = 0;
        qr_file_t  *qr_file = NULL;
        qr_local_t *local = NULL;

        if (op_ret == -1) {
                goto out;
        }

        local = frame->local;
        if ((local == NULL) || (local->fd == NULL)
            || (local->fd->inode == NULL)) {
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        ret = inode_ctx_get (local->fd->inode, this, &value);
        if (ret == 0) {
                qr_file = (qr_file_t *)(long) value;

                if (qr_file) {
                        LOCK (&qr_file->lock);
                        {
                                if (qr_file->stbuf.st_size != postbuf->st_size)
                                {
                                        dict_unref (qr_file->xattr);
                                        qr_file->xattr = NULL;
                                }
                        }
                        UNLOCK (&qr_file->lock);
                }
        }

out:
        STACK_UNWIND_STRICT (ftruncate, frame, op_ret, op_errno, prebuf,
                             postbuf);
        return 0;
}


int32_t
qr_ftruncate_helper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                     off_t offset)
{
        STACK_WIND (frame, qr_ftruncate_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->ftruncate, fd, offset);
        return 0;
}


int32_t
qr_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset)
{
        int          flags = 0;
        uint64_t     value = 0;
        call_stub_t *stub = NULL;  
        char        *path = NULL;
        loc_t        loc = {0, };
        qr_local_t  *local = NULL;
        qr_fd_ctx_t *qr_fd_ctx = NULL;
        int32_t      ret = -1, op_ret = -1, op_errno = -1;
        char         need_open = 0, can_wind = 0, need_unwind = 0;

        ret = fd_ctx_get (fd, this, &value);
        if (ret == 0) {
                qr_fd_ctx = (qr_fd_ctx_t *)(long)value;
        }

        local = CALLOC (1, sizeof (*local));
        if (local == NULL) {
                op_ret = -1;
                op_errno = ENOMEM;
                need_unwind = 1;
                goto out;
        }

        local->fd = fd;
        frame->local = local;

        if (qr_fd_ctx) {
                LOCK (&qr_fd_ctx->lock);
                {
                        path = qr_fd_ctx->path;
                        flags = qr_fd_ctx->flags;

                        if (!(qr_fd_ctx->opened
                              || qr_fd_ctx->open_in_transit)) {
                                need_open = 1;
                                qr_fd_ctx->open_in_transit = 1;
                        }

                        if (qr_fd_ctx->opened) {
                                can_wind = 1;
                        } else {
                                stub = fop_ftruncate_stub (frame,
                                                           qr_ftruncate_helper,
                                                           fd, offset);
                                if (stub == NULL) {
                                        op_ret = -1;
                                        op_errno = ENOMEM;
                                        need_unwind = 1;
                                        qr_fd_ctx->open_in_transit = 0;
                                        goto unlock;
                                }

                                list_add_tail (&stub->list,
                                               &qr_fd_ctx->waiting_ops);
                        }
                }
        unlock:
                UNLOCK (&qr_fd_ctx->lock);
        } else {
                can_wind = 1;
        }

out:
        if (need_unwind) {
                STACK_UNWIND_STRICT (ftruncate, frame, op_ret, op_errno, NULL,
                                     NULL);
        } else if (can_wind) {
                STACK_WIND (frame, qr_ftruncate_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->ftruncate, fd, offset);
        } else if (need_open) {
                op_ret = qr_loc_fill (&loc, fd->inode, path);
                if (op_ret == -1) {
                        qr_resume_pending_ops (qr_fd_ctx);
                        goto out;
                }

                STACK_WIND (frame, qr_open_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open, &loc, flags, fd, 0);

                qr_loc_wipe (&loc);
        }

        return 0;
}


int32_t
qr_lk_cbk (call_frame_t *frame,	void *cookie, xlator_t *this, int32_t op_ret,
           int32_t op_errno, struct flock *lock)
{
	STACK_UNWIND_STRICT (lk, frame, op_ret, op_errno, lock);
	return 0;
}


int32_t
qr_lk_helper (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t cmd,
              struct flock *lock)
{
	STACK_WIND (frame, qr_lk_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lk, fd, cmd, lock);

        return 0;
}


int32_t
qr_lk (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t cmd,
       struct flock *lock)
{
        int          flags = 0;
        uint64_t     value = 0;
        call_stub_t *stub = NULL;  
        char        *path = NULL;
        loc_t        loc = {0, };
        qr_fd_ctx_t *qr_fd_ctx = NULL;
        int32_t      ret = -1, op_ret = -1, op_errno = -1;
        char         need_open = 0, can_wind = 0, need_unwind = 0;

        ret = fd_ctx_get (fd, this, &value);
        if (ret == 0) {
                qr_fd_ctx = (qr_fd_ctx_t *)(long)value;
        }
        
        if (qr_fd_ctx) {
                LOCK (&qr_fd_ctx->lock);
                {
                        path = qr_fd_ctx->path;
                        flags = qr_fd_ctx->flags;

                        if (!(qr_fd_ctx->opened
                              || qr_fd_ctx->open_in_transit)) {
                                need_open = 1;
                                qr_fd_ctx->open_in_transit = 1;
                        }

                        if (qr_fd_ctx->opened) {
                                can_wind = 1;
                        } else {
                                stub = fop_lk_stub (frame, qr_lk_helper, fd,
                                                    cmd, lock);
                                if (stub == NULL) {
                                        op_ret = -1;
                                        op_errno = ENOMEM;
                                        need_unwind = 1;
                                        qr_fd_ctx->open_in_transit = 0;
                                        goto unlock;
                                }

                                list_add_tail (&stub->list,
                                               &qr_fd_ctx->waiting_ops);
                        }
                }
        unlock:
                UNLOCK (&qr_fd_ctx->lock);
        } else {
                can_wind = 1;
        }

out:
        if (need_unwind) {
                STACK_UNWIND_STRICT (lk, frame, op_ret, op_errno, NULL);
        } else if (can_wind) {
                STACK_WIND (frame, qr_lk_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->lk, fd, cmd, lock);
        } else if (need_open) {
                op_ret = qr_loc_fill (&loc, fd->inode, path);
                if (op_ret == -1) {
                        qr_resume_pending_ops (qr_fd_ctx);
                        goto out;
                }

                STACK_WIND (frame, qr_open_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open, &loc, flags, fd, 0);

                qr_loc_wipe (&loc);
        }
        
        return 0;
}


int32_t
qr_release (xlator_t *this, fd_t *fd)
{
        qr_fd_ctx_t *qr_fd_ctx = NULL;
        int32_t      ret = 0;
        uint64_t     value = 0;

        ret = fd_ctx_del (fd, this, &value);
        if (ret == 0) {
                qr_fd_ctx = (qr_fd_ctx_t *)(long) value;
                if (qr_fd_ctx) {
                        qr_fd_ctx_free (qr_fd_ctx);
                }
        }

        return 0;
}


int32_t
qr_forget (xlator_t *this, inode_t *inode)
{
        qr_file_t *qr_file = NULL;
        uint64_t   value = 0;
        int32_t    ret = -1;

        ret = inode_ctx_del (inode, this, &value);
        if (ret == 0) {
                qr_file = (qr_file_t *)(long) value;
                if (qr_file) {
                        LOCK (&qr_file->lock);
                        {
                                if (qr_file->xattr) {
                                        dict_unref (qr_file->xattr);
                                        qr_file->xattr = NULL;
                                }
                        }
                        UNLOCK (&qr_file->lock);
                }
                
                FREE (qr_file);
        }

        return 0;
}

int
qr_priv_dump (xlator_t *this)
{
        qr_conf_t       *conf = NULL;
        char            key[GF_DUMP_MAX_BUF_LEN];
        char            key_prefix[GF_DUMP_MAX_BUF_LEN];

        if (!this)
                return -1;

        conf = this->private;
        if (!conf) {
                gf_log (this->name, GF_LOG_WARNING,
                        "conf null in xlator");
                return -1;
        }

        gf_proc_dump_build_key (key_prefix,
                                "xlator.performance.quick-read",
                                "priv");

        gf_proc_dump_add_section (key_prefix);

        gf_proc_dump_build_key (key, key_prefix, "max_file_size");
        gf_proc_dump_write (key, "%d", conf->max_file_size);
        gf_proc_dump_build_key (key, key_prefix, "cache_timeout");
        gf_proc_dump_write (key, "%d", conf->cache_timeout);

        return 0;
}

int32_t 
init (xlator_t *this)
{
	char      *str = NULL;
        int32_t    ret = -1;
        qr_conf_t *conf = NULL;
 
        if (!this->children || this->children->next) {
                gf_log (this->name, GF_LOG_ERROR,
                        "FATAL: volume (%s) not configured with exactly one "
			"child", this->name);
                return -1;
        }

	if (!this->parents) {
		gf_log (this->name, GF_LOG_WARNING,
			"dangling volume. check volfile ");
	}

        conf = CALLOC (1, sizeof (*conf));
        if (conf == NULL) {
                gf_log (this->name, GF_LOG_ERROR,
                        "out of memory");
                ret = -1;
                goto out;
        }

        conf->max_file_size = 65536;
        ret = dict_get_str (this->options, "max-file-size", 
                            &str);
        if (ret == 0) {
                ret = gf_string2bytesize (str, &conf->max_file_size);
                if (ret != 0) {
                        gf_log (this->name, GF_LOG_ERROR, 
                                "invalid number format \"%s\" of \"option "
                                "max-file-size\"", 
                                str);
                        ret = -1;
                        goto out;
                }
        }

        conf->cache_timeout = 1;
        ret = dict_get_str (this->options, "cache-timeout", &str);
        if (ret == 0) {
                ret = gf_string2uint_base10 (str, 
                                             (unsigned int *)&conf->cache_timeout);
                if (ret != 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "invalid cache-timeout value %s", str);
                        ret = -1;
                        goto out;
                } 
        }

        ret = 0;

        this->private = conf;
out:
        if ((ret == -1) && conf) {
                FREE (conf);
        }

        return ret;
}


void
fini (xlator_t *this)
{
        return;
}


struct xlator_fops fops = {
	.lookup      = qr_lookup,
        .open        = qr_open,
        .readv       = qr_readv,
        .writev      = qr_writev,
        .fstat       = qr_fstat,
        .fsetxattr   = qr_fsetxattr,
        .fgetxattr   = qr_fgetxattr,
        .flush       = qr_flush,
        .fentrylk    = qr_fentrylk,
        .finodelk    = qr_finodelk,
        .fsync       = qr_fsync,
        .ftruncate   = qr_ftruncate,
        .lk          = qr_lk,
        .fsetattr    = qr_fsetattr,
};


struct xlator_mops mops = {
};


struct xlator_cbks cbks = {
        .forget  = qr_forget,
        .release = qr_release, 
};

struct xlator_dumpops dumpops = {
        .priv      =  qr_priv_dump,
};

struct volume_options options[] = {
        { .key  = {"cache-timeout"},
          .type = GF_OPTION_TYPE_INT,
          .min = 1,
          .max = 60
        },
        { .key  = {"max-file-size"},
          .type = GF_OPTION_TYPE_SIZET,
          .min  = 0,
          .max  = 1 * GF_UNIT_KB * 1000,
        },
};
