/*
  Copyright (c) 2009 Gluster, Inc. <http://www.gluster.com>
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

#include "server-protocol.h"
#include "server-helpers.h"


int
server_resolve_all (call_frame_t *frame);
int
resolve_entry_simple (call_frame_t *frame);
int
resolve_inode_simple (call_frame_t *frame);
int
resolve_path_simple (call_frame_t *frame);

int
component_count (const char *path)
{
        int         count = 0;
        const char *trav = NULL;

        trav = path;

        for (trav = path; *trav; trav++) {
                if (*trav == '/')
                        count++;
        }

        return count + 2;
}


int
prepare_components (call_frame_t *frame)
{
        server_state_t       *state = NULL;
        xlator_t             *this = NULL;
        server_resolve_t     *resolve = NULL;
        char                 *resolved = NULL;
        int                   count = 0;
        struct resolve_comp  *components = NULL;
        int                   i = 0;
        char                 *trav = NULL;


        state = CALL_STATE (frame);
        this  = frame->this;
        resolve = state->resolve_now;

        resolved = strdup (resolve->path);
        resolve->resolved = resolved;

        count = component_count (resolve->path);
        components = CALLOC (sizeof (*components), count);
        resolve->components = components;

        components[0].basename = "";
        components[0].ino      = 1;
        components[0].gen      = 0;
        components[0].inode    = state->itable->root;

        i = 1;
        for (trav = resolved; *trav; trav++) {
                if (*trav == '/') {
                        components[i].basename = trav + 1;
                        *trav = 0;
                        i++;
                }
        }

        return 0;
}


int
resolve_loc_touchup (call_frame_t *frame)
{
        server_state_t       *state = NULL;
        server_resolve_t     *resolve = NULL;
        loc_t                *loc = NULL;
        char                 *path = NULL;
        int                   ret = 0;

        state = CALL_STATE (frame);

        resolve = state->resolve_now;
        loc     = state->loc_now;

        if (!loc->path) {
                if (loc->parent) {
                        ret = inode_path (loc->parent, resolve->bname, &path);
                } else if (loc->inode) {
                        ret = inode_path (loc->inode, NULL, &path);
                }

                if (!path)
                        path = strdup (resolve->path);

                loc->path = path;
        }

        loc->name = strrchr (loc->path, '/');
        if (loc->name)
                loc->name++;

        if (!loc->parent && loc->inode) {
                loc->parent = inode_parent (loc->inode, 0, NULL);
        }

        return 0;
}


int
resolve_deep_continue (call_frame_t *frame)
{
        server_state_t       *state = NULL;
        xlator_t             *this = NULL;
        server_resolve_t     *resolve = NULL;
        int                   ret = 0;

        state = CALL_STATE (frame);
        this  = frame->this;
        resolve = state->resolve_now;

        resolve->op_ret   = 0;
        resolve->op_errno = 0;

        if (resolve->par)
                ret = resolve_entry_simple (frame);
        else if (resolve->ino)
                ret = resolve_inode_simple (frame);
        else if (resolve->path)
                ret = resolve_path_simple (frame);

        resolve_loc_touchup (frame);

        server_resolve_all (frame);

        return 0;
}


int
resolve_deep_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int op_ret, int op_errno, inode_t *inode, struct stat *buf,
                  dict_t *xattr, struct stat *postparent)
{
        server_state_t       *state = NULL;
        server_resolve_t     *resolve = NULL;
        struct resolve_comp  *components = NULL;
        int                   i = 0;
        inode_t              *link_inode = NULL;

        state = CALL_STATE (frame);
        resolve = state->resolve_now;
        components = resolve->components;

        i = (long) cookie;

        if (op_ret == -1) {
                goto get_out_of_here;
        }

        if (i != 0) {
                /* no linking for root inode */
                link_inode = inode_link (inode, resolve->deep_loc.parent,
                                         resolve->deep_loc.name, buf);
                inode_lookup (link_inode);
                components[i].inode  = link_inode;
                link_inode = NULL;
        }

        loc_wipe (&resolve->deep_loc);

        i++; /* next component */

        if (!components[i].basename) {
                /* all components of the path are resolved */
                goto get_out_of_here;
        }

        /* join the current component with the path resolved until now */
        *(components[i].basename - 1) = '/';

        resolve->deep_loc.path   = strdup (resolve->resolved);
        resolve->deep_loc.parent = inode_ref (components[i-1].inode);
        resolve->deep_loc.inode  = inode_new (state->itable);
        resolve->deep_loc.name   = components[i].basename;

        STACK_WIND_COOKIE (frame, resolve_deep_cbk, (void *) (long) i,
                           BOUND_XL (frame), BOUND_XL (frame)->fops->lookup,
                           &resolve->deep_loc, NULL);
        return 0;

get_out_of_here:
        resolve_deep_continue (frame);
        return 0;
}


int
resolve_path_deep (call_frame_t *frame)
{
        server_state_t     *state = NULL;
        xlator_t           *this = NULL;
        server_resolve_t   *resolve = NULL;
        int                 i = 0;

        state = CALL_STATE (frame);
        this  = frame->this;
        resolve = state->resolve_now;

        gf_log (BOUND_XL (frame)->name, GF_LOG_DEBUG,
                "RESOLVE %s() seeking deep resolution of %s",
                gf_fop_list[frame->root->op], resolve->path);

        prepare_components (frame);

        /* start from the root */
        resolve->deep_loc.inode = state->itable->root;
        resolve->deep_loc.path  = strdup ("/");
        resolve->deep_loc.name  = "";

        STACK_WIND_COOKIE (frame, resolve_deep_cbk, (void *) (long) i,
                           BOUND_XL (frame), BOUND_XL (frame)->fops->lookup,
                           &resolve->deep_loc, NULL);
        return 0;
}


int
resolve_path_simple (call_frame_t *frame)
{
        server_state_t       *state = NULL;
        xlator_t             *this = NULL;
        server_resolve_t     *resolve = NULL;
        struct resolve_comp  *components = NULL;
        int                   ret = -1;
        int                   par_idx = 0;
        int                   ino_idx = 0;
        int                   i = 0;

        state = CALL_STATE (frame);
        this  = frame->this;
        resolve = state->resolve_now;
        components = resolve->components;

        if (!components) {
                resolve->op_ret   = -1;
                resolve->op_errno = ENOENT;
                goto out;
        }

        for (i = 0; components[i].basename; i++) {
                par_idx = ino_idx;
                ino_idx = i;
        }

        if (!components[par_idx].inode) {
                resolve->op_ret    = -1;
                resolve->op_errno  = ENOENT;
                goto out;
        }

        if (!components[ino_idx].inode &&
            (resolve->type == RESOLVE_MUST || resolve->type == RESOLVE_EXACT)) {
                resolve->op_ret    = -1;
                resolve->op_errno  = ENOENT;
                goto out;
        }

        if (components[ino_idx].inode && resolve->type == RESOLVE_NOT) {
                resolve->op_ret    = -1;
                resolve->op_errno  = EEXIST;
                goto out;
        }

        if (components[ino_idx].inode)
                state->loc_now->inode  = inode_ref (components[ino_idx].inode);
        state->loc_now->parent = inode_ref (components[par_idx].inode);

        ret = 0;

out:
        return ret;
}

/*
  Check if the requirements are fulfilled by entries in the inode cache itself
  Return value:
  <= 0 - simple resolution was decisive and complete (either success or failure)
  > 0  - indecisive, need to perform deep resolution
*/

int
resolve_entry_simple (call_frame_t *frame)
{
        server_state_t     *state = NULL;
        xlator_t           *this = NULL;
        server_resolve_t   *resolve = NULL;
        inode_t            *parent = NULL;
        inode_t            *inode = NULL;
        int                 ret = 0;

        state = CALL_STATE (frame);
        this  = frame->this;
        resolve = state->resolve_now;

        parent = inode_get (state->itable, resolve->par, 0);
        if (!parent) {
                /* simple resolution is indecisive. need to perform
                   deep resolution */
                resolve->op_ret   = -1;
                resolve->op_errno = ENOENT;
                ret = 1;

                inode = inode_grep (state->itable, parent, resolve->bname);
                if (inode != NULL) {
                        gf_log (this->name, GF_LOG_DEBUG, "%"PRId64": inode "
                                "(pointer:%p ino: %"PRIu64") present but parent"
                                " is NULL for path (%s)", frame->root->unique,
                                inode, inode->ino, resolve->path);
                        inode_unref (inode);
                }
                goto out;
        }

        if (parent->ino != 1 && parent->generation != resolve->gen) {
                /* simple resolution is decisive - request was for a
                   stale handle */
                resolve->op_ret   = -1;
                resolve->op_errno = ENOENT;
                ret = -1;
                goto out;
        }

        /* expected @parent was found from the inode cache */
        state->loc_now->parent = inode_ref (parent);

        inode = inode_grep (state->itable, parent, resolve->bname);
        if (!inode) {
                switch (resolve->type) {
                case RESOLVE_DONTCARE:
                case RESOLVE_NOT:
                        ret = 0;
                        break;
                case RESOLVE_MAY:
                        ret = 1;
                        break;
                default:
                        resolve->op_ret   = -1;
                        resolve->op_errno = ENOENT;
                        ret = 1;
                        break;
                }

                goto out;
        }

        if (resolve->type == RESOLVE_NOT) {
                gf_log (this->name, GF_LOG_DEBUG, "inode (pointer: %p ino:%"
                        PRIu64") found for path (%s) while type is RESOLVE_NOT",
                        inode, inode->ino, resolve->path);
                resolve->op_ret   = -1;
                resolve->op_errno = EEXIST;
                ret = -1;
                goto out;
        }

        ret = 0;

        state->loc_now->inode  = inode_ref (inode);

out:
        if (parent)
                inode_unref (parent);

        if (inode)
                inode_unref (inode);

        return ret;
}


int
server_resolve_entry (call_frame_t *frame)
{
        server_state_t     *state = NULL;
        xlator_t           *this = NULL;
        server_resolve_t   *resolve = NULL;
        int                 ret = 0;
        loc_t              *loc = NULL;

        state = CALL_STATE (frame);
        this  = frame->this;
        resolve = state->resolve_now;
        loc  = state->loc_now;

        ret = resolve_entry_simple (frame);

        if (ret > 0) {
                loc_wipe (loc);
                resolve_path_deep (frame);
                return 0;
        }

        if (ret == 0)
                resolve_loc_touchup (frame);

        server_resolve_all (frame);

        return 0;
}


int
resolve_inode_simple (call_frame_t *frame)
{
        server_state_t     *state = NULL;
        xlator_t           *this = NULL;
        server_resolve_t   *resolve = NULL;
        inode_t            *inode = NULL;
        int                 ret = 0;

        state = CALL_STATE (frame);
        this  = frame->this;
        resolve = state->resolve_now;

        if (resolve->type == RESOLVE_EXACT) {
                inode = inode_get (state->itable, resolve->ino, resolve->gen);
        } else {
                inode = inode_get (state->itable, resolve->ino, 0);
        }

        if (!inode) {
                resolve->op_ret   = -1;
                resolve->op_errno = ENOENT;
                ret = 1;
                goto out;
        }

        if (inode->ino != 1 && inode->generation != resolve->gen) {
                resolve->op_ret      = -1;
                resolve->op_errno    = ENOENT;
                ret = -1;
                goto out;
        }

        ret = 0;

        state->loc_now->inode = inode_ref (inode);

out:
        if (inode)
                inode_unref (inode);

        return ret;
}


int
server_resolve_inode (call_frame_t *frame)
{
        server_state_t     *state = NULL;
        xlator_t           *this = NULL;
        server_resolve_t   *resolve = NULL;
        int                 ret = 0;
        loc_t              *loc = NULL;

        state = CALL_STATE (frame);
        this  = frame->this;
        resolve = state->resolve_now;
        loc  = state->loc_now;

        ret = resolve_inode_simple (frame);

        if (ret > 0) {
                loc_wipe (loc);
                resolve_path_deep (frame);
                return 0;
        }

        if (ret == 0)
                resolve_loc_touchup (frame);

        server_resolve_all (frame);

        return 0;
}


int
server_resolve_fd (call_frame_t *frame)
{
        server_state_t       *state = NULL;
        xlator_t             *this = NULL;
        server_resolve_t     *resolve = NULL;
        server_connection_t  *conn = NULL;
        uint64_t              fd_no = -1;

        state = CALL_STATE (frame);
        this  = frame->this;
        resolve = state->resolve_now;
        conn  = SERVER_CONNECTION (frame);

        fd_no = resolve->fd_no;

        state->fd = gf_fd_fdptr_get (conn->fdtable, fd_no);

        if (!state->fd) {
                resolve->op_ret   = -1;
                resolve->op_errno = EBADF;
        }

        server_resolve_all (frame);

        return 0;
}


int
server_resolve (call_frame_t *frame)
{
        server_state_t     *state = NULL;
        xlator_t           *this = NULL;
        server_resolve_t   *resolve = NULL;

        state = CALL_STATE (frame);
        this  = frame->this;
        resolve = state->resolve_now;

        if (resolve->fd_no != -1) {

                server_resolve_fd (frame);

        } else if (resolve->par) {

                server_resolve_entry (frame);

        } else if (resolve->ino) {

                server_resolve_inode (frame);

        } else if (resolve->path) {

                resolve_path_deep (frame);

        } else  {

                resolve->op_ret = -1;
                resolve->op_errno = EINVAL;

                server_resolve_all (frame);
        }

        return 0;
}


int
server_resolve_done (call_frame_t *frame)
{
        server_state_t    *state = NULL;
        xlator_t          *bound_xl = NULL;

        state = CALL_STATE (frame);
        bound_xl = BOUND_XL (frame);

        server_print_request (frame);

        state->resume_fn (frame, bound_xl);

        return 0;
}


/*
 * This function is called multiple times, once per resolving one location/fd.
 * state->resolve_now is used to decide which location/fd is to be resolved now
 */
int
server_resolve_all (call_frame_t *frame)
{
        server_state_t    *state = NULL;
        xlator_t          *this = NULL;

        this  = frame->this;
        state = CALL_STATE (frame);

        if (state->resolve_now == NULL) {

                state->resolve_now = &state->resolve;
                state->loc_now     = &state->loc;

                server_resolve (frame);

        } else if (state->resolve_now == &state->resolve) {

                state->resolve_now = &state->resolve2;
                state->loc_now     = &state->loc2;

                server_resolve (frame);

        } else if (state->resolve_now == &state->resolve2) {

                server_resolve_done (frame);

        } else {
                gf_log (this->name, GF_LOG_ERROR,
                        "Invalid pointer for state->resolve_now");
        }

        return 0;
}


int
resolve_and_resume (call_frame_t *frame, server_resume_fn_t fn)
{
        server_state_t    *state = NULL;
        xlator_t          *this  = NULL;

        state = CALL_STATE (frame);
        state->resume_fn = fn;

        this = frame->this;

        server_resolve_all (frame);

        return 0;
}
