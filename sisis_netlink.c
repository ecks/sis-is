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

#include "sisis_api.h"
#include "sisis_netlink.h"
#include "sisis_structs.h"
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <pthread.h>

/* Socket interface to kernel */
struct nlsock sisis_netlink_cmd  = { -1, 0, {0}, "netlink-cmd"};        /* command channel */

/* Make socket for Linux netlink interface. */
static int
sisis_netlink_socket (struct nlsock *nl, unsigned long groups)
{
  int ret;
  struct sockaddr_nl snl;
  int sock;
  int namelen;
  int save_errno;

  sock = socket (AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
  if (sock < 0)
      return -1;

  memset (&snl, 0, sizeof snl);
  snl.nl_family = AF_NETLINK;
  snl.nl_groups = groups;

  /* Bind the socket to the netlink structure for anything. */
  ret = bind (sock, (struct sockaddr *) &snl, sizeof snl);
  save_errno = errno;

  if (ret < 0)
    {
			close (sock);
      return -1;
    }

  /* multiple netlink sockets will have different nl_pid */
  namelen = sizeof snl;
  ret = getsockname (sock, (struct sockaddr *) &snl, (socklen_t *) &namelen);
  if (ret < 0 || namelen != sizeof snl)
    {
      close (sock);
      return -1;
    }

  nl->snl = snl;
  nl->sock = sock;
  return ret;
}

/* Exported interface function.  This function simply calls
   sisis_netlink_socket (). */
void sisis_kernel_init (void)
{
	if (sisis_netlink_cmd.sock == -1)
		sisis_netlink_socket (&sisis_netlink_cmd, 0);
}

/* Get type specified information from netlink. */
static int
sisis_netlink_request (int family, int type, struct nlsock *nl)
{
  int ret;
  struct sockaddr_nl snl;
  int save_errno;

  struct
  {
    struct nlmsghdr nlh;
    struct rtgenmsg g;
  } req;


  /* Check netlink socket. */
  if (nl->sock < 0)
      return -1;

  memset (&snl, 0, sizeof snl);
  snl.nl_family = AF_NETLINK;

  memset (&req, 0, sizeof req);
  req.nlh.nlmsg_len = sizeof req;
  req.nlh.nlmsg_type = type;
  req.nlh.nlmsg_flags = NLM_F_ROOT | NLM_F_MATCH | NLM_F_REQUEST;
  req.nlh.nlmsg_pid = nl->snl.nl_pid;
  req.nlh.nlmsg_seq = ++nl->seq;
  req.g.rtgen_family = family;

  ret = sendto (nl->sock, (void *) &req, sizeof req, 0,
                (struct sockaddr *) &snl, sizeof snl);
  save_errno = errno;

  if (ret < 0)
      return -1;

  return 0;
}

/* Receive message from netlink interface and pass those information
   to the given function. */
static int
sisis_netlink_parse_info (int (*filter) (struct sockaddr_nl *, struct nlmsghdr *, void *),
                    struct nlsock *nl, void * info)
{
  int status;
  int ret = 0;
  int error;

  while (1)
    {
      char buf[4096];
      struct iovec iov = { buf, sizeof buf };
      struct sockaddr_nl snl;
      struct msghdr msg = { (void *) &snl, sizeof snl, &iov, 1, NULL, 0, 0 };
      struct nlmsghdr *h;

      status = recvmsg (nl->sock, &msg, 0);
      if (status < 0)
        {
          if (errno == EINTR)
            continue;
          if (errno == EWOULDBLOCK || errno == EAGAIN)
            break;
          continue;
        }

      if (status == 0)
          return -1;

      if (msg.msg_namelen != sizeof snl)
          return -1;
      
      for (h = (struct nlmsghdr *) buf; NLMSG_OK (h, (unsigned int) status);
           h = NLMSG_NEXT (h, status))
        {
          /* Finish of reading. */
          if (h->nlmsg_type == NLMSG_DONE)
            return ret;

          /* Error handling. */
          if (h->nlmsg_type == NLMSG_ERROR)
            {
              struct nlmsgerr *err = (struct nlmsgerr *) NLMSG_DATA (h);
	      int errnum = err->error;
	      int msg_type = err->msg.nlmsg_type;

              /* If the error field is zero, then this is an ACK */
              if (err->error == 0)
                {
                  /* return if not a multipart message, otherwise continue */
                  if (!(h->nlmsg_flags & NLM_F_MULTI))
                    {
                      return 0;
                    }
                  continue;
                }

              if (h->nlmsg_len < NLMSG_LENGTH (sizeof (struct nlmsgerr)))
                  return -1;

              /* Deal with errors that occur because of races in link handling */
	      if (nl == &sisis_netlink_cmd
		  && ((msg_type == RTM_DELROUTE &&
		       (-errnum == ENODEV || -errnum == ESRCH))
		      || (msg_type == RTM_NEWROUTE && -errnum == EEXIST)))
		{
		  return 0;
		}
              return -1;
            }

          /* OK we got netlink message. */
          
          /* skip unsolicited messages originating from command socket */
					/*
          if (nl != &sisis_netlink_cmd && h->nlmsg_pid == sisis_netlink_cmd.snl.nl_pid)
            continue;
					*/

          error = (*filter) (&snl, h, info);
          if (error < 0)
              ret = error;
        }

      /* After error care. */
      if (msg.msg_flags & MSG_TRUNC)
          continue;
      if (status)
          return -1;
    }
  return ret;
}

/* Utility function for parse rtattr. */
static void
sisis_netlink_parse_rtattr (struct rtattr **tb, int max, struct rtattr *rta,
                      int len)
{
  while (RTA_OK (rta, len))
    {
      if (rta->rta_type <= max)
        tb[rta->rta_type] = rta;
      rta = RTA_NEXT (rta, len);
    }
}

/* Looking up routing table by netlink interface. */
static int
sisis_netlink_routing_table (struct sockaddr_nl *snl, struct nlmsghdr *h, void * info)
{
	// Convert info
	struct sisis_netlink_routing_table_info * real_info = (struct sisis_netlink_routing_table_info *)info;
	
  int len;
  struct rtmsg *rtm;
  struct rtattr *tb[RTA_MAX + 1];
  u_char flags = 0;

  char anyaddr[16] = { 0 };

  int index;
  int table;
  int metric;

  void *dest;
  void *gate;
  void *src;

  rtm = NLMSG_DATA (h);
	
	// Make sure this is an add or delete of a route
  if (h->nlmsg_type != RTM_NEWROUTE && h->nlmsg_type != RTM_DELROUTE)
    return 0;
  //if (rtm->rtm_type != RTN_UNICAST)
    //return 0;
	
	// Ignore unreachable routes
	if (rtm->rtm_type == RTN_UNREACHABLE)
		return 0;

  table = rtm->rtm_table;

  len = h->nlmsg_len - NLMSG_LENGTH (sizeof (struct rtmsg));
  if (len < 0)
    return -1;

  memset (tb, 0, sizeof tb);
  sisis_netlink_parse_rtattr (tb, RTA_MAX, RTM_RTA (rtm), len);

  if (rtm->rtm_flags & RTM_F_CLONED)
    return 0;
  if (rtm->rtm_protocol == RTPROT_REDIRECT)
    return 0;
  //if (rtm->rtm_protocol == RTPROT_KERNEL)
    //return 0;

  if (rtm->rtm_src_len != 0)
    return 0;

  index = 0;
  metric = 0;
  dest = NULL;
  gate = NULL;
  src = NULL;

  if (tb[RTA_OIF])
    index = *(int *) RTA_DATA (tb[RTA_OIF]);

  if (tb[RTA_DST])
    dest = RTA_DATA (tb[RTA_DST]);
  else
    dest = anyaddr;

  if (tb[RTA_PREFSRC])
    src = RTA_DATA (tb[RTA_PREFSRC]);

  /* Multipath treatment is needed. */
  if (tb[RTA_GATEWAY])
    gate = RTA_DATA (tb[RTA_GATEWAY]);

  if (tb[RTA_PRIORITY])
    metric = *(int *) RTA_DATA(tb[RTA_PRIORITY]);

  if (rtm->rtm_family == AF_INET)
    {
			// Check for callback
			if ((h->nlmsg_type == RTM_NEWROUTE && real_info->rib_add_ipv4_route) || (h->nlmsg_type == RTM_DELROUTE && real_info->rib_remove_ipv4_route))
			{
				// Construct route info
				struct route_ipv4 * route = malloc(sizeof(struct route_ipv4));
				route->type = 1;	// Means nothing right now
				route->flags = flags;
				route->p = malloc(sizeof(struct prefix_ipv4));
				route->p->family = AF_INET;
				memcpy (&route->p->prefix, dest, 4);
				route->p->prefixlen = rtm->rtm_dst_len;
				route->gate = gate;
				route->src = src;
				route->ifindex = index;
				route->vrf_id = table;
				route->metric = metric;
				route->distance = 0;
				
				// Note: Receivers responsibilty to free memory for route
				
				(h->nlmsg_type == RTM_NEWROUTE) ? real_info->rib_add_ipv4_route(route) : real_info->rib_remove_ipv4_route(route);
			}
    }
#ifdef HAVE_IPV6
  if (rtm->rtm_family == AF_INET6)
    {
			// Check for callback
			if ((h->nlmsg_type == RTM_NEWROUTE && real_info->rib_add_ipv6_route) || (h->nlmsg_type == RTM_DELROUTE && real_info->rib_remove_ipv6_route))
			{
				// Construct route info
				struct route_ipv6 * route = malloc(sizeof(struct route_ipv6));
				route->type = 1;	// Means nothing right now
				route->flags = flags;
				route->p = malloc(sizeof(struct prefix_ipv6));
				route->p->family = AF_INET6;
				memcpy (&route->p->prefix, dest, 16);
				route->p->prefixlen = rtm->rtm_dst_len;
				route->gate = gate;
				route->ifindex = index;
				route->vrf_id = table;
				route->metric = metric;
				route->distance = 0;
				
				// Note: Receivers responsibilty to free memory for route
				
				(h->nlmsg_type == RTM_NEWROUTE) ? real_info->rib_add_ipv6_route(route) : real_info->rib_remove_ipv6_route(route);
			}
    }
#endif /* HAVE_IPV6 */

  return 0;
}

/* Routing table read function using netlink interface. */
int sisis_netlink_route_read (struct sisis_netlink_routing_table_info * info)
{
	// Initialize kernel socket
	sisis_kernel_init();
	
  int ret;
	
	/* Get IPv4 routing table. */
  ret = sisis_netlink_request (AF_INET, RTM_GETROUTE, &sisis_netlink_cmd);
  if (ret < 0)
    return ret;
  ret = sisis_netlink_parse_info (sisis_netlink_routing_table, &sisis_netlink_cmd, (void*)info);
  if (ret < 0)
    return ret;

#ifdef HAVE_IPV6
  /* Get IPv6 routing table. */
  ret = sisis_netlink_request (AF_INET6, RTM_GETROUTE, &sisis_netlink_cmd);
  if (ret < 0)
    return ret;
  ret = sisis_netlink_parse_info (sisis_netlink_routing_table, &sisis_netlink_cmd, (void*)info);
  if (ret < 0)
    return ret;
#endif /* HAVE_IPV6 */

  return 0;
}

/* Thread to wait for and process rib changes on a socket. */
void * sisis_netlink_wait_for_rib_changes(void * info)
{
	struct sisis_netlink_wait_for_rib_changes_info * real_info = (struct sisis_netlink_wait_for_rib_changes_info *)info;
	sisis_netlink_parse_info(sisis_netlink_routing_table, real_info->netlink_rib, (void*)real_info->info);
}

/* Subscribe to routing table using netlink interface. */
int sisis_netlink_subscribe_to_rib_changes(struct sisis_netlink_routing_table_info * info)
{
	// Set up kernel socket
	struct nlsock * netlink_rib  = malloc(sizeof(struct nlsock));
	netlink_rib->sock = -1;
  netlink_rib->seq = 0;
  memset(&netlink_rib->snl, 0, sizeof(netlink_rib->snl));
  netlink_rib->name = "netlink";
	
	// Set up groups to listen for
	unsigned long groups= RTMGRP_IPV4_ROUTE;
#ifdef HAVE_IPV6
  groups |= RTMGRP_IPV6_ROUTE;
#endif /* HAVE_IPV6 */
	int rtn = sisis_netlink_socket (netlink_rib, groups);
	if (rtn < 0)
		return rtn;
	
	// Start thread
	pthread_t * thread = malloc(sizeof(pthread_t));
	struct sisis_netlink_wait_for_rib_changes_info * thread_info = malloc(sizeof(struct sisis_netlink_wait_for_rib_changes_info * ));
	thread_info->netlink_rib = netlink_rib;
	thread_info->info = info;
	pthread_create(thread, NULL, sisis_netlink_wait_for_rib_changes, thread_info);
	
	return 0;
}
