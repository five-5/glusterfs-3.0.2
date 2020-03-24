/*
   Copyright (c) 2008-2009 Gluster, Inc. <http://www.gluster.com>
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
#include "byte-order.h"

#include "afr-transaction.h"
#include "afr-self-heal.h"
#include "afr-self-heal-common.h"


int
afr_sh_metadata_done (call_frame_t *frame, xlator_t *this)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;
	int              i = 0;

	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

//	memset (sh->child_errno, 0, sizeof (int) * priv->child_count);
	memset (sh->buf, 0, sizeof (struct stat) * priv->child_count);
	memset (sh->success, 0, sizeof (int) * priv->child_count);

        for (i = 0; i < priv->child_count; i++) {
                sh->locked_nodes[i] = 1;
        }

	for (i = 0; i < priv->child_count; i++) {
		if (sh->xattr[i])
			dict_unref (sh->xattr[i]);
		sh->xattr[i] = NULL;
	}

	if (local->govinda_gOvinda) {
		gf_log (this->name, GF_LOG_DEBUG,
			"aborting selfheal of %s",
			local->loc.path);
		sh->completion_cbk (frame, this);
	} else {
		if (S_ISREG (sh->mode)) {
			gf_log (this->name, GF_LOG_TRACE,
				"proceeding to data check on %s",
				local->loc.path);
			afr_self_heal_data (frame, this);
			return 0;
		}

		if (S_ISDIR (sh->mode)) {
			gf_log (this->name, GF_LOG_TRACE,
				"proceeding to entry check on %s",
				local->loc.path);
			afr_self_heal_entry (frame, this);
			return 0;
		}
		gf_log (this->name, GF_LOG_DEBUG,
			"completed self heal of %s",
			local->loc.path);

		sh->completion_cbk (frame, this);
	}

	return 0;
}


int
afr_sh_metadata_unlck_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			   int32_t op_ret, int32_t op_errno)
{
	afr_local_t      *local = NULL;
	int               call_count = 0;


	local = frame->local;

	call_count = afr_frame_return (frame);

	if (call_count == 0)
		afr_sh_metadata_done (frame, this);

	return 0;
}


int
afr_sh_metadata_finish (call_frame_t *frame, xlator_t *this)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;
	int              i = 0;
	int              call_count = 0;
	struct flock     flock = {0, };


	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

        for (i = 0; i < priv->child_count; i++) {
                if (sh->locked_nodes[i])
                        call_count++;
        }

        if (call_count == 0) {
                afr_sh_metadata_done (frame, this);
                return 0;
        }

	local->call_count = call_count;

	for (i = 0; i < priv->child_count; i++) {
		flock.l_start   = 0;
		flock.l_len     = 0;
		flock.l_type    = F_UNLCK;

		if (sh->locked_nodes[i]) {
			gf_log (this->name, GF_LOG_TRACE,
				"unlocking %s on subvolume %s",
				local->loc.path, priv->children[i]->name);

			STACK_WIND (frame, afr_sh_metadata_unlck_cbk,
				    priv->children[i],
				    priv->children[i]->fops->inodelk,
                                    this->name,
				    &local->loc, F_SETLK, &flock);

			if (!--call_count)
				break;
		}
	}

	return 0;
}


int
afr_sh_metadata_erase_pending_cbk (call_frame_t *frame, void *cookie,
				   xlator_t *this, int32_t op_ret,
				   int32_t op_errno, dict_t *xattr)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;
	int             call_count = 0;

	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	LOCK (&frame->lock);
	{
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0)
		afr_sh_metadata_finish (frame, this);

	return 0;
}


int
afr_sh_metadata_erase_pending (call_frame_t *frame, xlator_t *this)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;
	int              call_count = 0;
	int              i = 0;
	dict_t          **erase_xattr = NULL;


	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	afr_sh_pending_to_delta (priv, sh->xattr, sh->delta_matrix, 
                                 sh->success, priv->child_count,
                                 AFR_METADATA_TRANSACTION);

	erase_xattr = CALLOC (sizeof (*erase_xattr), priv->child_count);

	for (i = 0; i < priv->child_count; i++) {
		if (sh->xattr[i]) {
			call_count++;

			erase_xattr[i] = get_new_dict();
			dict_ref (erase_xattr[i]);
		}
	}

	afr_sh_delta_to_xattr (priv, sh->delta_matrix, erase_xattr,
			       priv->child_count, AFR_METADATA_TRANSACTION);

	local->call_count = call_count;

	if (call_count == 0) {
		gf_log (this->name, GF_LOG_WARNING,
			"metadata of %s not healed on any subvolume",
			local->loc.path);

		afr_sh_metadata_finish (frame, this);
	}

	for (i = 0; i < priv->child_count; i++) {
		if (!erase_xattr[i])
			continue;

		gf_log (this->name, GF_LOG_TRACE,
			"erasing pending flags from %s on %s",
			local->loc.path, priv->children[i]->name);

		STACK_WIND_COOKIE (frame, afr_sh_metadata_erase_pending_cbk,
				   (void *) (long) i,
				   priv->children[i],
				   priv->children[i]->fops->xattrop,
				   &local->loc,
				   GF_XATTROP_ADD_ARRAY, erase_xattr[i]);
		if (!--call_count)
			break;
	}

	for (i = 0; i < priv->child_count; i++) {
		if (erase_xattr[i]) {
			dict_unref (erase_xattr[i]);
		}
	}
	FREE (erase_xattr);

	return 0;
}


int
afr_sh_metadata_sync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			  int32_t op_ret, int32_t op_errno)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;
	int              call_count = 0;
	int              child_index = 0;


	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	child_index = (long) cookie;

	LOCK (&frame->lock);
	{
		if (op_ret == -1) {
			gf_log (this->name, GF_LOG_DEBUG,
				"setting attributes failed for %s on %s (%s)",
				local->loc.path,
				priv->children[child_index]->name,
				strerror (op_errno));

			sh->success[child_index] = 0;
		}
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0)
		afr_sh_metadata_erase_pending (frame, this);

	return 0;
}


int
afr_sh_metadata_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                             int32_t op_ret, int32_t op_errno,
                             struct stat *preop, struct stat *postop)
{
	afr_sh_metadata_sync_cbk (frame, cookie, this, op_ret, op_errno);

	return 0;
}


int
afr_sh_metadata_xattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			   int32_t op_ret, int32_t op_errno)
{
	afr_sh_metadata_sync_cbk (frame, cookie, this, op_ret, op_errno);

	return 0;
}


int
afr_sh_metadata_sync (call_frame_t *frame, xlator_t *this, dict_t *xattr)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;
	int              source = 0;
	int              active_sinks = 0;
	int              call_count = 0;
	int              i = 0;

        struct stat      stbuf;
        int32_t          valid = 0;

	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	source = sh->source;
	active_sinks = sh->active_sinks;

	/*
	 * 2 calls per sink - setattr, setxattr
	 */
	if (xattr)
		call_count = active_sinks * 2;
	else
		call_count = active_sinks;

	local->call_count = call_count;

#ifdef HAVE_STRUCT_STAT_ST_ATIM_TV_NSEC
	stbuf.st_atim = sh->buf[source].st_atim;
	stbuf.st_mtim = sh->buf[source].st_mtim;
        
#elif HAVE_STRUCT_STAT_ST_ATIMESPEC_TV_NSEC
	stbuf.st_atimespec = sh->buf[source].st_atimespec;
	stbuf.st_mtimespec = sh->buf[source].st_mtimespec;
#else
	stbuf.st_atime = sh->buf[source].st_atime;
	stbuf.st_mtime = sh->buf[source].st_mtime;
#endif

        stbuf.st_uid = sh->buf[source].st_uid;
        stbuf.st_gid = sh->buf[source].st_gid;

        stbuf.st_mode = sh->buf[source].st_mode;
        
        valid = GF_SET_ATTR_MODE  | 
                GF_SET_ATTR_UID   | GF_SET_ATTR_GID |
                GF_SET_ATTR_ATIME | GF_SET_ATTR_MTIME;

	for (i = 0; i < priv->child_count; i++) {
		if (call_count == 0) {
			break;
		}
		if (sh->sources[i] || !local->child_up[i])
			continue;

		gf_log (this->name, GF_LOG_DEBUG,
			"self-healing metadata of %s from %s to %s",
			local->loc.path, priv->children[source]->name,
			priv->children[i]->name);

		STACK_WIND_COOKIE (frame, afr_sh_metadata_setattr_cbk,
				   (void *) (long) i,
				   priv->children[i],
				   priv->children[i]->fops->setattr,
				   &local->loc, &stbuf, valid);

		call_count--;

		if (!xattr)
			continue;

		STACK_WIND_COOKIE (frame, afr_sh_metadata_xattr_cbk,
				   (void *) (long) i,
				   priv->children[i],
				   priv->children[i]->fops->setxattr,
				   &local->loc, xattr, 0);
		call_count--;
	}

	return 0;
}


int
afr_sh_metadata_getxattr_cbk (call_frame_t *frame, void *cookie,
			      xlator_t *this,
			      int32_t op_ret, int32_t op_errno, dict_t *xattr)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;
	int              source = 0;

        int i;

	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	source = sh->source;

	if (op_ret == -1) {
		gf_log (this->name, GF_LOG_DEBUG,
			"getxattr of %s failed on subvolume %s (%s). proceeding without xattr",
			local->loc.path, priv->children[source]->name,
			strerror (op_errno));

		afr_sh_metadata_sync (frame, this, NULL);
	} else {
                for (i = 0; i < priv->child_count; i++) {
                        dict_del (xattr, priv->pending_key[i]);
                }

		afr_sh_metadata_sync (frame, this, xattr);
	}

	return 0;
}


int
afr_sh_metadata_sync_prepare (call_frame_t *frame, xlator_t *this)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;
	int              active_sinks = 0;
	int              source = 0;
	int              i = 0;

	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	source = sh->source;

	for (i = 0; i < priv->child_count; i++) {
		if (sh->sources[i] == 0 && local->child_up[i] == 1) {
			active_sinks++;
			sh->success[i] = 1;
		}
	}
	sh->success[source] = 1;

	if (active_sinks == 0) {
		gf_log (this->name, GF_LOG_DEBUG,
			"no active sinks for performing self-heal on file %s",
			local->loc.path);
		afr_sh_metadata_finish (frame, this);
		return 0;
	}
	sh->active_sinks = active_sinks;

	gf_log (this->name, GF_LOG_TRACE,
		"syncing metadata of %s from subvolume %s to %d active sinks",
		local->loc.path, priv->children[source]->name, active_sinks);

	STACK_WIND (frame, afr_sh_metadata_getxattr_cbk,
		    priv->children[source],
		    priv->children[source]->fops->getxattr,
		    &local->loc, NULL);

	return 0;
}


int
afr_sh_metadata_fix (call_frame_t *frame, xlator_t *this)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;
	int              nsources = 0;
	int              source = 0;
	int              i = 0;

	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	afr_sh_build_pending_matrix (priv, sh->pending_matrix, sh->xattr, 
				     priv->child_count, 
                                     AFR_METADATA_TRANSACTION);

	afr_sh_print_pending_matrix (sh->pending_matrix, this);

	nsources = afr_sh_mark_sources (sh, priv->child_count,
                                        AFR_SELF_HEAL_METADATA);

	afr_sh_supress_errenous_children (sh->sources, sh->child_errno,
					  priv->child_count);

        if (nsources == 0) {
                gf_log (this->name, GF_LOG_TRACE,
                        "No self-heal needed for %s",
                        local->loc.path);

                afr_sh_metadata_finish (frame, this);
                return 0;
        }

	if ((nsources == -1)
	    && (priv->favorite_child != -1)
	    && (sh->child_errno[priv->favorite_child] == 0)) {

		gf_log (this->name, GF_LOG_WARNING,
			"Picking favorite child %s as authentic source to resolve conflicting metadata of %s",
			priv->children[priv->favorite_child]->name,
			local->loc.path);

		sh->sources[priv->favorite_child] = 1;

		nsources = afr_sh_source_count (sh->sources,
						priv->child_count);
	}

	if (nsources == -1) {
		gf_log (this->name, GF_LOG_ERROR,
			"Unable to self-heal permissions/ownership of '%s' "
                        "(possible split-brain). Please fix the file on "
                        "all backend volumes", local->loc.path);

		local->govinda_gOvinda = 1;

		afr_sh_metadata_finish (frame, this);
		return 0;
	}

	source = afr_sh_select_source (sh->sources, priv->child_count);

        if (source == -1) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "No active sources found.");

                afr_sh_metadata_finish (frame, this);
                return 0;
        }

	sh->source = source;

	/* detect changes not visible through pending flags -- JIC */
	for (i = 0; i < priv->child_count; i++) {
		if (i == source || sh->child_errno[i])
			continue;

		if (PERMISSION_DIFFERS (&sh->buf[i], &sh->buf[source]))
			sh->sources[i] = 0;

		if (OWNERSHIP_DIFFERS (&sh->buf[i], &sh->buf[source]))
			sh->sources[i] = 0;
	}

	afr_sh_metadata_sync_prepare (frame, this);

	return 0;
}


int
afr_sh_metadata_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			    int32_t op_ret, int32_t op_errno,
                            inode_t *inode, struct stat *buf, dict_t *xattr,
                            struct stat *postparent)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;
	int              call_count = 0;
	int              child_index = 0;


	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	child_index = (long) cookie;

	LOCK (&frame->lock);
	{
		if (op_ret == 0) {
			gf_log (this->name, GF_LOG_TRACE,
				"path %s on subvolume %s is of mode 0%o",
				local->loc.path,
				priv->children[child_index]->name,
				buf->st_mode);

			sh->buf[child_index] = *buf;
			if (xattr)
				sh->xattr[child_index] = dict_ref (xattr);
		} else {
			gf_log (this->name, GF_LOG_DEBUG,
				"path %s on subvolume %s => -1 (%s)",
				local->loc.path,
				priv->children[child_index]->name,
				strerror (op_errno));

			sh->child_errno[child_index] = op_errno;
		}
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0)
		afr_sh_metadata_fix (frame, this);

	return 0;
}


int
afr_sh_metadata_lookup (call_frame_t *frame, xlator_t *this)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;
	int              i = 0;
	int              call_count = 0;
	dict_t          *xattr_req = NULL;
	int              ret = 0;

	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	call_count = afr_up_children_count (priv->child_count,
                                            local->child_up);
	local->call_count = call_count;
	
	xattr_req = dict_new();
	
	if (xattr_req) {
                for (i = 0; i < priv->child_count; i++) {
                        ret = dict_set_uint64 (xattr_req, 
                                               priv->pending_key[i],
                                               3 * sizeof(int32_t));
                }
        }

	for (i = 0; i < priv->child_count; i++) {
		if (local->child_up[i]) {
			gf_log (this->name, GF_LOG_TRACE,
				"looking up %s on %s",
				local->loc.path, priv->children[i]->name);

			STACK_WIND_COOKIE (frame, afr_sh_metadata_lookup_cbk,
					   (void *) (long) i,
					   priv->children[i],
					   priv->children[i]->fops->lookup,
					   &local->loc, xattr_req);
			if (!--call_count)
				break;
		}
	}
	
	if (xattr_req)
		dict_unref (xattr_req);

	return 0;
}


int
afr_sh_metadata_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			int32_t op_ret, int32_t op_errno)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;
	int              call_count = 0;
	int              child_index = (long) cookie;

	/* TODO: what if lock fails? */
	
	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	LOCK (&frame->lock);
	{
		if (op_ret == -1) {
			sh->op_failed = 1;

                        sh->locked_nodes[child_index] = 0;
			gf_log (this->name, GF_LOG_DEBUG,
				"locking of %s on child %d failed: %s",
				local->loc.path, child_index,
				strerror (op_errno));
		} else {
                        sh->locked_nodes[child_index] = 1;
			gf_log (this->name, GF_LOG_TRACE,
				"inode of %s on child %d locked",
				local->loc.path, child_index);
		}
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0) {
		if (sh->op_failed) {
			afr_sh_metadata_finish (frame, this);
			return 0;
		}

		afr_sh_metadata_lookup (frame, this);
	}

	return 0;
}


int
afr_sh_metadata_lock (call_frame_t *frame, xlator_t *this)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;
	int              i = 0;
	int              call_count = 0;
	struct flock     flock = {0, };


	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	call_count = afr_up_children_count (priv->child_count,
                                            local->child_up);
	local->call_count = call_count;

	for (i = 0; i < priv->child_count; i++) {
		flock.l_start   = 0;
		flock.l_len     = 0;
		flock.l_type    = F_WRLCK;

		if (local->child_up[i]) {
			gf_log (this->name, GF_LOG_TRACE,
				"locking %s on subvolume %s",
				local->loc.path, priv->children[i]->name);

			STACK_WIND_COOKIE (frame, afr_sh_metadata_lk_cbk,
					   (void *) (long) i,
					   priv->children[i],
					   priv->children[i]->fops->inodelk,
                                           this->name,
					   &local->loc, F_SETLK, &flock);

			if (!--call_count)
				break;
		}
	}

	return 0;
}


int
afr_self_heal_metadata (call_frame_t *frame, xlator_t *this)
{
	afr_local_t   *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t *priv = this->private;


	local = frame->local;
	sh = &local->self_heal;

	if (local->self_heal.need_metadata_self_heal && priv->metadata_self_heal) {
		afr_sh_metadata_lock (frame, this);
	} else {
		afr_sh_metadata_done (frame, this);
	}

	return 0;
}

