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

#ifndef __AFR_SELF_HEAL_ALGORITHM_H__
#define __AFR_SELF_HEAL_ALGORITHM_H__


typedef int (*afr_sh_algo_fn) (call_frame_t *frame,
                               xlator_t *this);

struct afr_sh_algorithm {
        const char *name;
        afr_sh_algo_fn fn;
};

extern struct afr_sh_algorithm afr_self_heal_algorithms[3];

typedef struct {
        gf_lock_t lock;
        unsigned int loops_running;
        off_t offset;
} afr_sh_algo_full_private_t;

struct sh_diff_loop_state {
        off_t   offset;
        unsigned char *write_needed;
        uint8_t *checksum;
        gf_boolean_t active;
};

typedef struct {
        size_t block_size;

        gf_lock_t lock;
        unsigned int loops_running;
        off_t offset;

        int32_t total_blocks;
        int32_t diff_blocks;

        struct sh_diff_loop_state **loops;
} afr_sh_algo_diff_private_t;

#endif /* __AFR_SELF_HEAL_ALGORITHM_H__ */
