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


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "dht-common.c"

/* TODO: all 'TODO's in dht.c holds good */

int
nufa_local_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		       int op_ret, int op_errno,
                       inode_t *inode, struct stat *stbuf, dict_t *xattr,
                       struct stat *postparent)
{
        xlator_t     *subvol      = NULL;
        char          is_linkfile = 0;
        char          is_dir      = 0;
        dht_conf_t   *conf        = NULL;
        dht_local_t  *local       = NULL;
        loc_t        *loc         = NULL;
        int           i           = 0;
        call_frame_t *prev        = NULL;
	int           call_cnt    = 0;
        int           ret         = 0;


        conf  = this->private;

        prev  = cookie;
        local = frame->local;
        loc   = &local->loc;

	if (ENTRY_MISSING (op_ret, op_errno)) {
		if (conf->search_unhashed) {
			local->op_errno = ENOENT;
			dht_lookup_everywhere (frame, this, loc);
			return 0;
		}
	}

        if (op_ret == -1)
                goto out;

        is_linkfile = check_is_linkfile (inode, stbuf, xattr);
        is_dir      = check_is_dir (inode, stbuf, xattr);

        if (!is_dir && !is_linkfile) {
                /* non-directory and not a linkfile */

		dht_itransform (this, prev->this, stbuf->st_ino,
				&stbuf->st_ino);

		ret = dht_layout_preset (this, prev->this, inode);
		if (ret < 0) {
			gf_log (this->name, GF_LOG_DEBUG,
				"could not set pre-set layout for subvol %s",
				prev->this->name);
			op_ret   = -1;
			op_errno = EINVAL;
			goto err;
		}

                goto out;
        }

        if (is_dir) {
                call_cnt        = conf->subvolume_cnt;
		local->call_cnt = call_cnt;

                local->inode = inode_ref (inode);
                local->xattr = dict_ref (xattr);

		local->op_ret = 0;
		local->op_errno = 0;

		local->layout = dht_layout_new (this, conf->subvolume_cnt);
		if (!local->layout) {
			op_ret   = -1;
			op_errno = ENOMEM;
			gf_log (this->name, GF_LOG_DEBUG,
				"memory allocation failed :(");
			goto err;
		}

                for (i = 0; i < call_cnt; i++) {
                        STACK_WIND (frame, dht_lookup_dir_cbk,
                                    conf->subvolumes[i],
                                    conf->subvolumes[i]->fops->lookup,
                                    &local->loc, local->xattr_req);
                }
        }

        if (is_linkfile) {
                subvol = dht_linkfile_subvol (this, inode, stbuf, xattr);

                if (!subvol) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "linkfile not having link subvolume. path=%s",
                                loc->path);
			dht_lookup_everywhere (frame, this, loc);
			return 0;
                }

		STACK_WIND (frame, dht_lookup_linkfile_cbk,
			    subvol, subvol->fops->lookup,
			    &local->loc, local->xattr_req);
        }

        return 0;

out:
	if (!local->hashed_subvol) {
		gf_log (this->name, GF_LOG_DEBUG,
			"no subvolume in layout for path=%s",
			local->loc.path);
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame, dht_lookup_cbk,
		    local->hashed_subvol, local->hashed_subvol->fops->lookup,
		    &local->loc, local->xattr_req);

	return 0;

 err:
        DHT_STACK_UNWIND (lookup, frame, op_ret, op_errno,
                          inode, stbuf, xattr, NULL);
        return 0;
}

int
nufa_lookup (call_frame_t *frame, xlator_t *this,
	     loc_t *loc, dict_t *xattr_req)
{
        xlator_t     *hashed_subvol = NULL;
        xlator_t     *cached_subvol = NULL;
        xlator_t     *subvol = NULL;
        dht_local_t  *local  = NULL;
	dht_conf_t   *conf = NULL;
        int           ret    = -1;
        int           op_errno = -1;
	dht_layout_t *layout = NULL;
	int           i = 0;
	int           call_cnt = 0;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	conf = this->private;

        local = dht_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"Out of memory");
		goto err;
	}

        ret = loc_dup (loc, &local->loc);
        if (ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_DEBUG,
                        "copying location failed for path=%s",
                        loc->path);
                goto err;
        }

	if (xattr_req) {
		local->xattr_req = dict_ref (xattr_req);
	} else {
		local->xattr_req = dict_new ();
	}

	hashed_subvol = dht_subvol_get_hashed (this, &local->loc);
	cached_subvol = dht_subvol_get_cached (this, local->loc.inode);

	local->cached_subvol = cached_subvol;
	local->hashed_subvol = hashed_subvol;

        if (is_revalidate (loc)) {
		local->layout = layout = dht_layout_get (this, loc->inode);

                if (!layout) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "revalidate without cache. path=%s",
                                loc->path);
                        op_errno = EINVAL;
                        goto err;
                }

		if (layout->gen && (layout->gen < conf->gen)) {
			gf_log (this->name, GF_LOG_DEBUG,
				"incomplete layout failure for path=%s",
				loc->path);
                        dht_layout_unref (this, local->layout);
			goto do_fresh_lookup;
		}

		local->inode    = inode_ref (loc->inode);
		local->st_ino   = loc->inode->ino;

		local->call_cnt = layout->cnt;
		call_cnt = local->call_cnt;

		/* NOTE: we don't require 'trusted.glusterfs.dht.linkto' attribute,
		 *       revalidates directly go to the cached-subvolume.
		 */
		ret = dict_set_uint32 (local->xattr_req,
				       "trusted.glusterfs.dht", 4 * 4);

		for (i = 0; i < layout->cnt; i++) {
			subvol = layout->list[i].xlator;

			STACK_WIND (frame, dht_revalidate_cbk,
				    subvol, subvol->fops->lookup,
				    loc, local->xattr_req);

			if (!--call_cnt)
				break;
		}
	} else {
        do_fresh_lookup:
		ret = dict_set_uint32 (local->xattr_req,
				       "trusted.glusterfs.dht", 4 * 4);

		ret = dict_set_uint32 (local->xattr_req,
				       "trusted.glusterfs.dht.linkto", 256);

		/* Send it to only local volume */
		STACK_WIND (frame, nufa_local_lookup_cbk,
			    (xlator_t *)conf->private,
			    ((xlator_t *)conf->private)->fops->lookup,
			    loc, local->xattr_req);
	}

        return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (lookup, frame, -1, op_errno, NULL, NULL, NULL, NULL);
	return 0;
}

int
nufa_create_linkfile_create_cbk (call_frame_t *frame, void *cookie,
				 xlator_t *this, int op_ret, int op_errno,
                                 inode_t *inode, struct stat *stbuf,
                                 struct stat *preparent,
                                 struct stat *postparent)
{
	dht_local_t  *local = NULL;
	call_frame_t *prev = NULL;
	dht_conf_t   *conf  = NULL;

	local = frame->local;
	prev  = cookie;
	conf  = this->private;

	if (op_ret == -1)
		goto err;

	STACK_WIND (frame, dht_create_cbk,
		    local->cached_subvol, local->cached_subvol->fops->create,
		    &local->loc, local->flags, local->mode, local->fd);

	return 0;

 err:
	DHT_STACK_UNWIND (create, frame, -1, op_errno,
                          NULL, NULL, NULL, NULL, NULL);
	return 0;
}

int
nufa_create (call_frame_t *frame, xlator_t *this,
	     loc_t *loc, int32_t flags, mode_t mode, fd_t *fd)
{
	dht_local_t *local = NULL;
	dht_conf_t  *conf  = NULL;
	xlator_t    *subvol = NULL;
        xlator_t    *avail_subvol = NULL;
	int          op_errno = -1;
	int          ret = -1;

	VALIDATE_OR_GOTO (frame, err);
	VALIDATE_OR_GOTO (this, err);
	VALIDATE_OR_GOTO (loc, err);

	conf  = this->private;

        dht_get_du_info (frame, this, loc);

        local = dht_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"Out of memory");
		goto err;
	}

	subvol = dht_subvol_get_hashed (this, loc);
	if (!subvol) {
		gf_log (this->name, GF_LOG_DEBUG,
			"no subvolume in layout for path=%s",
			loc->path);
		op_errno = ENOENT;
		goto err;
	}

        avail_subvol = conf->private;
        if (dht_is_subvol_filled (this, (xlator_t *)conf->private)) {
                avail_subvol =
                        dht_free_disk_available_subvol (this,
                                                        (xlator_t *)conf->private);
        }

        if (subvol != avail_subvol) {
                /* create a link file instead of actual file */
                ret = loc_copy (&local->loc, loc);
                if (ret == -1) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Out of memory");
                                op_errno = ENOMEM;
                                goto err;
                }

                local->fd = fd_ref (fd);
                local->mode = mode;
                local->flags = flags;

                local->cached_subvol = avail_subvol;
                dht_linkfile_create (frame,
                                     nufa_create_linkfile_create_cbk,
                                     avail_subvol, subvol, loc);
                return 0;
        }

        gf_log (this->name, GF_LOG_TRACE,
                "creating %s on %s", loc->path, subvol->name);

        STACK_WIND (frame, dht_create_cbk,
                    subvol, subvol->fops->create,
                    loc, flags, mode, fd);

        return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (create, frame, -1, op_errno,
                          NULL, NULL, NULL, NULL, NULL);

	return 0;
}

int
nufa_mknod_linkfile_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int op_ret, int op_errno, inode_t *inode,
                         struct stat *stbuf, struct stat *preparent,
                         struct stat *postparent)
{
	dht_local_t  *local = NULL;
	call_frame_t *prev = NULL;
	dht_conf_t   *conf  = NULL;

	local = frame->local;
	prev  = cookie;
	conf  = this->private;

	if (op_ret >= 0) {
		STACK_WIND (frame, dht_newfile_cbk,
			    local->cached_subvol,
			    local->cached_subvol->fops->mknod,
			    &local->loc, local->mode, local->rdev);

		return 0;
	}

	DHT_STACK_UNWIND (link, frame, op_ret, op_errno,
                          inode, stbuf, preparent, postparent);
	return 0;
}


int
nufa_mknod (call_frame_t *frame, xlator_t *this,
	    loc_t *loc, mode_t mode, dev_t rdev)
{
	dht_local_t *local = NULL;
	dht_conf_t  *conf  = NULL;
	xlator_t    *subvol = NULL;
        xlator_t    *avail_subvol = NULL;
	int          op_errno = -1;
	int          ret = -1;

	VALIDATE_OR_GOTO (frame, err);
	VALIDATE_OR_GOTO (this, err);
	VALIDATE_OR_GOTO (loc, err);

	conf  = this->private;

        dht_get_du_info (frame, this, loc);

        local = dht_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"Out of memory");
		goto err;
	}

	subvol = dht_subvol_get_hashed (this, loc);
	if (!subvol) {
		gf_log (this->name, GF_LOG_DEBUG,
			"no subvolume in layout for path=%s",
			loc->path);
		op_errno = ENOENT;
		goto err;
	}

        /* Consider the disksize in consideration */
        avail_subvol = conf->private;
        if (dht_is_subvol_filled (this, (xlator_t *)conf->private)) {
                avail_subvol =
                        dht_free_disk_available_subvol (this,
                                                        (xlator_t *)conf->private);
        }

	if (avail_subvol != subvol) {
		/* Create linkfile first */
		ret = loc_copy (&local->loc, loc);
		if (ret == -1) {
			gf_log (this->name, GF_LOG_ERROR,
				"Out of memory");
			op_errno = ENOMEM;
			goto err;
		}

		local->mode = mode;
		local->rdev = rdev;
		local->cached_subvol = avail_subvol;

		dht_linkfile_create (frame, nufa_mknod_linkfile_cbk,
                                     avail_subvol, subvol, loc);
		return 0;
	}

	gf_log (this->name, GF_LOG_TRACE,
		"creating %s on %s", loc->path, subvol->name);

	STACK_WIND (frame, dht_newfile_cbk,
		    subvol, subvol->fops->mknod,
		    loc, mode, rdev);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (mknod, frame, -1, op_errno,
                          NULL, NULL, NULL, NULL);

	return 0;
}


int
notify (xlator_t *this, int event, void *data, ...)
{
	int ret = -1;

	ret = dht_notify (this, event, data);

	return ret;
}

void
fini (xlator_t *this)
{
        int         i = 0;
        dht_conf_t *conf = NULL;

	conf = this->private;

        if (conf) {
                if (conf->file_layouts) {
                        for (i = 0; i < conf->subvolume_cnt; i++) {
                                FREE (conf->file_layouts[i]);
                        }
                        FREE (conf->file_layouts);
                }

                if (conf->default_dir_layout)
                        FREE (conf->default_dir_layout);

                if (conf->subvolumes)
                        FREE (conf->subvolumes);

		if (conf->subvolume_status)
			FREE (conf->subvolume_status);

                FREE (conf);
        }

	return;
}

int
init (xlator_t *this)
{
        dht_conf_t    *conf = NULL;
	xlator_list_t *trav = NULL;
	data_t        *data = NULL;
	char          *local_volname = NULL;
	char          *temp_str = NULL;
        int            ret = -1;
        int            i = 0;
	char           my_hostname[256];
	uint32_t       temp_free_disk = 0;

	if (!this->children) {
		gf_log (this->name, GF_LOG_CRITICAL,
			"NUFA needs more than one subvolume");
		return -1;
	}

	if (!this->parents) {
		gf_log (this->name, GF_LOG_WARNING,
			"dangling volume. check volfile");
	}

        conf = CALLOC (1, sizeof (*conf));
        if (!conf) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory");
                goto err;
        }

	conf->search_unhashed = 0;

	if (dict_get_str (this->options, "lookup-unhashed",
			  &temp_str) == 0) {
		gf_string2boolean (temp_str,
				   &conf->search_unhashed);
	}

        ret = dht_init_subvolumes (this, conf);
        if (ret == -1) {
                goto err;
        }

        ret = dht_layouts_init (this, conf);
        if (ret == -1) {
                goto err;
        }

	LOCK_INIT (&conf->subvolume_lock);
	LOCK_INIT (&conf->layout_lock);

	conf->gen = 1;

	local_volname = "localhost";
	ret = gethostname (my_hostname, 256);
	if (ret < 0) {
		gf_log (this->name, GF_LOG_WARNING,
			"could not find hostname (%s)",
			strerror (errno));
	}

	if (ret == 0)
		local_volname = my_hostname;

	data = dict_get (this->options, "local-volume-name");
	if (data) {
		local_volname = data->data;
	}

	trav = this->children;
	while (trav) {
		if (strcmp (trav->xlator->name, local_volname) == 0)
			break;
		trav = trav->next;
	}

	if (!trav) {
		gf_log (this->name, GF_LOG_ERROR,
			"Could not find subvolume named '%s'. "
			"Please define volume with the name as the hostname "
			"or override it with 'option local-volume-name'",
			local_volname);
		goto err;
	}
	/* The volume specified exists */
	conf->private = trav->xlator;

        conf->min_free_disk = 10;
        conf->disk_unit = 'p';

        if (dict_get_str (this->options, "min-free-disk",
                          &temp_str) == 0) {
                if (gf_string2percent (temp_str,
                                       &temp_free_disk) == 0) {
                        if (temp_free_disk > 100) {
                                gf_string2bytesize (temp_str,
                                                        &conf->min_free_disk);
                                conf->disk_unit = 'b';
                        } else {
                                conf->min_free_disk = (uint64_t)temp_free_disk;
                                conf->disk_unit = 'p';
                        }
                } else {
                        gf_string2bytesize (temp_str,
                                                &conf->min_free_disk);
                        conf->disk_unit = 'b';
                }
        }

        conf->du_stats = CALLOC (conf->subvolume_cnt, sizeof (dht_du_t));
        if (!conf->du_stats) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory");
                goto err;
        }

        this->private = conf;

        return 0;

err:
        if (conf) {
                if (conf->file_layouts) {
                        for (i = 0; i < conf->subvolume_cnt; i++) {
                                FREE (conf->file_layouts[i]);
                        }
                        FREE (conf->file_layouts);
                }

                if (conf->default_dir_layout)
                        FREE (conf->default_dir_layout);

                if (conf->subvolumes)
                        FREE (conf->subvolumes);

		if (conf->subvolume_status)
			FREE (conf->subvolume_status);

                if (conf->du_stats)
                        FREE (conf->du_stats);

                FREE (conf);
        }

        return -1;
}


struct xlator_fops fops = {
	.lookup      = nufa_lookup,
	.create      = nufa_create,
	.mknod       = nufa_mknod,

	.stat        = dht_stat,
	.fstat       = dht_fstat,
	.truncate    = dht_truncate,
	.ftruncate   = dht_ftruncate,
	.access      = dht_access,
	.readlink    = dht_readlink,
	.setxattr    = dht_setxattr,
	.getxattr    = dht_getxattr,
	.removexattr = dht_removexattr,
	.open        = dht_open,
	.readv       = dht_readv,
	.writev      = dht_writev,
	.flush       = dht_flush,
	.fsync       = dht_fsync,
	.statfs      = dht_statfs,
	.lk          = dht_lk,
	.opendir     = dht_opendir,
	.readdir     = dht_readdir,
	.readdirp    = dht_readdirp,
	.fsyncdir    = dht_fsyncdir,
	.symlink     = dht_symlink,
	.unlink      = dht_unlink,
	.link        = dht_link,
	.mkdir       = dht_mkdir,
	.rmdir       = dht_rmdir,
	.rename      = dht_rename,
	.inodelk     = dht_inodelk,
	.finodelk    = dht_finodelk,
	.entrylk     = dht_entrylk,
	.fentrylk    = dht_fentrylk,
	.xattrop     = dht_xattrop,
	.fxattrop    = dht_fxattrop,
        .setattr     = dht_setattr,
#if 0
	.setdents    = dht_setdents,
	.getdents    = dht_getdents,
	.checksum    = dht_checksum,
#endif
};


struct xlator_mops mops = {
};


struct xlator_cbks cbks = {
	.forget     = dht_forget
};


struct volume_options options[] = {
	{ .key  = {"local-volume-name"},
	  .type = GF_OPTION_TYPE_XLATOR
	},
        { .key  = {"lookup-unhashed"},
	  .type = GF_OPTION_TYPE_BOOL
	},
        { .key  = {"min-free-disk"},
          .type = GF_OPTION_TYPE_PERCENT_OR_SIZET,
        },
	{ .key  = {NULL} },
};
