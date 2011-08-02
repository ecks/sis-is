/* Zebra's client library.
 * Copyright (C) 1999 Kunihiro Ishiguro
 * Copyright (C) 2005 Andrew J. Schorr
 *
 * This file is part of GNU Zebra.
 *
 * GNU Zebra is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2, or (at your
 * option) any later version.
 *
 * GNU Zebra is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Zebra; see the file COPYING.  If not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA 02111-1307, USA.
 */

#include <zebra.h>
#include <time.h>

#include "prefix.h"
#include "stream.h"
#include "buffer.h"
#include "network.h"
#include "if.h"
#include "log.h"
#include "thread.h"
#include "svz_client.h"
#include "memory.h"
#include "table.h"
#include "ospf6_network.h"


/* Zebra client events. */
enum event {SVZCLIENT_SCHEDULE, SVZCLIENT_READ, SVZCLIENT_CONNECT};

/* Prototype for event manager. */
static void svzclient_event (enum event, struct svzclient *);

extern struct thread_master *master;

/* This file local debug flag. */
int svzclient_debug = 1;

/* Allocate zclient structure. */
struct svzclient *
svzclient_new ()
{
  struct svzclient * svzclient;
  svzclient = XCALLOC (MTYPE_ZCLIENT, sizeof (struct svzclient));

  svzclient->ibuf = stream_new (ZEBRA_MAX_PACKET_SIZ);
  svzclient->obuf = stream_new (ZEBRA_MAX_PACKET_SIZ);
  svzclient->wb = buffer_new(0);

  return svzclient;
}

/* This function is only called when exiting, because
   many parts of the code do not check for I/O errors, so they could
   reference an invalid pointer if the structure was ever freed.

   Free zclient structure. */
void
svzclient_free (struct svzclient *svzclient)
{
  if (svzclient->ibuf)
    stream_free(svzclient->ibuf);
  if (svzclient->obuf)
    stream_free(svzclient->obuf);
  if (svzclient->wb)
    buffer_free(svzclient->wb);

  XFREE (MTYPE_ZCLIENT, svzclient);
}

/* Initialize zebra client.  Argument redist_default is unwanted
   redistribute route type. */
void
svzclient_init (struct svzclient *svzclient, int redist_default, struct in6_addr * sv_addr)
{
  int i;
  
  /* Enable zebra client connection by default. */
  svzclient->enable = 1;

  /* Set -1 to the default socket value. */
  svzclient->sock = -1;

  /* Clear redistribution flags. */
  for (i = 0; i < ZEBRA_ROUTE_MAX; i++)
    svzclient->redist[i] = 0;

  /* Set unwanted redistribute route.  bgpd does not need BGP route
     redistribution. */
  svzclient->redist_default = redist_default;
  svzclient->redist[redist_default] = 1;

  /* Set default-information redistribute to zero. */
  svzclient->default_information = 0;
  
  svzclient->sv_addr = sv_addr;

  /* Schedule first zclient connection. */
  if (svzclient_debug)
    zlog_debug ("svzclient start scheduled");

  svzclient_event (SVZCLIENT_SCHEDULE, svzclient);
}

/* Stop zebra client services. */
void
svzclient_stop (struct svzclient *svzclient)
{
  if (svzclient_debug)
    zlog_debug ("svzclient stopped");

  /* Stop threads. */
  THREAD_OFF(svzclient->t_read);
  THREAD_OFF(svzclient->t_connect);
  THREAD_OFF(svzclient->t_write);

  /* Reset streams. */
  stream_reset(svzclient->ibuf);
  stream_reset(svzclient->obuf);

  /* Empty the write buffer. */
  buffer_reset(svzclient->wb);

  /* Close socket. */
  if (svzclient->sock >= 0)
  {
    close (svzclient->sock);
    svzclient->sock = -1;
  }
  svzclient->fail = 0;
}

void
svzclient_reset (struct svzclient *svzclient)
{
  svzclient_stop (svzclient);
  svzclient_init (svzclient, svzclient->redist_default, svzclient->sv_addr);
}

/* Make socket to zebra daemon. Return zebra socket. */
int
svzclient_socket(struct in6_addr * svz_addr)
{
  char s_addr[INET6_ADDRSTRLEN+1];
  int sock;
  int ret;
  int status;

  inet_ntop(AF_INET6, svz_addr, s_addr, INET6_ADDRSTRLEN+1);

  // Set up socket address info
  struct addrinfo hints, *addr;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET6;     // IPv6
  hints.ai_socktype = SOCK_STREAM;  // TCP
  char port_str[8];
  sprintf(port_str, "%u", SVZ_SISIS_PORT);
  if((status = getaddrinfo(s_addr, port_str, &hints, &addr)) != 0)
  {
    fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
    exit(1);
  }

  sock = socket (addr->ai_family, addr->ai_socktype, addr->ai_protocol);
  if (sock < 0)    
  {         
    zlog_warn ("Network: can't create socket.");
  }
  
  if (svzclient_debug)
    zlog_debug("svzclient TCP socket: creating socket %d", sock);
    

  /* Connect to zebra. */
  ret = connect (sock, addr->ai_addr, addr->ai_addrlen);
  if (ret < 0)
    {
      close (sock);
      perror("connect");
      return -1;
    }
  return sock;
}

static int
svzclient_failed(struct svzclient *svzclient)
{
  svzclient->fail++;
  svzclient_stop(svzclient);
  svzclient_event(SVZCLIENT_CONNECT, svzclient);
  return -1;
}

static int
svzclient_flush_data(struct thread *thread)
{
  struct svzclient *svzclient = THREAD_ARG(thread);

  svzclient->t_write = NULL;
  if (svzclient->sock < 0)
    return -1;
  switch (buffer_flush_available(svzclient->wb, svzclient->sock))
    {
    case BUFFER_ERROR:
      zlog_warn("%s: buffer_flush_available failed on svzclient fd %d, closing",
      		__func__, svzclient->sock);
      return svzclient_failed(svzclient);
      break;
    case BUFFER_PENDING:
      svzclient->t_write = thread_add_write(master, svzclient_flush_data,
					  svzclient, svzclient->sock);
      break;
    case BUFFER_EMPTY:
      break;
    }
  return 0;
}

int
svzclient_send_message(struct svzclient *svzclient)
{
  if (svzclient->sock < 0)
    return -1;
  switch (buffer_write(svzclient->wb, svzclient->sock, STREAM_DATA(svzclient->obuf),
		       stream_get_endp(svzclient->obuf)))
    {
    case BUFFER_ERROR:
      zlog_warn("%s: buffer_write failed to svzclient fd %d, closing",
      		 __func__, svzclient->sock);
      return svzclient_failed(svzclient);
      break;
    case BUFFER_EMPTY:
      THREAD_OFF(svzclient->t_write);
      break;
    case BUFFER_PENDING:
      THREAD_WRITE_ON(master, svzclient->t_write,
		      svzclient_flush_data, svzclient, svzclient->sock);
      break;
    }
  return 0;
}

void
svzclient_create_header (struct stream *s, uint16_t command)
{
  /* length placeholder, caller can update */
  stream_putw (s, ZEBRA_HEADER_SIZE);
  stream_putc (s, ZEBRA_HEADER_MARKER);
  stream_putc (s, ZSERV_VERSION);
  stream_putw (s, command);
}

/* Send simple Zebra message. */
static int
zebra_message_send (struct svzclient *svzclient, int command)
{
  struct stream *s;

  /* Get zclient output buffer. */
  s = svzclient->obuf;
  stream_reset (s);

  /* Send very simple command only Zebra message. */
  svzclient_create_header (s, command);
  
  return svzclient_send_message(svzclient);
}

/* Make connection to zebra daemon. */
int
svzclient_start (struct svzclient *svzclient)
{
  int i;

  if (svzclient_debug)
    zlog_debug ("svzclient_start is called");

  /* zclient is disabled. */
  if (! svzclient->enable)
    return 0;

  /* If already connected to the zebra. */
  if (svzclient->sock >= 0)
    return 0;

  /* Check connect thread. */
  if (svzclient->t_connect)
    return 0;

  /* Make socket. */
  svzclient->sock = svzclient_socket (svzclient->sv_addr);
  if (svzclient->sock < 0)
    {
      if (svzclient_debug)
	zlog_debug ("svzclient connection fail");
      svzclient->fail++;
      svzclient_event (SVZCLIENT_CONNECT, svzclient);
      return -1;
    }

  if (set_nonblocking(svzclient->sock) < 0)
    zlog_warn("%s: set_nonblocking(%d) failed", __func__, svzclient->sock);

  /* Clear fail count. */
  svzclient->fail = 0;
  if (svzclient_debug)
    zlog_debug ("svzclient connect success with socket [%d]", svzclient->sock);
      
  /* Create read thread. */
  svzclient_event (SVZCLIENT_READ, svzclient);

  /* We need router-id information. */
  zebra_message_send (svzclient, ZEBRA_ROUTER_ID_ADD);

  /* We need interface information. */
  zebra_message_send (svzclient, ZEBRA_INTERFACE_ADD);

  /* Flush all redistribute request. */
  for (i = 0; i < ZEBRA_ROUTE_MAX; i++)
    if (i != svzclient->redist_default && svzclient->redist[i])
      svzebra_redistribute_send (ZEBRA_REDISTRIBUTE_ADD, svzclient, i);

  /* If default information is needed. */
  if (svzclient->default_information)
    zebra_message_send (svzclient, ZEBRA_REDISTRIBUTE_DEFAULT_ADD);

  return 0;
}

/* This function is a wrapper function for calling zclient_start from
   timer or event thread. */
static int
svzclient_connect (struct thread *t)
{
  struct svzclient *svzclient;

  svzclient = THREAD_ARG (t);
  svzclient->t_connect = NULL;

  if (svzclient_debug)
    zlog_debug ("svzclient_connect is called");

  return svzclient_start (svzclient);
}

 /* 
  * "xdr_encode"-like interface that allows daemon (client) to send
  * a message to zebra server for a route that needs to be
  * added/deleted to the kernel. Info about the route is specified
  * by the caller in a struct zapi_ipv4. zapi_ipv4_read() then writes
  * the info down the zclient socket using the stream_* functions.
  * 
  * The corresponding read ("xdr_decode") function on the server
  * side is zread_ipv4_add()/zread_ipv4_delete().
  *
  *  0 1 2 3 4 5 6 7 8 9 A B C D E F 0 1 2 3 4 5 6 7 8 9 A B C D E F
  * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  * |            Length (2)         |    Command    | Route Type    |
  * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  * | ZEBRA Flags   | Message Flags | Prefix length |
  * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  * | Destination IPv4 Prefix for route                             |
  * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  * | Nexthop count | 
  * +-+-+-+-+-+-+-+-+
  *
  * 
  * A number of IPv4 nexthop(s) or nexthop interface index(es) are then 
  * described, as per the Nexthop count. Each nexthop described as:
  *
  * +-+-+-+-+-+-+-+-+
  * | Nexthop Type  |  Set to one of ZEBRA_NEXTHOP_*
  * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  * |       IPv4 Nexthop address or Interface Index number          |
  * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  *
  * Alternatively, if the flags field has ZEBRA_FLAG_BLACKHOLE or
  * ZEBRA_FLAG_REJECT is set then Nexthop count is set to 1, then _no_ 
  * nexthop information is provided, and the message describes a prefix
  * to blackhole or reject route.
  *
  * If ZAPI_MESSAGE_DISTANCE is set, the distance value is written as a 1
  * byte value.
  * 
  * If ZAPI_MESSAGE_METRIC is set, the metric value is written as an 8
  * byte value.
  *
  * XXX: No attention paid to alignment.
  */ 
/*
int
svzapi_ipv4_route (u_char cmd, struct svzclient *svzclient, struct prefix_ipv4 *p,
                 struct zapi_ipv4 *api)
{
  int i;
  int psize;
  struct stream *s;
*/
  /* Reset stream. */
/*  s = zclient->obuf;
  stream_reset (s);
  
  svzclient_create_header (s, cmd);
*/ 
  /* Put type and nexthop. */
/*  stream_putc (s, api->type);
  stream_putc (s, api->flags);
  stream_putc (s, api->message);
*/
  /* Put prefix information. */
/*  psize = PSIZE (p->prefixlen);
  stream_putc (s, p->prefixlen);
  stream_write (s, (u_char *) & p->prefix, psize);
*/
  /* Nexthop, ifindex, distance and metric information. */
/*  if (CHECK_FLAG (api->message, ZAPI_MESSAGE_NEXTHOP))
    {
      if (CHECK_FLAG (api->flags, ZEBRA_FLAG_BLACKHOLE))
        {
          stream_putc (s, 1);
          stream_putc (s, ZEBRA_NEXTHOP_BLACKHOLE); */
          /* XXX assert(api->nexthop_num == 0); */
          /* XXX assert(api->ifindex_num == 0); */
/*        }
      else
        stream_putc (s, api->nexthop_num + api->ifindex_num);

      for (i = 0; i < api->nexthop_num; i++)
        {
          stream_putc (s, ZEBRA_NEXTHOP_IPV4);
          stream_put_in_addr (s, api->nexthop[i]);
        }
      for (i = 0; i < api->ifindex_num; i++)
        {
          stream_putc (s, ZEBRA_NEXTHOP_IFINDEX);
          stream_putl (s, api->ifindex[i]);
        }
    }

  if (CHECK_FLAG (api->message, ZAPI_MESSAGE_DISTANCE))
    stream_putc (s, api->distance);
  if (CHECK_FLAG (api->message, ZAPI_MESSAGE_METRIC))
    stream_putl (s, api->metric);
*/
  /* Put length at the first point of the stream. */
/*  stream_putw_at (s, 0, stream_get_endp (s));

  return zclient_send_message(zclient);
}
*/
#ifdef HAVE_IPV6
int
svzapi_ipv6_route (u_char cmd, struct svzclient *svzclient, struct prefix_ipv6 *p,
	       struct svzapi_ipv6 *api)
{
  int i;
  int psize;
  struct stream *s;

  /* Reset stream. */
  s = svzclient->obuf;
  stream_reset (s);

  svzclient_create_header (s, cmd);

  /* Put type and nexthop. */
  stream_putc (s, api->type);
  stream_putc (s, api->flags);
  stream_putc (s, api->message);
  
  /* Put prefix information. */
  psize = PSIZE (p->prefixlen);
  stream_putc (s, p->prefixlen);
  stream_write (s, (u_char *)&p->prefix, psize);

  /* Nexthop, ifindex, distance and metric information. */
  if (CHECK_FLAG (api->message, ZAPI_MESSAGE_NEXTHOP))
  {
    stream_putc (s, api->nexthop_num + api->ifindex_num);

    for (i = 0; i < api->nexthop_num; i++)
    {
      stream_putc (s, ZEBRA_NEXTHOP_IPV6);
      stream_write (s, (u_char *)api->nexthop[i], 16);
    }
    for (i = 0; i < api->ifindex_num; i++)
    {
      stream_putc (s, ZEBRA_NEXTHOP_IFINDEX);
      stream_putl (s, api->ifindex[i]);
    }
  }

  if (CHECK_FLAG (api->message, ZAPI_MESSAGE_DISTANCE))
    stream_putc (s, api->distance);
  if (CHECK_FLAG (api->message, ZAPI_MESSAGE_METRIC))
    stream_putl (s, api->metric);

  /* Put length at the first point of the stream. */
  stream_putw_at (s, 0, stream_get_endp (s));

  return svzclient_send_message(svzclient);
}
#endif /* HAVE_IPV6 */

/* 
 * send a ZEBRA_REDISTRIBUTE_ADD or ZEBRA_REDISTRIBUTE_DELETE
 * for the route type (ZEBRA_ROUTE_KERNEL etc.). The zebra server will
 * then set/unset redist[type] in the client handle (a struct zserv) for the 
 * sending client
 */

int
svzebra_redistribute_send (int command, struct svzclient *svzclient, int type)
{
  struct stream *s;

  s = svzclient->obuf;
  stream_reset(s);
  
  svzclient_create_header (s, command);
  stream_putc (s, type);
  
  stream_putw_at (s, 0, stream_get_endp (s));
  
  return svzclient_send_message(svzclient);
}

/* Router-id update from zebra daemon. */
void
svzebra_router_id_update_read (struct stream *s, struct prefix *rid)
{
  int plen;

  zlog_notice("router_id_update_read called");

  /* Fetch interface address. */
  rid->family = stream_getc (s);

  plen = prefix_blen (rid);
  stream_get (&rid->u.prefix, s, plen);
  rid->prefixlen = stream_getc (s);
} 

/* Interface addition from zebra daemon. */
/*  
 * The format of the message sent with type ZEBRA_INTERFACE_ADD or
 * ZEBRA_INTERFACE_DELETE from zebra to the client is:
 *     0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+
 * |   type        |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  ifname                                                       |
 * |                                                               |
 * |                                                               |
 * |                                                               |
 * |                                                               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |         ifindex                                               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |         if_flags                                              |
 * |                                                               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |         metric                                                |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |         ifmtu                                                 |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |         ifmtu6                                                |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |         bandwidth                                             |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |         sockaddr_dl                                           |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

struct interface *
svzebra_interface_add_read (struct stream *s)
{
  struct interface *ifp;
  char ifname_tmp[INTERFACE_NAMSIZ];

  /* Read interface name. */
  stream_get (ifname_tmp, s, INTERFACE_NAMSIZ); 

  /* Lookup/create interface by name. */
  ifp = if_get_by_name_len (ifname_tmp, strnlen(ifname_tmp, INTERFACE_NAMSIZ)); 

  /* Read interface's index. */
  ifp->ifindex = stream_getl (s); 

  /* Read interface's value. */
  ifp->status = stream_getc (s);
  ifp->flags = stream_getq (s);
  ifp->metric = stream_getl (s);
  ifp->mtu = stream_getl (s);
  ifp->mtu6 = stream_getl (s);
  ifp->bandwidth = stream_getl (s);
#ifdef HAVE_STRUCT_SOCKADDR_DL
  stream_get (&ifp->sdl, s, sizeof (ifp->sdl));
#else
  ifp->hw_addr_len = stream_getl (s);
  if (ifp->hw_addr_len)
    stream_get (ifp->hw_addr, s, ifp->hw_addr_len);
#endif /* HAVE_STRUCT_SOCKADDR_DL */
  
  return ifp;
}

/* 
 * Read interface up/down msg (ZEBRA_INTERFACE_UP/ZEBRA_INTERFACE_DOWN)
 * from zebra server.  The format of this message is the same as
 * that sent for ZEBRA_INTERFACE_ADD/ZEBRA_INTERFACE_DELETE (see
 * comments for zebra_interface_add_read), except that no sockaddr_dl
 * is sent at the tail of the message.
 */
struct interface *
svzebra_interface_state_read (struct stream *s)
{
  struct interface *ifp;
  char ifname_tmp[INTERFACE_NAMSIZ];

  /* Read interface name. */
  stream_get (ifname_tmp, s, INTERFACE_NAMSIZ); 

  /* Lookup this by interface index. */
  ifp = if_lookup_by_name_len (ifname_tmp,
			       strnlen(ifname_tmp, INTERFACE_NAMSIZ));

  /* If such interface does not exist, indicate an error */
  if (! ifp)
     return NULL;

  /* Read interface's index. */
  ifp->ifindex = stream_getl (s);

  /* Read interface's value. */
  ifp->status = stream_getc (s);
  ifp->flags = stream_getq (s);
  ifp->metric = stream_getl (s);
  ifp->mtu = stream_getl (s);
  ifp->mtu6 = stream_getl (s);
  ifp->bandwidth = stream_getl (s);

  return ifp;
}

/* 
 * format of message for address additon is:
 *    0
 *  0 1 2 3 4 5 6 7
 * +-+-+-+-+-+-+-+-+
 * |   type        |  ZEBRA_INTERFACE_ADDRESS_ADD or
 * +-+-+-+-+-+-+-+-+  ZEBRA_INTERFACE_ADDRES_DELETE
 * |               |
 * +               +
 * |   ifindex     |
 * +               +
 * |               |
 * +               +
 * |               |
 * +-+-+-+-+-+-+-+-+
 * |   ifc_flags   |  flags for connected address
 * +-+-+-+-+-+-+-+-+
 * |  addr_family  |
 * +-+-+-+-+-+-+-+-+
 * |    addr...    |
 * :               :
 * |               |
 * +-+-+-+-+-+-+-+-+
 * |    addr_len   |  len of addr. E.g., addr_len = 4 for ipv4 addrs.
 * +-+-+-+-+-+-+-+-+
 * |     daddr..   |
 * :               :
 * |               |
 * +-+-+-+-+-+-+-+-+
 *
 */
/*
void
zebra_interface_if_set_value (struct stream *s, struct interface *ifp)
{ */
  /* Read interface's index. */
/*  ifp->ifindex = stream_getl (s);
  ifp->status = stream_getc (s);
*/
  /* Read interface's value. */
/*  ifp->flags = stream_getq (s);
  ifp->metric = stream_getl (s);
  ifp->mtu = stream_getl (s);
  ifp->mtu6 = stream_getl (s);
  ifp->bandwidth = stream_getl (s);
}
*/
static int
memconstant(const void *s, int c, size_t n)
{
  const u_char *p = s;

  while (n-- > 0)
    if (*p++ != c)
      return 0;
  return 1;
}

struct connected *
svzebra_interface_address_read (int type, struct stream *s)
{
  unsigned int ifindex;
  struct interface *ifp;
  struct connected *ifc;
  struct prefix p, d;
  int family;
  int plen;
  u_char ifc_flags;

  memset (&p, 0, sizeof(p));
  memset (&d, 0, sizeof(d));

  /* Get interface index. */
  ifindex = stream_getl (s);

  /* Lookup index. */
  ifp = if_lookup_by_index (ifindex);
  if (ifp == NULL)
  {
    zlog_warn ("zebra_interface_address_read(%s): "
               "Can't find interface by ifindex: %d ",
               (type == ZEBRA_INTERFACE_ADDRESS_ADD? "ADD" : "DELETE"),
               ifindex);
    return NULL;
  }

  /* Fetch flag. */
  ifc_flags = stream_getc (s);

  /* Fetch interface address. */
  family = p.family = stream_getc (s);

  plen = prefix_blen (&p);
  stream_get (&p.u.prefix, s, plen);
  p.prefixlen = stream_getc (s);

  /* Fetch destination address. */
  stream_get (&d.u.prefix, s, plen);
  d.family = family;

  if (type == ZEBRA_INTERFACE_ADDRESS_ADD) 
  { 
    /* N.B. NULL destination pointers are encoded as all zeroes */
    ifc = connected_add_by_prefix(ifp, &p,(memconstant(&d.u.prefix,0,plen) ?
					  NULL : &d));
    if (ifc != NULL)
    {
      ifc->flags = ifc_flags;
      if (ifc->destination)
        ifc->destination->prefixlen = ifc->address->prefixlen;
    }
  }
  else
  {
    assert (type == ZEBRA_INTERFACE_ADDRESS_DELETE);
    ifc = connected_delete_by_prefix(ifp, &p);
  }

  return ifc;
}


/* Zebra client message read function. */
static int
svzclient_read (struct thread *thread)
{
  int ret;
  size_t already;
  uint16_t length, command;
  uint8_t marker, version;
  struct svzclient *svzclient;

  /* Get socket to zebra. */
  svzclient = THREAD_ARG (thread);
  svzclient->t_read = NULL;

  /* Read zebra header (if we don't have it already). */
  if ((already = stream_get_endp(svzclient->ibuf)) < ZEBRA_HEADER_SIZE)
    {
      ssize_t nbyte;
      if (((nbyte = stream_read_try(svzclient->ibuf, svzclient->sock,
				     ZEBRA_HEADER_SIZE-already)) == 0) ||
	  (nbyte == -1))
	{
	  if (svzclient_debug)
	   zlog_debug ("zclient connection closed socket [%d].", svzclient->sock);
	  return svzclient_failed(svzclient);
	}
      if (nbyte != (ssize_t)(ZEBRA_HEADER_SIZE-already))
	{ 
	  /* Try again later. */
	  svzclient_event (SVZCLIENT_READ, svzclient);
	  return 0;
	}
      already = ZEBRA_HEADER_SIZE;
    }

  /* Reset to read from the beginning of the incoming packet. */
  stream_set_getp(svzclient->ibuf, 0); 

  /* Fetch header values. */
  length = stream_getw (svzclient->ibuf);
  marker = stream_getc (svzclient->ibuf);
  version = stream_getc (svzclient->ibuf);
  command = stream_getw (svzclient->ibuf);
  
  if (marker != ZEBRA_HEADER_MARKER || version != ZSERV_VERSION)
    {
      zlog_err("%s: socket %d version mismatch, marker %d, version %d",
               __func__, svzclient->sock, marker, version);
      return svzclient_failed(svzclient);
    }
  
  if (length < ZEBRA_HEADER_SIZE) 
    {
      zlog_err("%s: socket %d message length %u is less than %d ",
	       __func__, svzclient->sock, length, ZEBRA_HEADER_SIZE);
      return svzclient_failed(svzclient);
    }

  /* Length check. */
  if (length > STREAM_SIZE(svzclient->ibuf))
    {
      struct stream *ns;
      zlog_warn("%s: message size %u exceeds buffer size %lu, expanding...",
	        __func__, length, (u_long)STREAM_SIZE(svzclient->ibuf));
      ns = stream_new(length);
      stream_copy(ns, svzclient->ibuf);
      stream_free (svzclient->ibuf);
      svzclient->ibuf = ns;
    }

  /* Read rest of zebra packet. */
  if (already < length)
    {
      ssize_t nbyte;
      if (((nbyte = stream_read_try(svzclient->ibuf, svzclient->sock,
				     length-already)) == 0) ||
	  (nbyte == -1))
	{
	  if (svzclient_debug)
	    zlog_debug("svzclient connection closed socket [%d].", svzclient->sock);
	  return svzclient_failed(svzclient);
	}
      if (nbyte != (ssize_t)(length-already))
	{ 
	  /* Try again later. */
	  svzclient_event (SVZCLIENT_READ, svzclient);
	  return 0;
	}
    }

  length -= ZEBRA_HEADER_SIZE;

  if (svzclient_debug)
    zlog_debug("svzclient 0x%p command 0x%x \n", svzclient, command);

  switch (command)
    {
    case ZEBRA_ROUTER_ID_UPDATE:
      if (svzclient->router_id_update)
	ret = (*svzclient->router_id_update) (command, svzclient, length);
      break;
    case ZEBRA_INTERFACE_ADD:
      if (svzclient->interface_add)
	ret = (*svzclient->interface_add) (command, svzclient, length);
      break;
    case ZEBRA_INTERFACE_DELETE:
      if (svzclient->interface_delete)
	ret = (*svzclient->interface_delete) (command, svzclient, length);
      break;
    case ZEBRA_INTERFACE_ADDRESS_ADD:
      if (svzclient->interface_address_add)
	ret = (*svzclient->interface_address_add) (command, svzclient, length);
      break;
    case ZEBRA_INTERFACE_ADDRESS_DELETE:
      if (svzclient->interface_address_delete)
	ret = (*svzclient->interface_address_delete) (command, svzclient, length);
      break;
    case ZEBRA_INTERFACE_UP:
      if (svzclient->interface_up)
	ret = (*svzclient->interface_up) (command, svzclient, length);
      break;
    case ZEBRA_INTERFACE_DOWN:
      if (svzclient->interface_down)
	ret = (*svzclient->interface_down) (command, svzclient, length);
      break;
    case ZEBRA_IPV4_ROUTE_ADD:
      if (svzclient->ipv4_route_add)
	ret = (*svzclient->ipv4_route_add) (command, svzclient, length);
      break;
    case ZEBRA_IPV4_ROUTE_DELETE:
      if (svzclient->ipv4_route_delete)
	ret = (*svzclient->ipv4_route_delete) (command, svzclient, length);
      break;
    case ZEBRA_IPV6_ROUTE_ADD:
      if (svzclient->ipv6_route_add)
	ret = (*svzclient->ipv6_route_add) (command, svzclient, length);
      break;
    case ZEBRA_IPV6_ROUTE_DELETE:
      if (svzclient->ipv6_route_delete)
	ret = (*svzclient->ipv6_route_delete) (command, svzclient, length);
      break;
    default:
      break;
    }

  if (svzclient->sock < 0)
  /* Connection was closed during packet processing. */
    return -1;

  /* Register read thread. */
  stream_reset(svzclient->ibuf);
  svzclient_event (SVZCLIENT_READ, svzclient);

  return 0;
}
/*
void
zclient_redistribute (int command, struct zclient *zclient, int type)
{

  if (command == ZEBRA_REDISTRIBUTE_ADD) 
    {
      if (zclient->redist[type])
         return;
      zclient->redist[type] = 1;
    }
  else
    {
      if (!zclient->redist[type])
         return;
      zclient->redist[type] = 0;
    }

  if (zclient->sock > 0)
    zebra_redistribute_send (command, zclient, type);
}


void
zclient_redistribute_default (int command, struct zclient *zclient)
{

  if (command == ZEBRA_REDISTRIBUTE_DEFAULT_ADD)
    {
      if (zclient->default_information)
        return;
      zclient->default_information = 1;
    }
  else 
    {
      if (!zclient->default_information)
        return;
      zclient->default_information = 0;
    }

  if (zclient->sock > 0)
    zebra_message_send (zclient, command);
}
*/
static void
svzclient_event (enum event event, struct svzclient *svzclient)
{
  switch (event)
    {
    case SVZCLIENT_SCHEDULE:
      if (! svzclient->t_connect)
	svzclient->t_connect =
	  thread_add_event (master, svzclient_connect, svzclient, 0);
      break;
    case SVZCLIENT_CONNECT:
      if (svzclient->fail >= 10)
	return;
      if (svzclient_debug)
	zlog_debug ("zclient connect schedule interval is %d", 
		   svzclient->fail < 3 ? 10 : 60);
      if (! svzclient->t_connect)
	svzclient->t_connect = 
	  thread_add_timer (master, svzclient_connect, svzclient,
			    svzclient->fail < 3 ? 10 : 60);
      break;
    case SVZCLIENT_READ:
      svzclient->t_read = 
	thread_add_read (master, svzclient_read, svzclient, svzclient->sock);
      break;
    }
}

// Add or delete an IP address from and interface
/*
int zapi_interface_address (u_char cmd, struct zclient *zclient, struct prefix *p, unsigned int ifindex, time_t * expires)
{
  int blen;
  struct stream *s;
*/
  /* Reset stream. */
/*  s = zclient->obuf;
  stream_reset (s);
  
  zclient_create_header (s, cmd);
  
	// Put ifindex
	stream_putl (s, ifindex);
*/	
	/* Prefix information. */
/*	stream_putc (s, p->family);
	blen = prefix_blen (p);
	stream_put (s, &p->u.prefix, blen);
	stream_putc (s, p->prefixlen);
*/	
	/* Put Expiration if needed */
/*	if (expires)
		stream_write (s, (u_char *) expires, sizeof(*expires)); */

  /* Put length at the first point of the stream. */
/*  stream_putw_at (s, 0, stream_get_endp (s));

  return zclient_send_message(zclient);
} */
