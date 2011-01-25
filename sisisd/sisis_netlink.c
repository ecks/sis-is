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

/* Hack for GNU libc version 2. */
#ifndef MSG_TRUNC
#define MSG_TRUNC      0x20
#endif /* MSG_TRUNC */

#include <zebra.h>

extern int sisis_rib_add_ipv4 (int type, int flags, struct prefix_ipv4 *p, 
	      struct in_addr *gate, struct in_addr *src,
	      unsigned int ifindex, u_int32_t vrf_id,
	      u_int32_t metric, u_char distance);
extern int sisis_rib_add_ipv6 (int type, int flags, struct prefix_ipv6 *p,
	      struct in6_addr *gate, unsigned int ifindex, u_int32_t vrf_id,
	      u_int32_t metric, u_char distance);

/* Socket interface to kernel */
struct nlsock
{
  int sock;
  int seq;
  struct sockaddr_nl snl;
  const char *name;
} sisis_netlink_cmd  = { -1, 0, {0}, "netlink-cmd"};        /* command channel */

static const struct message nlmsg_str[] = {
  {RTM_NEWROUTE, "RTM_NEWROUTE"},
  {RTM_DELROUTE, "RTM_DELROUTE"},
  {RTM_GETROUTE, "RTM_GETROUTE"},
  {RTM_NEWLINK,  "RTM_NEWLINK"},
  {RTM_DELLINK,  "RTM_DELLINK"},
  {RTM_GETLINK,  "RTM_GETLINK"},
  {RTM_NEWADDR,  "RTM_NEWADDR"},
  {RTM_DELADDR,  "RTM_DELADDR"},
  {RTM_GETADDR,  "RTM_GETADDR"},
  {0, NULL}
};

extern struct zebra_privs_t zserv_privs;

/* Make socket for Linux netlink interface. */
static int
netlink_socket (struct nlsock *nl, unsigned long groups)
{
  int ret;
  struct sockaddr_nl snl;
  int sock;
  int namelen;
  int save_errno;

  sock = socket (AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
  if (sock < 0)
    {
      zlog_err("Can't open %s socket: %s", nl->name,
            safe_strerror (errno));
      return -1;
    }

  memset (&snl, 0, sizeof snl);
  snl.nl_family = AF_NETLINK;
  snl.nl_groups = groups;

  /* Bind the socket to the netlink structure for anything. */
  if (zserv_privs.change (ZPRIVS_RAISE))
    {
      zlog_err("Can't raise privileges");
      return -1;
    }

  ret = bind (sock, (struct sockaddr *) &snl, sizeof snl);
  save_errno = errno;
  if (zserv_privs.change (ZPRIVS_LOWER))
    zlog_err("Can't lower privileges");

  if (ret < 0)
    {
      zlog_err("Can't bind %s socket to group 0x%x: %s",
            nl->name, snl.nl_groups, safe_strerror (save_errno));
      close (sock);
      return -1;
    }

  /* multiple netlink sockets will have different nl_pid */
  namelen = sizeof snl;
  ret = getsockname (sock, (struct sockaddr *) &snl, (socklen_t *) &namelen);
  if (ret < 0 || namelen != sizeof snl)
    {
      zlog_err("Can't get %s socket name: %s", nl->name,
            safe_strerror (errno));
      close (sock);
      return -1;
    }

  nl->snl = snl;
  nl->sock = sock;
  return ret;
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
    {
      zlog_err("%s socket isn't active.", nl->name);
      return -1;
    }

  memset (&snl, 0, sizeof snl);
  snl.nl_family = AF_NETLINK;

  memset (&req, 0, sizeof req);
  req.nlh.nlmsg_len = sizeof req;
  req.nlh.nlmsg_type = type;
  req.nlh.nlmsg_flags = NLM_F_ROOT | NLM_F_MATCH | NLM_F_REQUEST;
  req.nlh.nlmsg_pid = nl->snl.nl_pid;
  req.nlh.nlmsg_seq = ++nl->seq;
  req.g.rtgen_family = family;

  /* linux appears to check capabilities on every message 
   * have to raise caps for every message sent
   */
  if (zserv_privs.change (ZPRIVS_RAISE))
    {
      zlog_err("Can't raise privileges");
      return -1;
    }

  ret = sendto (nl->sock, (void *) &req, sizeof req, 0,
                (struct sockaddr *) &snl, sizeof snl);
  save_errno = errno;

  if (zserv_privs.change (ZPRIVS_LOWER))
    zlog_err("Can't lower privileges");

  if (ret < 0)
    {
      zlog_err("%s sendto failed: %s", nl->name,
            safe_strerror (save_errno));
      return -1;
    }

  return 0;
}

/* Receive message from netlink interface and pass those information
   to the given function. */
static int
sisis_netlink_parse_info (int (*filter) (struct sockaddr_nl *, struct nlmsghdr *),
                    struct nlsock *nl)
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
          zlog_err("%s recvmsg overrun: %s",
	  	nl->name, safe_strerror(errno));
          continue;
        }

      if (status == 0)
        {
          zlog_err("%s EOF", nl->name);
          return -1;
        }

      if (msg.msg_namelen != sizeof snl)
        {
          zlog_err("%s sender address length error: length %d",
                nl->name, msg.msg_namelen);
          return -1;
        }
      
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
                  if (IS_ZEBRA_DEBUG_KERNEL)
                    {
                      zlog_debug ("%s: %s ACK: type=%s(%u), seq=%u, pid=%u",
                                 __FUNCTION__, nl->name,
                                 lookup (nlmsg_str, err->msg.nlmsg_type),
                                 err->msg.nlmsg_type, err->msg.nlmsg_seq,
                                 err->msg.nlmsg_pid);
                    }

                  /* return if not a multipart message, otherwise continue */
                  if (!(h->nlmsg_flags & NLM_F_MULTI))
                    {
                      return 0;
                    }
                  continue;
                }

              if (h->nlmsg_len < NLMSG_LENGTH (sizeof (struct nlmsgerr)))
                {
                  zlog_err("%s error: message truncated",
                        nl->name);
                  return -1;
                }

              /* Deal with errors that occur because of races in link handling */
	      if (nl == &sisis_netlink_cmd
		  && ((msg_type == RTM_DELROUTE &&
		       (-errnum == ENODEV || -errnum == ESRCH))
		      || (msg_type == RTM_NEWROUTE && -errnum == EEXIST)))
		{
		  if (IS_ZEBRA_DEBUG_KERNEL)
		    zlog_debug ("%s: error: %s type=%s(%u), seq=%u, pid=%u",
				nl->name, safe_strerror (-errnum),
				lookup (nlmsg_str, msg_type),
				msg_type, err->msg.nlmsg_seq, err->msg.nlmsg_pid);
		  return 0;
		}

	      zlog_err ("%s error: %s, type=%s(%u), seq=%u, pid=%u",
			nl->name, safe_strerror (-errnum),
			lookup (nlmsg_str, msg_type),
			msg_type, err->msg.nlmsg_seq, err->msg.nlmsg_pid);
              return -1;
            }

          /* OK we got netlink message. */
          if (IS_ZEBRA_DEBUG_KERNEL)
            zlog_debug ("sisis_netlink_parse_info: %s type %s(%u), seq=%u, pid=%u",
                       nl->name,
                       lookup (nlmsg_str, h->nlmsg_type), h->nlmsg_type,
                       h->nlmsg_seq, h->nlmsg_pid);

          /* skip unsolicited messages originating from command socket */
          if (nl != &sisis_netlink_cmd && h->nlmsg_pid == sisis_netlink_cmd.snl.nl_pid)
            {
              if (IS_ZEBRA_DEBUG_KERNEL)
                zlog_debug ("sisis_netlink_parse_info: %s packet comes from %s",
                            sisis_netlink_cmd.name, nl->name);
              continue;
            }

          error = (*filter) (&snl, h);
          if (error < 0)
            {
              zlog_err("%s filter function error", nl->name);
              ret = error;
            }
        }

      /* After error care. */
      if (msg.msg_flags & MSG_TRUNC)
        {
          zlog_err("%s error: message truncated", nl->name);
          continue;
        }
      if (status)
        {
          zlog_err("%s error: data remnant size %d", nl->name,
                status);
          return -1;
        }
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
sisis_netlink_routing_table (struct sockaddr_nl *snl, struct nlmsghdr *h)
{
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

  if (h->nlmsg_type != RTM_NEWROUTE)
    return 0;
  if (rtm->rtm_type != RTN_UNICAST)
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
  if (rtm->rtm_protocol == RTPROT_KERNEL)
    return 0;

  if (rtm->rtm_src_len != 0)
    return 0;

  /* Route which inserted by Zebra. */
  if (rtm->rtm_protocol == RTPROT_ZEBRA)
    flags |= ZEBRA_FLAG_SELFROUTE;

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
      struct prefix_ipv4 p;
      p.family = AF_INET;
      memcpy (&p.prefix, dest, 4);
      p.prefixlen = rtm->rtm_dst_len;

      sisis_rib_add_ipv4 (ZEBRA_ROUTE_KERNEL, flags, &p, gate, src, index, table, metric, 0);
    }
#ifdef HAVE_IPV6
  if (rtm->rtm_family == AF_INET6)
    {
      struct prefix_ipv6 p;
      p.family = AF_INET6;
      memcpy (&p.prefix, dest, 16);
      p.prefixlen = rtm->rtm_dst_len;

      sisis_rib_add_ipv6 (ZEBRA_ROUTE_KERNEL, flags, &p, gate, index, table,
		    metric, 0);
    }
#endif /* HAVE_IPV6 */

  return 0;
}

/* Routing table read function using netlink interface.  Only called
   bootstrap time. */
int
sisis_netlink_route_read (void)
{
  int ret;
	
	kernel_init();

  /* Get IPv4 routing table. */
  ret = sisis_netlink_request (AF_INET, RTM_GETROUTE, &sisis_netlink_cmd);
  if (ret < 0)
    return ret;
  ret = sisis_netlink_parse_info (sisis_netlink_routing_table, &sisis_netlink_cmd);
  if (ret < 0)
    return ret;

#ifdef HAVE_IPV6
  /* Get IPv6 routing table. */
  ret = sisis_netlink_request (AF_INET6, RTM_GETROUTE, &sisis_netlink_cmd);
  if (ret < 0)
    return ret;
  ret = sisis_sisis_netlink_parse_info (sisis_netlink_routing_table, &sisis_netlink_cmd);
  if (ret < 0)
    return ret;
#endif /* HAVE_IPV6 */

  return 0;
}

/* Exported interface function.  This function simply calls
   netlink_socket (). */
void
sisis_kernel_init (void)
{
  netlink_socket (&sisis_netlink_cmd, 0);
}
