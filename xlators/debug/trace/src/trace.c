/*
  Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
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

/**
 * xlators/debug/trace :
 *    This translator logs all the arguments to the fops/mops and also
 *    their _cbk functions, which later passes the call to next layer.
 *    Very helpful translator for debugging.
 */

#include <time.h>
#include <errno.h>
#include "glusterfs.h"
#include "xlator.h"
#include "common-utils.h"

#define ERR_EINVAL_NORETURN(cond)                               \
        do                                                      \
        {                                                       \
                if ((cond))                                     \
                {                                               \
                        gf_log ("ERROR", GF_LOG_ERROR,          \
                                "%s: %s: (%s) is true",         \
                                __FILE__, __FUNCTION__, #cond); \
                }                                               \
        } while (0)


typedef struct trace_private {
        int32_t debug_flag;
} trace_private_t;


struct {
        char *name;
        int enabled;
} trace_fop_names[GF_FOP_MAXVALUE];


static char *
trace_stat_to_str (struct stat *stbuf)
{
        char *statstr = NULL;
        char atime_buf[256] = {0,};
        char mtime_buf[256] = {0,};
        char ctime_buf[256] = {0,};
        int  asprint_ret_value = 0;

        strftime (atime_buf, 256, "[%b %d %H:%M:%S]",
                  localtime (&stbuf->st_atime));
        strftime (mtime_buf, 256, "[%b %d %H:%M:%S]",
                  localtime (&stbuf->st_mtime));
        strftime (ctime_buf, 256, "[%b %d %H:%M:%S]",
                  localtime (&stbuf->st_ctime));

        asprint_ret_value = asprintf (&statstr,
                                      "st_ino=%"PRIu64", st_mode=%o, st_nlink=%"GF_PRI_NLINK", "
                                      "st_uid=%d, st_gid=%d, st_size=%"PRId64", st_blocks=%"PRId64
                                      ", st_atime=%s, st_mtime=%s, st_ctime=%s",
                                      stbuf->st_ino, stbuf->st_mode, stbuf->st_nlink, stbuf->st_uid,
                                      stbuf->st_gid, stbuf->st_size, stbuf->st_blocks, atime_buf,
                                      mtime_buf, ctime_buf);

        if (asprint_ret_value < 0)
                statstr = NULL;

        return statstr;
}


int
trace_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, fd_t *fd,
                  inode_t *inode, struct stat *buf,
                  struct stat *preparent, struct stat *postparent)
{
        char  *statstr = NULL;
        char  *preparentstr = NULL;
        char  *postparentstr = NULL;

        if (trace_fop_names[GF_FOP_CREATE].enabled) {
                if (op_ret >= 0) {
                        statstr = trace_stat_to_str (buf);
                        preparentstr = trace_stat_to_str (preparent);
                        postparentstr = trace_stat_to_str (postparent);

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, fd=%p, ino=%"PRIu64" "
                                "*stbuf {%s}, *preparent {%s}, *postparent = "
                                "{%s})",
                                frame->root->unique, op_ret, fd, inode->ino,
                                statstr, preparentstr, postparentstr);

                        if (statstr)
                                FREE (statstr);
                        if (preparentstr)
                                FREE (preparentstr);
                        if (postparentstr)
                                FREE (postparentstr);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, op_errno=%d)",
                                frame->root->unique, op_ret, op_errno);
                }
        }

        STACK_UNWIND_STRICT (create, frame, op_ret, op_errno, fd, inode, buf,
                             preparent, postparent);
        return 0;
}


int
trace_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, fd_t *fd)
{

        if (trace_fop_names[GF_FOP_OPEN].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (op_ret=%d, op_errno=%d, *fd=%p)",
                        frame->root->unique, op_ret, op_errno, fd);
        }

        STACK_UNWIND_STRICT (open, frame, op_ret, op_errno, fd);
        return 0;
}


int
trace_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct stat *buf)
{
        char atime_buf[256];
        char mtime_buf[256];
        char ctime_buf[256];


        if (trace_fop_names[GF_FOP_STAT].enabled) {
                if (op_ret >= 0) {
                        strftime (atime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_atime));
                        strftime (mtime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_mtime));
                        strftime (ctime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_ctime));

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, buf {st_dev=%"GF_PRI_DEV", "
                                "st_ino=%"PRIu64", st_mode=%o, st_nlink=%"GF_PRI_NLINK", "
                                "st_uid=%d, st_gid=%d, st_rdev=%"GF_PRI_DEV", st_size=%"PRId64
                                ", st_blksize=%"GF_PRI_BLKSIZE", st_blocks=%"PRId64", "
                                "st_atime=%s, st_mtime=%s, st_ctime=%s})",
                                frame->root->unique, op_ret, buf->st_dev, buf->st_ino,
                                buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid,
                                buf->st_rdev, buf->st_size, buf->st_blksize,
                                buf->st_blocks, atime_buf, mtime_buf, ctime_buf);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, op_errno=%d)",
                                frame->root->unique, op_ret, op_errno);
                }
        }

        STACK_UNWIND_STRICT (stat, frame, op_ret, op_errno, buf);
        return 0;
}


int
trace_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iovec *vector,
                 int32_t count, struct stat *buf, struct iobref *iobref)
{
        char  atime_buf[256];
        char  mtime_buf[256];
        char  ctime_buf[256];

        if (trace_fop_names[GF_FOP_READ].enabled) {
                if (op_ret >= 0) {
                        strftime (atime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_atime));
                        strftime (mtime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_mtime));
                        strftime (ctime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_ctime));

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, *buf {st_dev=%"GF_PRI_DEV", "
                                "st_ino=%"PRIu64", st_mode=%o, st_nlink=%"GF_PRI_NLINK", "
                                "st_uid=%d, st_gid=%d, st_rdev=%"GF_PRI_DEV", "
                                "st_size=%"PRId64", st_blksize=%"GF_PRI_BLKSIZE", "
                                "st_blocks=%"PRId64", st_atime=%s, st_mtime=%s, st_ctime=%s})",
                                frame->root->unique, op_ret, buf->st_dev, buf->st_ino,
                                buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid,
                                buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks,
                                atime_buf, mtime_buf, ctime_buf);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, op_errno=%d)",
                                frame->root->unique, op_ret, op_errno);
                }
        }

        STACK_UNWIND_STRICT (readv, frame, op_ret, op_errno, vector, count,
                             buf, iobref);
        return 0;
}


int
trace_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno,
                  struct stat *prebuf, struct stat *postbuf)
{
        char  *preopstr = NULL;
        char  *postopstr = NULL;

        if (trace_fop_names[GF_FOP_WRITE].enabled) {
                if (op_ret >= 0) {
                        preopstr = trace_stat_to_str (prebuf);
                        preopstr = trace_stat_to_str (postbuf);

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, ino = %"PRIu64
                                ", *prebuf = {%s}, *postbuf = {%s})",
                                frame->root->unique, op_ret, postbuf->st_ino,
                                preopstr, postopstr);

                        if (preopstr)
                                FREE (preopstr);

                        if (postopstr)
                                FREE (postopstr);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, op_errno=%d)",
                                frame->root->unique, op_ret, op_errno);
                }
        }

        STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, prebuf, postbuf);
        return 0;
}


int
trace_getdents_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dir_entry_t *entries,
                    int32_t count)
{
        if (trace_fop_names[GF_FOP_GETDENTS].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (op_ret=%d, op_errno=%d, count=%d)",
                        frame->root->unique, op_ret, op_errno, count);
        }

        STACK_UNWIND_STRICT (getdents, frame, op_ret, op_errno, entries, count);
        return 0;
}


int
trace_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, gf_dirent_t *buf)
{
        if (trace_fop_names[GF_FOP_READDIR].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64" :(op_ret=%d, op_errno=%d)",
                        frame->root->unique, op_ret, op_errno);
        }

        STACK_UNWIND_STRICT (readdir, frame, op_ret, op_errno, buf);

        return 0;
}


int
trace_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, gf_dirent_t *buf)
{
        if (trace_fop_names[GF_FOP_READDIRP].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64" :(op_ret=%d, op_errno=%d)",
                        frame->root->unique, op_ret, op_errno);
        }

        STACK_UNWIND_STRICT (readdirp, frame, op_ret, op_errno, buf);

        return 0;
}


int
trace_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno,
                 struct stat *prebuf, struct stat *postbuf)
{
        char  *preopstr = NULL;
        char  *postopstr = NULL;

        if (trace_fop_names[GF_FOP_FSYNC].enabled) {
                if (op_ret >= 0) {
                        preopstr = trace_stat_to_str (prebuf);
                        preopstr = trace_stat_to_str (postbuf);

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, ino = %"PRIu64
                                ", *prebuf = {%s}, *postbuf = {%s}",
                                frame->root->unique, op_ret, postbuf->st_ino,
                                preopstr, postopstr);

                        if (preopstr)
                                FREE (preopstr);

                        if (postopstr)
                                FREE (postopstr);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, op_errno=%d)",
                                frame->root->unique, op_ret, op_errno);
                }
        }

        STACK_UNWIND_STRICT (fsync, frame, op_ret, op_errno, prebuf, postbuf);

        return 0;
}


int
trace_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno,
                   struct stat *statpre, struct stat *statpost)
{
        char atime_pre[256] = {0,};
        char mtime_pre[256] = {0,};
        char ctime_pre[256] = {0,};
        char atime_post[256] = {0,};
        char mtime_post[256] = {0,};
        char ctime_post[256] = {0,};

        if (trace_fop_names[GF_FOP_SETATTR].enabled) {
                if (op_ret >= 0) {
                        strftime (atime_pre, 256, "[%b %d %H:%M:%S]",
                                  localtime (&statpre->st_atime));
                        strftime (mtime_pre, 256, "[%b %d %H:%M:%S]",
                                  localtime (&statpre->st_mtime));
                        strftime (ctime_pre, 256, "[%b %d %H:%M:%S]",
                                  localtime (&statpre->st_ctime));

                        strftime (atime_post, 256, "[%b %d %H:%M:%S]",
                                  localtime (&statpost->st_atime));
                        strftime (mtime_post, 256, "[%b %d %H:%M:%S]",
                                  localtime (&statpost->st_mtime));
                        strftime (ctime_post, 256, "[%b %d %H:%M:%S]",
                                  localtime (&statpost->st_ctime));

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, *statpre "
                                "{st_ino=%"PRIu64", st_mode=%o, st_uid=%d, "
                                "st_gid=%d, st_atime=%s, st_mtime=%s, "
                                "st_ctime=%s}, *statpost {st_ino=%"PRIu64", "
                                "st_mode=%o, st_uid=%d, st_gid=%d, st_atime=%s,"
                                " st_mtime=%s, st_ctime=%s})",
                                frame->root->unique, op_ret, statpre->st_ino,
                                statpre->st_mode, statpre->st_uid,
                                statpre->st_gid, atime_pre, mtime_pre,
                                ctime_pre, statpost->st_ino, statpost->st_mode,
                                statpost->st_uid, statpost->st_gid, atime_post,
                                mtime_post, ctime_post);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, op_errno=%d)",
                                frame->root->unique, op_ret, op_errno);
                }
        }

        STACK_UNWIND_STRICT (setattr, frame, op_ret, op_errno, statpre, statpost);
        return 0;
}


int
trace_fsetattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno,
                    struct stat *statpre, struct stat *statpost)
{
        char atime_pre[256] = {0,};
        char mtime_pre[256] = {0,};
        char ctime_pre[256] = {0,};
        char atime_post[256] = {0,};
        char mtime_post[256] = {0,};
        char ctime_post[256] = {0,};

        if (trace_fop_names[GF_FOP_FSETATTR].enabled) {
                if (op_ret >= 0) {
                        strftime (atime_pre, 256, "[%b %d %H:%M:%S]",
                                  localtime (&statpre->st_atime));
                        strftime (mtime_pre, 256, "[%b %d %H:%M:%S]",
                                  localtime (&statpre->st_mtime));
                        strftime (ctime_pre, 256, "[%b %d %H:%M:%S]",
                                  localtime (&statpre->st_ctime));

                        strftime (atime_post, 256, "[%b %d %H:%M:%S]",
                                  localtime (&statpost->st_atime));
                        strftime (mtime_post, 256, "[%b %d %H:%M:%S]",
                                  localtime (&statpost->st_mtime));
                        strftime (ctime_post, 256, "[%b %d %H:%M:%S]",
                                  localtime (&statpost->st_ctime));

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, *statpre "
                                "{st_ino=%"PRIu64", st_mode=%o, st_uid=%d, "
                                "st_gid=%d, st_atime=%s, st_mtime=%s, "
                                "st_ctime=%s}, *statpost {st_ino=%"PRIu64", "
                                "st_mode=%o, st_uid=%d, st_gid=%d, st_atime=%s,"
                                " st_mtime=%s, st_ctime=%s})",
                                frame->root->unique, op_ret, statpre->st_ino,
                                statpre->st_mode, statpre->st_uid,
                                statpre->st_gid, atime_pre, mtime_pre,
                                ctime_pre, statpost->st_ino, statpost->st_mode,
                                statpost->st_uid, statpost->st_gid, atime_post,
                                mtime_post, ctime_post);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, op_errno=%d)",
                                frame->root->unique, op_ret, op_errno);
                }
        }

        STACK_UNWIND_STRICT (fsetattr, frame, op_ret, op_errno,
                             statpre, statpost);
        return 0;
}


int
trace_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno,
                  struct stat *preparent, struct stat *postparent)
{
        char *preparentstr = NULL;
        char *postparentstr = NULL;

        if (trace_fop_names[GF_FOP_UNLINK].enabled) {
                if (op_ret >= 0) {
                        preparentstr = trace_stat_to_str (preparent);
                        postparentstr = trace_stat_to_str (postparent);

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, *preparent = {%s}, "
                                "*postparent = {%s})",
                                frame->root->unique, op_ret, preparentstr,
                                postparentstr);

                        if (preparentstr)
                                FREE (preparentstr);

                        if (postparentstr)
                                FREE (postparentstr);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, op_errno=%d)",
                                frame->root->unique, op_ret, op_errno);
                }
        }

        STACK_UNWIND_STRICT (unlink, frame, op_ret, op_errno,
                             preparent, postparent);
        return 0;
}


int
trace_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct stat *buf,
                  struct stat *preoldparent, struct stat *postoldparent,
                  struct stat *prenewparent, struct stat *postnewparent)
{
        char  *statstr = NULL;
        char  *preoldparentstr = NULL;
        char  *postoldparentstr = NULL;
        char  *prenewparentstr = NULL;
        char  *postnewparentstr = NULL;

        if (trace_fop_names[GF_FOP_RENAME].enabled) {
                if (op_ret >= 0) {
                        statstr = trace_stat_to_str (buf);
                        preoldparentstr = trace_stat_to_str (preoldparent);
                        postoldparentstr = trace_stat_to_str (postoldparent);

                        prenewparentstr = trace_stat_to_str (prenewparent);
                        postnewparentstr = trace_stat_to_str (postnewparent);

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, *stbuf = {%s}, "
                                "*preoldparent = {%s}, *postoldparent = {%s}"
                                " *prenewparent = {%s}, *postnewparent = {%s})",
                                frame->root->unique, op_ret, statstr,
                                preoldparentstr, postoldparentstr,
                                prenewparentstr, postnewparentstr);

                        if (preoldparentstr)
                                FREE (preoldparentstr);

                        if (postoldparentstr)
                                FREE (postoldparentstr);

                        if (prenewparentstr)
                                FREE (prenewparentstr);

                        if (postnewparentstr)
                                FREE (postnewparentstr);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, op_errno=%d)",
                                frame->root->unique, op_ret, op_errno);
                }
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (op_ret=%d, op_errno=%d, buf {st_ino=%"PRIu64"})",
                        frame->root->unique, op_ret, op_errno,
                        (buf? buf->st_ino : 0));
        }

        STACK_UNWIND_STRICT (rename, frame, op_ret, op_errno, buf,
                             preoldparent, postoldparent,
                             prenewparent, postnewparent);
        return 0;
}


int
trace_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno,
                    const char *buf, struct stat *stbuf)
{
        char *statstr = NULL;

        if (trace_fop_names[GF_FOP_READLINK].enabled) {
                statstr = trace_stat_to_str (stbuf);

                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (op_ret=%d, op_errno=%d, buf=%s, "
                        "stbuf = { %s })",
                        frame->root->unique, op_ret, op_errno, buf, statstr);

                if (statstr)
                        FREE (statstr);
        }

        STACK_UNWIND_STRICT (readlink, frame, op_ret, op_errno, buf, stbuf);
        return 0;
}


int
trace_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno,
                  inode_t *inode, struct stat *buf,
                  dict_t *xattr, struct stat *postparent)
{
        char  *statstr = NULL;
        char  *postparentstr = NULL;

        if (trace_fop_names[GF_FOP_LOOKUP].enabled) {
                if (op_ret >= 0) {
                        statstr = trace_stat_to_str (buf);
                        postparentstr = trace_stat_to_str (buf);

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, ino=%"PRIu64", "
                                "*buf {%s}, *postparent {%s}",
                                frame->root->unique, op_ret, inode->ino,
                                statstr, postparentstr);

                        if (statstr)
                                FREE (statstr);
                        if (postparentstr)
                                FREE (postparentstr);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, op_errno=%d)",
                                frame->root->unique, op_ret, op_errno);
                }
        }

        STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno, inode, buf,
                             xattr, postparent);
        return 0;
}


int
trace_symlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno,
                   inode_t *inode, struct stat *buf,
                   struct stat *preparent, struct stat *postparent)
{
        char  *statstr = NULL;
        char  *preparentstr = NULL;
        char  *postparentstr = NULL;

        if (trace_fop_names[GF_FOP_SYMLINK].enabled) {
                if (op_ret >= 0) {
                        statstr = trace_stat_to_str (buf);
                        preparentstr = trace_stat_to_str (preparent);
                        postparentstr = trace_stat_to_str (postparent);

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, ino=%"PRIu64", "
                                "*stbuf = {%s}, *preparent = {%s}, "
                                "*postparent = {%s})",
                                frame->root->unique, op_ret, inode->ino,
                                statstr, preparentstr, postparentstr);

                        if (statstr)
                                FREE (statstr);

                        if (preparentstr)
                                FREE (preparentstr);

                        if (postparentstr)
                                FREE (postparentstr);

                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, op_errno=%d)",
                                frame->root->unique, op_ret, op_errno);
                }
        }

        STACK_UNWIND_STRICT (symlink, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent);
        return 0;
}


int
trace_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno,
                 inode_t *inode, struct stat *buf,
                 struct stat *preparent, struct stat *postparent)
{
        char *statstr = NULL;
        char *preparentstr = NULL;
        char *postparentstr = NULL;

        if (trace_fop_names[GF_FOP_MKNOD].enabled) {
                if (op_ret >= 0) {
                        statstr = trace_stat_to_str (buf);
                        preparentstr = trace_stat_to_str (preparent);
                        postparentstr = trace_stat_to_str (postparent);

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, ino=%"PRIu64", "
                                "*stbuf = {%s}, *preparent = {%s}, "
                                "*postparent = {%s})",
                                frame->root->unique, op_ret, inode->ino,
                                statstr, preparentstr, postparentstr);

                        if (statstr)
                                FREE (statstr);

                        if (preparentstr)
                                FREE (preparentstr);

                        if (postparentstr)
                                FREE (postparentstr);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, op_errno=%d)",
                                frame->root->unique, op_ret, op_errno);
                }
        }

        STACK_UNWIND_STRICT (mknod, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent);
        return 0;
}


int
trace_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno,
                 inode_t *inode, struct stat *buf,
                 struct stat *preparent, struct stat *postparent)
{
        char  *statstr = NULL;
        char  *preparentstr = NULL;
        char  *postparentstr = NULL;

        if (trace_fop_names[GF_FOP_MKDIR].enabled) {
                if (op_ret >= 0) {
                        statstr = trace_stat_to_str (buf);
                        preparentstr = trace_stat_to_str (preparent);
                        preparentstr = trace_stat_to_str (postparent);

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, ino = %"PRIu64
                                ", *stbuf = {%s}, *prebuf = {%s}, "
                                "*postbuf = {%s} )",
                                frame->root->unique, op_ret, buf->st_ino,
                                statstr, preparentstr, postparentstr);

                        if (statstr)
                                FREE (statstr);

                        if (preparentstr)
                                FREE (preparentstr);

                        if (postparentstr)
                                FREE (postparentstr);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, op_errno=%d)",
                                frame->root->unique, op_ret, op_errno);
                }
        }

        STACK_UNWIND_STRICT (mkdir, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent);
        return 0;
}


int
trace_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno,
                inode_t *inode, struct stat *buf,
                struct stat *preparent, struct stat *postparent)
{
        char  *statstr = NULL;
        char  *preparentstr = NULL;
        char  *postparentstr = NULL;

        if (trace_fop_names[GF_FOP_LINK].enabled) {
                if (op_ret >= 0) {
                        statstr = trace_stat_to_str (buf);
                        preparentstr = trace_stat_to_str (preparent);
                        preparentstr = trace_stat_to_str (postparent);

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, ino = %"PRIu64
                                ", *stbuf = {%s}, *prebuf = {%s}, "
                                "*postbuf = {%s})",
                                frame->root->unique, op_ret, buf->st_ino,
                                statstr, preparentstr, postparentstr);

                        if (statstr)
                                FREE (statstr);

                        if (preparentstr)
                                FREE (preparentstr);

                        if (postparentstr)
                                FREE (postparentstr);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, op_errno=%d)",
                                frame->root->unique, op_ret, op_errno);
                }
        }

        STACK_UNWIND_STRICT (link, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent);
        return 0;
}


int
trace_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno)
{
        if (trace_fop_names[GF_FOP_FLUSH].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (op_ret=%d, op_errno=%d)",
                        frame->root->unique, op_ret, op_errno);
        }

        STACK_UNWIND_STRICT (flush, frame, op_ret, op_errno);
        return 0;
}


int
trace_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, fd_t *fd)
{
        if (trace_fop_names[GF_FOP_OPENDIR].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (op_ret=%d, op_errno=%d, fd=%p)",
                        frame->root->unique, op_ret, op_errno, fd);
        }

        STACK_UNWIND_STRICT (opendir, frame, op_ret, op_errno, fd);
        return 0;
}


int
trace_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno,
                 struct stat *preparent, struct stat *postparent)
{
        char  *preparentstr = NULL;
        char  *postparentstr = NULL;

        if (trace_fop_names[GF_FOP_RMDIR].enabled) {
                if (op_ret >= 0) {
                        preparentstr = trace_stat_to_str (preparent);
                        preparentstr = trace_stat_to_str (postparent);

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, *prebuf = {%s}, "
                                "*postbuf = {%s}",
                                frame->root->unique, op_ret, preparentstr,
                                postparentstr);

                        if (preparentstr)
                                FREE (preparentstr);

                        if (postparentstr)
                                FREE (postparentstr);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, op_errno=%d)",
                                frame->root->unique, op_ret, op_errno);
                }
        }

        STACK_UNWIND_STRICT (rmdir, frame, op_ret, op_errno,
                             preparent, postparent);
        return 0;
}


int
trace_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno,
                    struct stat *prebuf, struct stat *postbuf)
{
        char  *preopstr = NULL;
        char  *postopstr = NULL;

        if (trace_fop_names[GF_FOP_TRUNCATE].enabled) {
                if (op_ret >= 0) {
                        preopstr = trace_stat_to_str (prebuf);
                        postopstr = trace_stat_to_str (prebuf);

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, *prebuf = {%s}, "
                                "*postbuf = {%s} )",
                                frame->root->unique, op_ret, preopstr,
                                postopstr);

                        if (preopstr)
                                FREE (preopstr);

                        if (postopstr)
                                FREE (postopstr);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, op_errno=%d)",
                                frame->root->unique, op_ret, op_errno);
                }
        }

        STACK_UNWIND_STRICT (truncate, frame, op_ret, op_errno, prebuf, postbuf);
        return 0;
}


int
trace_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct statvfs *buf)
{
        if (trace_fop_names[GF_FOP_STATFS].enabled) {
                if (op_ret >= 0) {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": ({f_bsize=%lu, f_frsize=%lu, f_blocks=%"GF_PRI_FSBLK
                                ", f_bfree=%"GF_PRI_FSBLK", f_bavail=%"GF_PRI_FSBLK", "
                                "f_files=%"GF_PRI_FSBLK", f_ffree=%"GF_PRI_FSBLK", f_favail=%"
                                GF_PRI_FSBLK", f_fsid=%lu, f_flag=%lu, f_namemax=%lu}) => ret=%d",
                                frame->root->unique, buf->f_bsize, buf->f_frsize, buf->f_blocks,
                                buf->f_bfree, buf->f_bavail, buf->f_files, buf->f_ffree,
                                buf->f_favail, buf->f_fsid, buf->f_flag, buf->f_namemax, op_ret);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, op_errno=%d)",
                                frame->root->unique, op_ret, op_errno);
                }
        }

        STACK_UNWIND_STRICT (statfs, frame, op_ret, op_errno, buf);
        return 0;
}


int
trace_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno)
{
        if (trace_fop_names[GF_FOP_SETXATTR].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (op_ret=%d, op_errno=%d)",
                        frame->root->unique, op_ret, op_errno);
        }

        STACK_UNWIND_STRICT (setxattr, frame, op_ret, op_errno);
        return 0;
}


int
trace_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *dict)
{
        if (trace_fop_names[GF_FOP_GETXATTR].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (op_ret=%d, op_errno=%d, dict=%p)",
                        frame->root->unique, op_ret, op_errno, dict);
        }

        STACK_UNWIND_STRICT (getxattr, frame, op_ret, op_errno, dict);

        return 0;
}


int
trace_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno)
{
        if (trace_fop_names[GF_FOP_REMOVEXATTR].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (op_ret=%d, op_errno=%d)",
                        frame->root->unique, op_ret, op_errno);
        }

        STACK_UNWIND_STRICT (removexattr, frame, op_ret, op_errno);

        return 0;
}


int
trace_fsyncdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno)
{
        if (trace_fop_names[GF_FOP_FSYNCDIR].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (op_ret=%d, op_errno=%d)",
                        frame->root->unique, op_ret, op_errno);
        }

        STACK_UNWIND_STRICT (fsyncdir, frame, op_ret, op_errno);
        return 0;
}


int
trace_access_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno)
{
        if (trace_fop_names[GF_FOP_ACCESS].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (op_ret=%d, op_errno=%d)",
                        frame->root->unique, op_ret, op_errno);
        }

        STACK_UNWIND_STRICT (access, frame, op_ret, op_errno);
        return 0;
}


int
trace_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno,
                     struct stat *prebuf, struct stat *postbuf)
{
        char  *prebufstr = NULL;
        char  *postbufstr = NULL;

        if (trace_fop_names[GF_FOP_FTRUNCATE].enabled) {
                if (op_ret >= 0) {
                        prebufstr = trace_stat_to_str (prebuf);
                        postbufstr = trace_stat_to_str (postbuf);

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, *prebuf = {%s}, "
                                "*postbuf = {%s} )",
                                frame->root->unique, op_ret,
                                prebufstr, postbufstr);

                        if (prebufstr)
                                FREE (prebufstr);

                        if (postbufstr)
                                FREE (postbufstr);

                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, op_errno=%d)",
                                frame->root->unique, op_ret, op_errno);
                }
        }

        STACK_UNWIND_STRICT (ftruncate, frame, op_ret, op_errno, prebuf, postbuf);
        return 0;
}


int
trace_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct stat *buf)
{
        char atime_buf[256];
        char mtime_buf[256];
        char ctime_buf[256];

        if (trace_fop_names[GF_FOP_FSTAT].enabled) {
                if (op_ret >= 0) {
                        strftime (atime_buf, 256, "[%b %d %H:%M:%S]",
                                  localtime (&buf->st_atime));
                        strftime (mtime_buf, 256, "[%b %d %H:%M:%S]",
                                  localtime (&buf->st_mtime));
                        strftime (ctime_buf, 256, "[%b %d %H:%M:%S]",
                                  localtime (&buf->st_ctime));

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, *buf {st_dev=%"GF_PRI_DEV", "
                                "st_ino=%"PRIu64", st_mode=%o, st_nlink=%"GF_PRI_NLINK", "
                                "st_uid=%d, st_gid=%d, st_rdev=%"GF_PRI_DEV", st_size=%"PRId64", "
                                "st_blksize=%"GF_PRI_BLKSIZE", st_blocks=%"PRId64", st_atime=%s, "
                                "st_mtime=%s, st_ctime=%s})",
                                frame->root->unique, op_ret, buf->st_dev, buf->st_ino,
                                buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid,
                                buf->st_rdev, buf->st_size, buf->st_blksize,
                                buf->st_blocks, atime_buf, mtime_buf, ctime_buf);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, op_errno=%d)",
                                frame->root->unique, op_ret, op_errno);
                }
        }

        STACK_UNWIND_STRICT (fstat, frame, op_ret, op_errno, buf);
        return 0;
}


int
trace_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno, struct flock *lock)
{
        if (trace_fop_names[GF_FOP_LK].enabled) {
                if (op_ret >= 0) {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, {l_type=%d, l_whence=%d, "
                                "l_start=%"PRId64", l_len=%"PRId64", l_pid=%u})",
                                frame->root->unique, op_ret, lock->l_type, lock->l_whence,
                                lock->l_start, lock->l_len, lock->l_pid);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, op_errno=%d)",
                                frame->root->unique, op_ret, op_errno);
                }
        }

        STACK_UNWIND_STRICT (lk, frame, op_ret, op_errno, lock);
        return 0;
}


int
trace_setdents_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno)
{
        if (trace_fop_names[GF_FOP_SETDENTS].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": op_ret=%d, op_errno=%d",
                        frame->root->unique, op_ret, op_errno);
        }

        STACK_UNWIND_STRICT (setdents, frame, op_ret, op_errno);
        return 0;
}


int
trace_entrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno)
{
        if (trace_fop_names[GF_FOP_ENTRYLK].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": op_ret=%d, op_errno=%d",
                        frame->root->unique, op_ret, op_errno);
        }

        STACK_UNWIND_STRICT (entrylk, frame, op_ret, op_errno);
        return 0;
}


int
trace_xattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, dict_t *dict)
{
        if (trace_fop_names[GF_FOP_XATTROP].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (op_ret=%d, op_errno=%d)",
                        frame->root->unique, op_ret, op_errno);
        }

        STACK_UNWIND_STRICT (xattrop, frame, op_ret, op_errno, dict);
        return 0;
}


int
trace_fxattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *dict)
{
        if (trace_fop_names[GF_FOP_FXATTROP].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (op_ret=%d, op_errno=%d)",
                        frame->root->unique, op_ret, op_errno);
        }

        STACK_UNWIND_STRICT (fxattrop, frame, op_ret, op_errno, dict);
        return 0;
}


int
trace_inodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno)
{
        if (trace_fop_names[GF_FOP_INODELK].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": op_ret=%d, op_errno=%d",
                        frame->root->unique, op_ret, op_errno);
        }

        STACK_UNWIND_STRICT (inodelk, frame, op_ret, op_errno);
        return 0;
}


int
trace_entrylk (call_frame_t *frame, xlator_t *this,
               const char *volume, loc_t *loc, const char *basename,
               entrylk_cmd cmd, entrylk_type type)
{
        if (trace_fop_names[GF_FOP_ENTRYLK].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": volume=%s, (loc= {path=%s, ino=%"PRIu64"} basename=%s, cmd=%s, type=%s)",
                        frame->root->unique, volume, loc->path, loc->inode->ino, basename,
                        ((cmd == ENTRYLK_LOCK) ? "ENTRYLK_LOCK" : "ENTRYLK_UNLOCK"),
                        ((type == ENTRYLK_RDLCK) ? "ENTRYLK_RDLCK" : "ENTRYLK_WRLCK"));
        }

        STACK_WIND (frame, trace_entrylk_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->entrylk,
                    volume, loc, basename, cmd, type);
        return 0;
}


int
trace_inodelk (call_frame_t *frame, xlator_t *this, const char *volume,
               loc_t *loc, int32_t cmd, struct flock *flock)
{
        if (trace_fop_names[GF_FOP_INODELK].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": volume=%s, (loc {path=%s, ino=%"PRIu64"}, cmd=%s)",
                        frame->root->unique, volume, loc->path, loc->inode->ino,
                        ((cmd == F_SETLK)? "F_SETLK" : "unknown"));
        }

        STACK_WIND (frame, trace_inodelk_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->inodelk,
                    volume, loc, cmd, flock);
        return 0;
}


int
trace_finodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno)
{
        if (trace_fop_names[GF_FOP_FINODELK].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": op_ret=%d, op_errno=%d",
                        frame->root->unique, op_ret, op_errno);
        }

        STACK_UNWIND_STRICT (finodelk, frame, op_ret, op_errno);
        return 0;
}


int
trace_finodelk (call_frame_t *frame, xlator_t *this, const char *volume,
                fd_t *fd, int32_t cmd, struct flock *flock)
{
        if (trace_fop_names[GF_FOP_FINODELK].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": volume=%s, (fd=%p, cmd=%s)",
                        frame->root->unique, volume, fd,
                        ((cmd == F_SETLK) ? "F_SETLK" : "unknown"));
        }

        STACK_WIND (frame, trace_finodelk_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->finodelk,
                    volume, fd, cmd, flock);
        return 0;
}


int
trace_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
               gf_xattrop_flags_t flags, dict_t *dict)
{
        if (trace_fop_names[GF_FOP_XATTROP].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (path=%s, ino=%"PRIu64" flags=%d)",
                        frame->root->unique, loc->path, loc->inode->ino, flags);

        }

        STACK_WIND (frame, trace_xattrop_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->xattrop,
                    loc, flags, dict);

        return 0;
}


int
trace_fxattrop (call_frame_t *frame, xlator_t *this, fd_t *fd,
                gf_xattrop_flags_t flags, dict_t *dict)
{
        if (trace_fop_names[GF_FOP_FXATTROP].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (fd=%p, flags=%d)",
                        frame->root->unique, fd, flags);

        }

        STACK_WIND (frame, trace_fxattrop_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fxattrop,
                    fd, flags, dict);

        return 0;
}


int
trace_lookup (call_frame_t *frame, xlator_t *this,
              loc_t *loc, dict_t *xattr_req)
{
        if (trace_fop_names[GF_FOP_LOOKUP].enabled) {
                /* TODO: print all the keys mentioned in xattr_req */
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (loc {path=%s, ino=%"PRIu64"})",
                        frame->root->unique, loc->path,
                        loc->inode->ino);
        }

        STACK_WIND (frame, trace_lookup_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup,
                    loc, xattr_req);

        return 0;
}


int
trace_stat (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        if (trace_fop_names[GF_FOP_STAT].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (loc {path=%s, ino=%"PRIu64"})",
                        frame->root->unique, loc->path, loc->inode->ino);
        }

        STACK_WIND (frame, trace_stat_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->stat,
                    loc);

        return 0;
}


int
trace_readlink (call_frame_t *frame, xlator_t *this, loc_t *loc, size_t size)
{
        if (trace_fop_names[GF_FOP_READLINK].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (loc {path=%s, ino=%"PRIu64"}, size=%"GF_PRI_SIZET")",
                        frame->root->unique, loc->path, loc->inode->ino, size);
        }

        STACK_WIND (frame, trace_readlink_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readlink,
                    loc, size);

        return 0;
}


int
trace_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc,
             mode_t mode, dev_t dev)
{
        if (trace_fop_names[GF_FOP_MKNOD].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (loc {path=%s, ino=%"PRIu64"}, mode=%d, dev=%"GF_PRI_DEV")",
                        frame->root->unique, loc->path, loc->inode->ino, mode, dev);
        }

        STACK_WIND (frame, trace_mknod_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->mknod,
                    loc, mode, dev);

        return 0;
}


int
trace_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode)
{
        if (trace_fop_names[GF_FOP_MKDIR].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (path=%s, ino=%"PRIu64", mode=%d)",
                        frame->root->unique, loc->path,
                        ((loc->inode)? loc->inode->ino : 0), mode);
        }

        STACK_WIND (frame, trace_mkdir_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->mkdir,
                    loc, mode);
        return 0;
}


int
trace_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        if (trace_fop_names[GF_FOP_UNLINK].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (loc {path=%s, ino=%"PRIu64"})",
                        frame->root->unique, loc->path, loc->inode->ino);
        }

        STACK_WIND (frame, trace_unlink_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->unlink,
                    loc);
        return 0;
}


int
trace_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        if (trace_fop_names[GF_FOP_RMDIR].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (loc {path=%s, ino=%"PRIu64"})",
                        frame->root->unique, loc->path, loc->inode->ino);
        }

        STACK_WIND (frame, trace_rmdir_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rmdir,
                    loc);

        return 0;
}


int
trace_symlink (call_frame_t *frame, xlator_t *this, const char *linkpath,
               loc_t *loc)
{
        if (trace_fop_names[GF_FOP_SYMLINK].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (linkpath=%s, loc {path=%s, ino=%"PRIu64"})",
                        frame->root->unique, linkpath, loc->path,
                        ((loc->inode)? loc->inode->ino : 0));
        }

        STACK_WIND (frame, trace_symlink_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->symlink,
                    linkpath, loc);

        return 0;
}


int
trace_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc)
{
        if (trace_fop_names[GF_FOP_RENAME].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (oldloc {path=%s, ino=%"PRIu64"}, "
                        "newloc{path=%s, ino=%"PRIu64"})",
                        frame->root->unique, oldloc->path, oldloc->ino,
                        newloc->path, newloc->ino);
        }

        STACK_WIND (frame, trace_rename_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rename,
                    oldloc, newloc);

        return 0;
}


int
trace_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc)
{
        if (trace_fop_names[GF_FOP_LINK].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (oldloc {path=%s, ino=%"PRIu64"}, "
                        "newloc {path=%s, ino=%"PRIu64"})",
                        frame->root->unique, oldloc->path, oldloc->inode->ino,
                        newloc->path, newloc->inode->ino);
        }

        STACK_WIND (frame, trace_link_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->link,
                    oldloc, newloc);
        return 0;
}


int
trace_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
               struct stat *stbuf, int32_t valid)
{
        char actime_str[256] = {0,};
        char modtime_str[256] = {0,};

        if (trace_fop_names[GF_FOP_SETATTR].enabled) {
                if (valid & GF_SET_ATTR_MODE) {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (loc {path=%s, ino=%"PRIu64"},"
                                " mode=%o)", frame->root->unique, loc->path,
                                loc->inode->ino, stbuf->st_mode);
                }

                if (valid & (GF_SET_ATTR_UID | GF_SET_ATTR_GID)) {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (loc {path=%s, ino=%"PRIu64"},"
                                " uid=%o, gid=%o)",
                                frame->root->unique, loc->path, loc->inode->ino,
                                stbuf->st_uid, stbuf->st_gid);
                }

                if (valid & (GF_SET_ATTR_ATIME | GF_SET_ATTR_MTIME)) {
                        strftime (actime_str, 256, "[%b %d %H:%M:%S]",
                                  localtime (&stbuf->st_atime));
                        strftime (modtime_str, 256, "[%b %d %H:%M:%S]",
                                  localtime (&stbuf->st_mtime));

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (loc {path=%s, ino=%"PRIu64"}, "
                                "*stbuf=%p {st_atime=%s, st_mtime=%s})",
                                frame->root->unique, loc->path, loc->inode->ino,
                                stbuf, actime_str, modtime_str);
                }
        }

        STACK_WIND (frame, trace_setattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setattr,
                    loc, stbuf, valid);

        return 0;
}


int
trace_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                struct stat *stbuf, int32_t valid)
{
        char actime_str[256] = {0,};
        char modtime_str[256] = {0,};

        if (trace_fop_names[GF_FOP_FSETATTR].enabled) {
                if (valid & GF_SET_ATTR_MODE) {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (*fd=%p, mode=%o)",
                                frame->root->unique, fd,
                                stbuf->st_mode);
                }

                if (valid & (GF_SET_ATTR_UID | GF_SET_ATTR_GID)) {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (*fd=%p, uid=%o, gid=%o)",
                                frame->root->unique, fd,
                                stbuf->st_uid, stbuf->st_gid);
                }

                if (valid & (GF_SET_ATTR_ATIME | GF_SET_ATTR_MTIME)) {
                        strftime (actime_str, 256, "[%b %d %H:%M:%S]",
                                  localtime (&stbuf->st_atime));
                        strftime (modtime_str, 256, "[%b %d %H:%M:%S]",
                                  localtime (&stbuf->st_mtime));

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (*fd=%p"
                                "*stbuf=%p {st_atime=%s, st_mtime=%s})",
                                frame->root->unique, fd, stbuf, actime_str,
                                modtime_str);
                }
        }

        STACK_WIND (frame, trace_fsetattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsetattr,
                    fd, stbuf, valid);

        return 0;
}


int
trace_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc,
                off_t offset)
{
        if (trace_fop_names[GF_FOP_TRUNCATE].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (loc {path=%s, ino=%"PRIu64"}, offset=%"PRId64")",
                        frame->root->unique, loc->path, loc->inode->ino, offset);
        }

        STACK_WIND (frame, trace_truncate_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->truncate,
                    loc, offset);

        return 0;
}


int
trace_open (call_frame_t *frame, xlator_t *this, loc_t *loc,
            int32_t flags, fd_t *fd, int32_t wbflags)
{
        if (trace_fop_names[GF_FOP_OPEN].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (loc {path=%s, ino=%"PRIu64"}, flags=%d, "
                        "fd=%p, wbflags=%d)",
                        frame->root->unique, loc->path, loc->inode->ino, flags,
                        fd, wbflags);
        }

        STACK_WIND (frame, trace_open_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->open,
                    loc, flags, fd, wbflags);
        return 0;
}


int
trace_create (call_frame_t *frame, xlator_t *this, loc_t *loc,
              int32_t flags, mode_t mode, fd_t *fd)
{
        if (trace_fop_names[GF_FOP_CREATE].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (loc {path=%s, ino=%"PRIu64"}, flags=0%o mode=0%o)",
                        frame->root->unique, loc->path, loc->inode->ino, flags, mode);
        }

        STACK_WIND (frame, trace_create_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->create,
                    loc, flags, mode, fd);
        return 0;
}


int
trace_readv (call_frame_t *frame, xlator_t *this, fd_t *fd,
             size_t size, off_t offset)
{
        if (trace_fop_names[GF_FOP_READ].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (*fd=%p, size=%"GF_PRI_SIZET", offset=%"PRId64")",
                        frame->root->unique, fd, size, offset);
        }

        STACK_WIND (frame, trace_readv_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readv,
                    fd, size, offset);
        return 0;
}


int
trace_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
              struct iovec *vector, int32_t count,
              off_t offset, struct iobref *iobref)
{
        if (trace_fop_names[GF_FOP_WRITE].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (*fd=%p, *vector=%p, count=%d, offset=%"PRId64")",
                        frame->root->unique, fd, vector, count, offset);
        }

        STACK_WIND (frame, trace_writev_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->writev,
                    fd, vector, count, offset, iobref);
        return 0;
}


int
trace_statfs (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        if (trace_fop_names[GF_FOP_STATFS].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (loc {path=%s, ino=%"PRIu64"})",
                        frame->root->unique, loc->path,
                        ((loc->inode)? loc->inode->ino : 0));
        }

        STACK_WIND (frame, trace_statfs_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->statfs,
                    loc);
        return 0;
}


int
trace_flush (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
        if (trace_fop_names[GF_FOP_FLUSH].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (*fd=%p)",
                        frame->root->unique, fd);
        }

        STACK_WIND (frame, trace_flush_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->flush,
                    fd);
        return 0;
}


int
trace_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags)
{
        if (trace_fop_names[GF_FOP_FSYNC].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (flags=%d, *fd=%p)",
                        frame->root->unique, flags, fd);
        }

        STACK_WIND (frame, trace_fsync_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsync,
                    fd, flags);
        return 0;
}


int
trace_setxattr (call_frame_t *frame, xlator_t *this,
                loc_t *loc, dict_t *dict, int32_t flags)
{
        if (trace_fop_names[GF_FOP_SETXATTR].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (loc {path=%s, ino=%"PRIu64"}, dict=%p, flags=%d)",
                        frame->root->unique, loc->path,
                        ((loc->inode)? loc->inode->ino : 0), dict, flags);
        }

        STACK_WIND (frame, trace_setxattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setxattr,
                    loc, dict, flags);
        return 0;
}


int
trace_getxattr (call_frame_t *frame, xlator_t *this,
                loc_t *loc, const char *name)
{
        if (trace_fop_names[GF_FOP_GETXATTR].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (loc {path=%s, ino=%"PRIu64"}), name=%s",
                        frame->root->unique, loc->path,
                        ((loc->inode)? loc->inode->ino : 0), name);
        }

        STACK_WIND (frame, trace_getxattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->getxattr,
                    loc, name);
        return 0;
}


int
trace_removexattr (call_frame_t *frame, xlator_t *this,
                   loc_t *loc, const char *name)
{
        if (trace_fop_names[GF_FOP_REMOVEXATTR].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (loc {path=%s, ino=%"PRIu64"}, name=%s)",
                        frame->root->unique, loc->path,
                        ((loc->inode)? loc->inode->ino : 0), name);
        }

        STACK_WIND (frame, trace_removexattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->removexattr,
                    loc, name);

        return 0;
}


int
trace_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd)
{
        if (trace_fop_names[GF_FOP_OPENDIR].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64":( loc {path=%s, ino=%"PRIu64"}, fd=%p)",
                        frame->root->unique, loc->path, loc->inode->ino, fd);
        }

        STACK_WIND (frame, trace_opendir_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->opendir,
                    loc, fd);
        return 0;
}


int
trace_getdents (call_frame_t *frame, xlator_t *this, fd_t *fd,
                size_t size, off_t offset, int32_t flag)
{
        if (trace_fop_names[GF_FOP_GETDENTS].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (fd=%p, size=%"GF_PRI_SIZET", offset=%"PRId64", flag=0x%x)",
                        frame->root->unique, fd, size, offset, flag);
        }

        STACK_WIND (frame, trace_getdents_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->getdents,
                    fd, size, offset, flag);
        return 0;
}


int
trace_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                off_t offset)
{
        if (trace_fop_names[GF_FOP_READDIRP].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (fd=%p, size=%"GF_PRI_SIZET", offset=%"PRId64")",
                        frame->root->unique, fd, size, offset);
        }

        STACK_WIND (frame, trace_readdirp_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readdirp,
                    fd, size, offset);

        return 0;
}


int
trace_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd,
               size_t size, off_t offset)
{
        if (trace_fop_names[GF_FOP_READDIR].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (fd=%p, size=%"GF_PRI_SIZET", offset=%"PRId64")",
                        frame->root->unique, fd, size, offset);
        }

        STACK_WIND (frame, trace_readdir_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readdir,
                    fd, size, offset);

        return 0;
}


int
trace_fsyncdir (call_frame_t *frame, xlator_t *this,
                fd_t *fd, int32_t datasync)
{
        if (trace_fop_names[GF_FOP_FSYNCDIR].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (datasync=%d, *fd=%p)",
                        frame->root->unique, datasync, fd);
        }

        STACK_WIND (frame, trace_fsyncdir_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsyncdir,
                    fd, datasync);
        return 0;
}


int
trace_access (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t mask)
{
        if (trace_fop_names[GF_FOP_ACCESS].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (*loc {path=%s, ino=%"PRIu64"}, mask=0%o)",
                        frame->root->unique, loc->path,
                        ((loc->inode)? loc->inode->ino : 0), mask);
        }

        STACK_WIND (frame, trace_access_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->access,
                    loc, mask);
        return 0;
}


int
trace_ftruncate (call_frame_t *frame, xlator_t *this,
                 fd_t *fd, off_t offset)
{
        if (trace_fop_names[GF_FOP_FTRUNCATE].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (offset=%"PRId64", *fd=%p)",
                        frame->root->unique, offset, fd);
        }

        STACK_WIND (frame, trace_ftruncate_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->ftruncate,
                    fd, offset);

        return 0;
}


int
trace_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
        if (trace_fop_names[GF_FOP_FSTAT].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (*fd=%p)",
                        frame->root->unique, fd);
        }

        STACK_WIND (frame, trace_fstat_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fstat,
                    fd);
        return 0;
}


int
trace_lk (call_frame_t *frame, xlator_t *this, fd_t *fd,
          int32_t cmd, struct flock *lock)
{
        if (trace_fop_names[GF_FOP_LK].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (*fd=%p, cmd=%d, lock {l_type=%d, l_whence=%d, "
                        "l_start=%"PRId64", l_len=%"PRId64", l_pid=%u})",
                        frame->root->unique, fd, cmd, lock->l_type, lock->l_whence,
                        lock->l_start, lock->l_len, lock->l_pid);
        }

        STACK_WIND (frame, trace_lk_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lk,
                    fd, cmd, lock);
        return 0;
}


int
trace_setdents (call_frame_t *frame, xlator_t *this, fd_t *fd,
                int32_t flags, dir_entry_t *entries, int32_t count)
{
        if (trace_fop_names[GF_FOP_SETDENTS].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": (*fd=%p, flags=%d, count=%d",
                        frame->root->unique, fd, flags, count);
        }

        STACK_WIND (frame, trace_setdents_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setdents,
                    fd, flags, entries, count);
        return 0;
}


int
trace_checksum_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno,
                    uint8_t *fchecksum, uint8_t *dchecksum)
{
        gf_log (this->name, GF_LOG_NORMAL,
                "%"PRId64": op_ret (%d), op_errno(%d)",
                frame->root->unique, op_ret, op_errno);

        STACK_UNWIND_STRICT (checksum, frame, op_ret, op_errno,
                             fchecksum, dchecksum);

        return 0;
}


int
trace_checksum (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flag)
{
        gf_log (this->name, GF_LOG_NORMAL,
                "%"PRId64": loc->path (%s) flag (%d)",
                frame->root->unique, loc->path, flag);

        STACK_WIND (frame, trace_checksum_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->checksum,
                    loc, flag);

        return 0;
}


int
trace_stats_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno,
                 struct xlator_stats *stats)
{
        gf_log (this->name, GF_LOG_NORMAL,
                "%"PRId64": op_ret (%d), op_errno(%d)",
                frame->root->unique, op_ret, op_errno);

        STACK_UNWIND (frame, op_ret, op_errno, stats);
        return 0;
}


int
trace_stats (call_frame_t *frame, xlator_t *this, int32_t flags)
{
        gf_log (this->name, GF_LOG_NORMAL,
                "%"PRId64": (flags=%d)",
                frame->root->unique, flags);

        STACK_WIND (frame, trace_stats_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->mops->stats,
                    flags);

        return 0;
}


void
enable_all_calls (int enabled)
{
        int i;

        for (i = 0; i < GF_FOP_MAXVALUE; i++)
                trace_fop_names[i].enabled = enabled;
}


void
enable_call (const char *name, int enabled)
{
        int i;
        for (i = 0; i < GF_FOP_MAXVALUE; i++)
                if (!strcasecmp(trace_fop_names[i].name, name))
                        trace_fop_names[i].enabled = enabled;
}


/*
  include = 1 for "include-ops"
  = 0 for "exclude-ops"
*/
void
process_call_list (const char *list, int include)
{
        enable_all_calls (include ? 0 : 1);

        char *call = strsep ((char **)&list, ",");

        while (call) {
                enable_call (call, include);
                call = strsep ((char **)&list, ",");
        }
}


int32_t
init (xlator_t *this)
{
        dict_t *options = NULL;
        char *includes = NULL, *excludes = NULL;

        if (!this)
                return -1;

        if (!this->children || this->children->next) {
                gf_log (this->name, GF_LOG_ERROR,
                        "trace translator requires one subvolume");
                return -1;
        }
        if (!this->parents) {
                gf_log (this->name, GF_LOG_WARNING,
                        "dangling volume. check volfile ");
        }


        options = this->options;
        includes = data_to_str (dict_get (options, "include-ops"));
        excludes = data_to_str (dict_get (options, "exclude-ops"));

        {
                int i;
                for (i = 0; i < GF_FOP_MAXVALUE; i++) {
                        trace_fop_names[i].name = (gf_fop_list[i] ?
                                                   gf_fop_list[i] : ":O");
                        trace_fop_names[i].enabled = 1;
                }
        }

        if (includes && excludes) {
                gf_log (this->name,
                        GF_LOG_ERROR,
                        "must specify only one of 'include-ops' and 'exclude-ops'");
                return -1;
        }
        if (includes)
                process_call_list (includes, 1);
        if (excludes)
                process_call_list (excludes, 0);

        if (gf_log_get_loglevel () < GF_LOG_NORMAL)
                gf_log_set_loglevel (GF_LOG_NORMAL);

        /* Set this translator's inode table pointer to child node's pointer. */
        this->itable = FIRST_CHILD (this)->itable;

        return 0;
}

void
fini (xlator_t *this)
{
        if (!this)
                return;

        gf_log (this->name, GF_LOG_NORMAL,
                "trace translator unloaded");
        return;
}

struct xlator_fops fops = {
        .stat        = trace_stat,
        .readlink    = trace_readlink,
        .mknod       = trace_mknod,
        .mkdir       = trace_mkdir,
        .unlink      = trace_unlink,
        .rmdir       = trace_rmdir,
        .symlink     = trace_symlink,
        .rename      = trace_rename,
        .link        = trace_link,
        .truncate    = trace_truncate,
        .open        = trace_open,
        .readv       = trace_readv,
        .writev      = trace_writev,
        .statfs      = trace_statfs,
        .flush       = trace_flush,
        .fsync       = trace_fsync,
        .setxattr    = trace_setxattr,
        .getxattr    = trace_getxattr,
        .removexattr = trace_removexattr,
        .opendir     = trace_opendir,
        .readdir     = trace_readdir,
        .readdirp    = trace_readdirp,
        .fsyncdir    = trace_fsyncdir,
        .access      = trace_access,
        .ftruncate   = trace_ftruncate,
        .fstat       = trace_fstat,
        .create      = trace_create,
        .lk          = trace_lk,
        .inodelk     = trace_inodelk,
        .finodelk    = trace_finodelk,
        .entrylk     = trace_entrylk,
        .lookup      = trace_lookup,
        .setdents    = trace_setdents,
        .getdents    = trace_getdents,
        .checksum    = trace_checksum,
        .xattrop     = trace_xattrop,
        .fxattrop    = trace_fxattrop,
        .setattr     = trace_setattr,
        .fsetattr    = trace_fsetattr,
};

struct xlator_mops mops = {
        .stats    = trace_stats,
};

struct xlator_cbks cbks = {
};

struct volume_options options[] = {
        { .key  = {"include-ops", "include"},
          .type = GF_OPTION_TYPE_STR,
          /*.value = { ""} */
        },
        { .key  = {"exclude-ops", "exclude"},
          .type = GF_OPTION_TYPE_STR
          /*.value = { ""} */
        },
        { .key  = {NULL} },
};
