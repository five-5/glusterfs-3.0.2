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

#ifndef _SOCKET_NAME_H
#define _SOCKET_NAME_H

#include "compat.h"

int32_t 
client_bind (transport_t *this, 
             struct sockaddr *sockaddr, 
             socklen_t *sockaddr_len, 
             int sock);

int32_t
client_fill_address_family (transport_t *this, sa_family_t *sa_family);

int32_t
server_fill_address_family (transport_t *this, sa_family_t *sa_family);

int32_t
socket_client_get_remote_sockaddr (transport_t *this, 
                                   struct sockaddr *sockaddr, 
                                   socklen_t *sockaddr_len);

int32_t
socket_server_get_local_sockaddr (transport_t *this, 
                                  struct sockaddr *addr, 
                                  socklen_t *addr_len);

int32_t
get_transport_identifiers (transport_t *this);

#endif /* _SOCKET_NAME_H */
