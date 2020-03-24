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

#ifndef __TRANSACTION_H__
#define __TRANSACTION_H__

void
afr_transaction_fop_failed (call_frame_t *frame, xlator_t *this,
			    int child_index);

int
afr_lock_server_count (afr_private_t *priv, afr_transaction_type type);

int32_t
afr_transaction (call_frame_t *frame, xlator_t *this, afr_transaction_type type);

#endif /* __TRANSACTION_H__ */
