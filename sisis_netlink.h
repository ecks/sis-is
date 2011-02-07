/*
 * SIS-IS Test program.
 * Stephen Sigwart
 * University of Delaware
 */

#ifndef _SISIS_NETLINK_H
#define _SISIS_NETLINK_H

/* Kernel routing table updates using netlink over GNU/Linux system.
 * Copyright (C) 1997, 98, 99 Kunihiro Ishiguro
 * Modified for SIS-IS: Stephen Sigwart; 2011; University of Delaware
 *
 * This file is part of GNU Zebra.
 *
 * GNU Zebra is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * GNU Zebra is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Zebra; see the file COPYING.  If not, write to the Free
 * Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.  
 */

#include "sisis_structs.h"
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

/* Hack for GNU libc version 2. */
#ifndef MSG_TRUNC
#define MSG_TRUNC      0x20
#endif /* MSG_TRUNC */

/* Socket interface to kernel */
struct nlsock
{
  int sock;
  int seq;
  struct sockaddr_nl snl;
  const char *name;
};

/* Structure to pass in to sisis_netlink_routing_table with callback functions. */
struct sisis_netlink_routing_table_info
{
	int (*rib_add_ipv4_route)(struct route_ipv4 *);
	int (*rib_remove_ipv4_route)(struct route_ipv4 *);
	#ifdef HAVE_IPV6
	int (*rib_add_ipv6_route)(struct route_ipv6 *);
	int (*rib_remove_ipv6_route)(struct route_ipv6 *);
	#endif /* HAVE_IPV6 */
};

/* Make socket for Linux netlink interface. */
static int sisis_netlink_socket (struct nlsock *nl, unsigned long groups);

/* Exported interface function.  This function simply calls
   sisis_netlink_socket (). */
void sisis_kernel_init (void);

/* Get type specified information from netlink. */
static int sisis_netlink_request (int family, int type, struct nlsock *nl);

/* Receive message from netlink interface and pass those information to the given function. */
static int sisis_netlink_parse_info (int (*filter) (struct sockaddr_nl *, struct nlmsghdr *), struct nlsock *nl, void * info);

/* Utility function for parse rtattr. */
static void sisis_netlink_parse_rtattr (struct rtattr **tb, int max, struct rtattr *rta, int len);

/* Looking up routing table by netlink interface. */
static int sisis_netlink_routing_table (struct sockaddr_nl *snl, struct nlmsghdr *h, void * info);

/* Routing table read function using netlink interface. */
int sisis_netlink_route_read (struct sisis_netlink_routing_table_info * info);

#endif