/*
 * IS-IS Rout(e)ing protocol - isis_zebra.c   
 *
 * Copyright (C) 2001,2002   Sampo Saaristo
 *                           Tampere University of Technology      
 *                           Institute of Communications Engineering
 *
 * This program is free software; you can redistribute it and/or modify it 
 * under the terms of the GNU General Public Licenseas published by the Free 
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,but WITHOUT 
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or 
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for 
 * more details.

 * You should have received a copy of the GNU General Public License along 
 * with this program; if not, write to the Free Software Foundation, Inc., 
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <zebra.h>

#include "thread.h"
#include "command.h"
#include "memory.h"
#include "log.h"
#include "if.h"
#include "network.h"
#include "prefix.h"
#include "zclient.h"
#include "stream.h"
#include "linklist.h"

#include "isisd/dict.h"
#include "isisd/isis_constants.h"
#include "isisd/isis_common.h"
#include "isisd/isisd.h"
#include "isisd/isis_circuit.h"
#include "isisd/isis_csm.h"
#include "isisd/isis_route.h"
#include "isisd/isis_zebra.h"

struct zclient *zclient = NULL;

extern struct thread_master *master;
extern struct isis *isis;

struct in_addr router_id_zebra;

/* Router-id update message from zebra. */
static int
isis_router_id_update_zebra (int command, struct zclient *zclient,
			     zebra_size_t length)
{
  struct prefix router_id;

  zebra_router_id_update_read (zclient->ibuf,&router_id);
  router_id_zebra = router_id.u.prefix4;

  /* FIXME: Do we react somehow? */
  return 0;
}

static int
isis_zebra_if_add (int command, struct zclient *zclient, zebra_size_t length)
{
  struct interface *ifp;

  ifp = zebra_interface_add_read (zclient->ibuf);

  if (isis->debugs & DEBUG_ZEBRA)
    zlog_debug ("Zebra I/F add: %s index %d flags %ld metric %d mtu %d",
		ifp->name, ifp->ifindex, (long)ifp->flags, ifp->metric, ifp->mtu);

  if (if_is_operative (ifp))
    isis_csm_state_change (IF_UP_FROM_Z, circuit_scan_by_ifp (ifp), ifp);

  return 0;
}

static int
isis_zebra_if_del (int command, struct zclient *zclient, zebra_size_t length)
{
  struct interface *ifp;
  struct stream *s;

  s = zclient->ibuf;
  ifp = zebra_interface_state_read (s);

  if (!ifp)
    return 0;

  if (if_is_operative (ifp))
    zlog_warn ("Zebra: got delete of %s, but interface is still up",
	       ifp->name);

  if (isis->debugs & DEBUG_ZEBRA)
    zlog_debug ("Zebra I/F delete: %s index %d flags %ld metric %d mtu %d",
		ifp->name, ifp->ifindex, (long)ifp->flags, ifp->metric, ifp->mtu);


  /* Cannot call if_delete because we should retain the pseudo interface
     in case there is configuration info attached to it. */
  if_delete_retain(ifp);

  isis_csm_state_change (IF_DOWN_FROM_Z, circuit_scan_by_ifp (ifp), ifp);

  ifp->ifindex = IFINDEX_INTERNAL;

  return 0;
}

static struct interface *
zebra_interface_if_lookup (struct stream *s)
{
  char ifname_tmp[INTERFACE_NAMSIZ];

  /* Read interface name. */
  stream_get (ifname_tmp, s, INTERFACE_NAMSIZ);

  /* And look it up. */
  return if_lookup_by_name_len(ifname_tmp,
			       strnlen(ifname_tmp, INTERFACE_NAMSIZ));
}

static int
isis_zebra_if_state_up (int command, struct zclient *zclient,
			zebra_size_t length)
{
  struct interface *ifp;

  ifp = zebra_interface_if_lookup (zclient->ibuf);

  if (!ifp)
    return 0;

  if (if_is_operative (ifp))
    {
      zebra_interface_if_set_value (zclient->ibuf, ifp);
      /* HT: This is wrong actually. We can't assume that circuit exist
       * if we delete circuit during if_state_down event. Needs rethink.
       * TODO */
      isis_circuit_update_params (circuit_scan_by_ifp (ifp), ifp);
      return 0;
    }

  zebra_interface_if_set_value (zclient->ibuf, ifp);
  isis_csm_state_change (IF_UP_FROM_Z, circuit_scan_by_ifp (ifp), ifp);

  return 0;
}

static int
isis_zebra_if_state_down (int command, struct zclient *zclient,
			  zebra_size_t length)
{
  struct interface *ifp;

  ifp = zebra_interface_if_lookup (zclient->ibuf);

  if (ifp == NULL)
    return 0;

  if (if_is_operative (ifp))
    {
      zebra_interface_if_set_value (zclient->ibuf, ifp);
      isis_csm_state_change (IF_DOWN_FROM_Z, circuit_scan_by_ifp (ifp), ifp);
    }

  return 0;
}

static int
isis_zebra_if_address_add (int command, struct zclient *zclient,
			   zebra_size_t length)
{
  struct connected *c;
  struct prefix *p;
  char buf[BUFSIZ];

  c = zebra_interface_address_read (ZEBRA_INTERFACE_ADDRESS_ADD,
				    zclient->ibuf);

  if (c == NULL)
    return 0;

  p = c->address;

  prefix2str (p, buf, BUFSIZ);
#ifdef EXTREME_DEBUG
  if (p->family == AF_INET)
    zlog_debug ("connected IP address %s", buf);
#ifdef HAVE_IPV6
  if (p->family == AF_INET6)
    zlog_debug ("connected IPv6 address %s", buf);
#endif /* HAVE_IPV6 */
#endif /* EXTREME_DEBUG */
  if (if_is_operative (c->ifp))
    isis_circuit_add_addr (circuit_scan_by_ifp (c->ifp), c);

  return 0;
}

static int
isis_zebra_if_address_del (int command, struct zclient *client,
			   zebra_size_t length)
{
  struct connected *c;
  struct interface *ifp;
#ifdef EXTREME_DEBUG
  struct prefix *p;
  u_char buf[BUFSIZ];
#endif /* EXTREME_DEBUG */

  c = zebra_interface_address_read (ZEBRA_INTERFACE_ADDRESS_DELETE,
				    zclient->ibuf);

  if (c == NULL)
    return 0;

  ifp = c->ifp;

#ifdef EXTREME_DEBUG
  p = c->address;
  prefix2str (p, buf, BUFSIZ);

  if (p->family == AF_INET)
    zlog_debug ("disconnected IP address %s", buf);
#ifdef HAVE_IPV6
  if (p->family == AF_INET6)
    zlog_debug ("disconnected IPv6 address %s", buf);
#endif /* HAVE_IPV6 */
#endif /* EXTREME_DEBUG */

  if (if_is_operative (ifp))
    isis_circuit_del_addr (circuit_scan_by_ifp (ifp), c);
  connected_free (c);

  return 0;
}

static void
isis_zebra_route_add_ipv4 (struct prefix *prefix,
			   struct isis_route_info *route_info)
{
  u_char message, flags;
  int psize;
  struct stream *stream;
  struct isis_nexthop *nexthop;
  struct listnode *node;

  if (CHECK_FLAG (route_info->flag, ISIS_ROUTE_FLAG_ZEBRA_SYNC))
    return;

  if (zclient->redist[ZEBRA_ROUTE_ISIS])
    {
      message = 0;
      flags = 0;

      SET_FLAG (message, ZAPI_MESSAGE_NEXTHOP);
      SET_FLAG (message, ZAPI_MESSAGE_METRIC);
#if 0
      SET_FLAG (message, ZAPI_MESSAGE_DISTANCE);
#endif
			/* Distance value. */
      u_int32_t distance = isis_distance_apply (prefix, route_info);
      if (distance)
        SET_FLAG (message, ZAPI_MESSAGE_DISTANCE);

      stream = zclient->obuf;
      stream_reset (stream);
      zclient_create_header (stream, ZEBRA_IPV4_ROUTE_ADD);
      /* type */
      stream_putc (stream, ZEBRA_ROUTE_ISIS);
      /* flags */
      stream_putc (stream, flags);
      /* message */
      stream_putc (stream, message);
      /* prefix information */
      psize = PSIZE (prefix->prefixlen);
      stream_putc (stream, prefix->prefixlen);
      stream_write (stream, (u_char *) & prefix->u.prefix4, psize);

      stream_putc (stream, listcount (route_info->nexthops));

      /* Nexthop, ifindex, distance and metric information */
      for (ALL_LIST_ELEMENTS_RO (route_info->nexthops, node, nexthop))
	{
	  /* FIXME: can it be ? */
	  if (nexthop->ip.s_addr != INADDR_ANY)
	    {
	      stream_putc (stream, ZEBRA_NEXTHOP_IPV4);
	      stream_put_in_addr (stream, &nexthop->ip);
	    }
	  else
	    {
	      stream_putc (stream, ZEBRA_NEXTHOP_IFINDEX);
	      stream_putl (stream, nexthop->ifindex);
	    }
	}
#if 0
      if (CHECK_FLAG (message, ZAPI_MESSAGE_DISTANCE))
	stream_putc (stream, route_info->depth);
#endif
      if (CHECK_FLAG (message, ZAPI_MESSAGE_METRIC))
	stream_putl (stream, route_info->cost);

      stream_putw_at (stream, 0, stream_get_endp (stream));
      zclient_send_message(zclient);
      SET_FLAG (route_info->flag, ISIS_ROUTE_FLAG_ZEBRA_SYNC);
    }
}

static void
isis_zebra_route_del_ipv4 (struct prefix *prefix,
			   struct isis_route_info *route_info)
{
  struct zapi_ipv4 api;
  struct prefix_ipv4 prefix4;

  if (zclient->redist[ZEBRA_ROUTE_ISIS])
    {
      api.type = ZEBRA_ROUTE_ISIS;
      api.flags = 0;
      api.message = 0;
      prefix4.family = AF_INET;
      prefix4.prefixlen = prefix->prefixlen;
      prefix4.prefix = prefix->u.prefix4;
      zapi_ipv4_route (ZEBRA_IPV4_ROUTE_DELETE, zclient, &prefix4, &api);
    }
  UNSET_FLAG (route_info->flag, ISIS_ROUTE_FLAG_ZEBRA_SYNC);

  return;
}

#ifdef HAVE_IPV6
void
isis_zebra_route_add_ipv6 (struct prefix *prefix,
			   struct isis_route_info *route_info)
{
  struct zapi_ipv6 api;
  struct in6_addr **nexthop_list;
  unsigned int *ifindex_list;
  struct isis_nexthop6 *nexthop6;
  int i, size;
  struct listnode *node;
  struct prefix_ipv6 prefix6;

  if (CHECK_FLAG (route_info->flag, ISIS_ROUTE_FLAG_ZEBRA_SYNC))
    return;

  api.type = ZEBRA_ROUTE_ISIS;
  api.flags = 0;
  api.message = 0;
  SET_FLAG (api.message, ZAPI_MESSAGE_NEXTHOP);
  SET_FLAG (api.message, ZAPI_MESSAGE_IFINDEX);
  SET_FLAG (api.message, ZAPI_MESSAGE_METRIC);
  api.metric = route_info->cost;
#if 0
  SET_FLAG (api.message, ZAPI_MESSAGE_DISTANCE);
  api.distance = route_info->depth;
#endif
  api.nexthop_num = listcount (route_info->nexthops6);
  api.ifindex_num = listcount (route_info->nexthops6);

  /* allocate memory for nexthop_list */
  size = sizeof (struct isis_nexthop6 *) * listcount (route_info->nexthops6);
  nexthop_list = (struct in6_addr **) XMALLOC (MTYPE_ISIS_TMP, size);
  if (!nexthop_list)
    {
      zlog_err ("isis_zebra_add_route_ipv6: out of memory!");
      return;
    }

  /* allocate memory for ifindex_list */
  size = sizeof (unsigned int) * listcount (route_info->nexthops6);
  ifindex_list = (unsigned int *) XMALLOC (MTYPE_ISIS_TMP, size);
  if (!ifindex_list)
    {
      zlog_err ("isis_zebra_add_route_ipv6: out of memory!");
      XFREE (MTYPE_ISIS_TMP, nexthop_list);
      return;
    }

  /* for each nexthop */
  i = 0;
  for (ALL_LIST_ELEMENTS_RO (route_info->nexthops6, node, nexthop6))
    {
      if (!IN6_IS_ADDR_LINKLOCAL (&nexthop6->ip6) &&
	  !IN6_IS_ADDR_UNSPECIFIED (&nexthop6->ip6))
	{
	  api.nexthop_num--;
	  api.ifindex_num--;
	  continue;
	}

      nexthop_list[i] = &nexthop6->ip6;
      ifindex_list[i] = nexthop6->ifindex;
      i++;
    }

  api.nexthop = nexthop_list;
  api.ifindex = ifindex_list;

  if (api.nexthop_num && api.ifindex_num)
    {
      prefix6.family = AF_INET6;
      prefix6.prefixlen = prefix->prefixlen;
      memcpy (&prefix6.prefix, &prefix->u.prefix6, sizeof (struct in6_addr));
      zapi_ipv6_route (ZEBRA_IPV6_ROUTE_ADD, zclient, &prefix6, &api);
      SET_FLAG (route_info->flag, ISIS_ROUTE_FLAG_ZEBRA_SYNC);
    }

  XFREE (MTYPE_ISIS_TMP, nexthop_list);
  XFREE (MTYPE_ISIS_TMP, ifindex_list);

  return;
}

static void
isis_zebra_route_del_ipv6 (struct prefix *prefix,
			   struct isis_route_info *route_info)
{
  struct zapi_ipv6 api;
  struct in6_addr **nexthop_list;
  unsigned int *ifindex_list;
  struct isis_nexthop6 *nexthop6;
  int i, size;
  struct listnode *node;
  struct prefix_ipv6 prefix6;

  if (CHECK_FLAG (route_info->flag, ISIS_ROUTE_FLAG_ZEBRA_SYNC))
    return;

  api.type = ZEBRA_ROUTE_ISIS;
  api.flags = 0;
  api.message = 0;
  SET_FLAG (api.message, ZAPI_MESSAGE_NEXTHOP);
  SET_FLAG (api.message, ZAPI_MESSAGE_IFINDEX);
  api.nexthop_num = listcount (route_info->nexthops6);
  api.ifindex_num = listcount (route_info->nexthops6);

  /* allocate memory for nexthop_list */
  size = sizeof (struct isis_nexthop6 *) * listcount (route_info->nexthops6);
  nexthop_list = (struct in6_addr **) XMALLOC (MTYPE_ISIS_TMP, size);
  if (!nexthop_list)
    {
      zlog_err ("isis_zebra_route_del_ipv6: out of memory!");
      return;
    }

  /* allocate memory for ifindex_list */
  size = sizeof (unsigned int) * listcount (route_info->nexthops6);
  ifindex_list = (unsigned int *) XMALLOC (MTYPE_ISIS_TMP, size);
  if (!ifindex_list)
    {
      zlog_err ("isis_zebra_route_del_ipv6: out of memory!");
      XFREE (MTYPE_ISIS_TMP, nexthop_list);
      return;
    }

  /* for each nexthop */
  i = 0;
  for (ALL_LIST_ELEMENTS_RO (route_info->nexthops6, node, nexthop6))
    {
      if (!IN6_IS_ADDR_LINKLOCAL (&nexthop6->ip6) &&
	  !IN6_IS_ADDR_UNSPECIFIED (&nexthop6->ip6))
	{
	  api.nexthop_num--;
	  api.ifindex_num--;
	  continue;
	}

      nexthop_list[i] = &nexthop6->ip6;
      ifindex_list[i] = nexthop6->ifindex;
      i++;
    }

  api.nexthop = nexthop_list;
  api.ifindex = ifindex_list;

  if (api.nexthop_num && api.ifindex_num)
    {
      prefix6.family = AF_INET6;
      prefix6.prefixlen = prefix->prefixlen;
      memcpy (&prefix6.prefix, &prefix->u.prefix6, sizeof (struct in6_addr));
      zapi_ipv6_route (ZEBRA_IPV6_ROUTE_DELETE, zclient, &prefix6, &api);
      UNSET_FLAG (route_info->flag, ISIS_ROUTE_FLAG_ZEBRA_SYNC);
    }

  XFREE (MTYPE_ISIS_TMP, nexthop_list);
  XFREE (MTYPE_ISIS_TMP, ifindex_list);
}

#endif /* HAVE_IPV6 */

void
isis_zebra_route_update (struct prefix *prefix,
			 struct isis_route_info *route_info)
{
  if (zclient->sock < 0)
    return;

  if (!zclient->redist[ZEBRA_ROUTE_ISIS])
    return;

  if (CHECK_FLAG (route_info->flag, ISIS_ROUTE_FLAG_ACTIVE))
    {
      if (prefix->family == AF_INET)
	isis_zebra_route_add_ipv4 (prefix, route_info);
#ifdef HAVE_IPV6
      else if (prefix->family == AF_INET6)
	isis_zebra_route_add_ipv6 (prefix, route_info);
#endif /* HAVE_IPV6 */
    }
  else
    {
      if (prefix->family == AF_INET)
	isis_zebra_route_del_ipv4 (prefix, route_info);
#ifdef HAVE_IPV6
      else if (prefix->family == AF_INET6)
	isis_zebra_route_del_ipv6 (prefix, route_info);
#endif /* HAVE_IPV6 */
    }
  return;
}

const char * isis_redist_string(u_int route_type)
{
  return (route_type == ZEBRA_ROUTE_MAX) ? "Default" : zebra_route_string(route_type);
}

/* Redistribute route treatment. Adapted from OSPF (ospf_external_info_add). */
void isis_redistribute_add (struct prefix_ipv4 *p, struct in_addr *nexthop, u_int32_t metric, u_char type)
{
	struct isis_external_info *new_info;
	struct route_node *rn;
	
	/* Initialize route table. */
	if (ISIS_EXTERNAL_INFO (type) == NULL)
		ISIS_EXTERNAL_INFO (type) = route_table_init ();
	
	rn = route_node_get (ISIS_EXTERNAL_INFO (type), (struct prefix *) p);
	/* If old info exists, -- discard new one or overwrite with new one? */
	if (rn && rn->info)
	{
		route_unlock_node (rn);
		zlog_warn ("Redistribute[%s]: %s/%d already exists, discard.",
			isis_redist_string(type),
			inet_ntoa (p->prefix), p->prefixlen);
		return rn->info;
	}
	
	/* Create new External info instance. */
	new_info = isis_external_info_new (type);
	new_info->p = *p;
	new_info->metric = metric;
	new_info->nexthop = *nexthop;
	
	rn->info = new_info;
	
	// Debug info
	if (isis->debugs & DEBUG_ZEBRA)
		zlog_debug ("Redistribute[%s]: %s/%d external info created.",
			isis_redist_string(type),
			inet_ntoa (p->prefix), p->prefixlen);
}

/* Redistribute route treatment. Adapted from OSPF (ospf_external_info_delete). */
void isis_redistribute_delete (struct prefix_ipv4 *p, u_char type)
{
  struct route_node *rn;
  rn = route_node_lookup (ISIS_EXTERNAL_INFO (type), (struct prefix *) p);
  if (rn)
	{
		isis_external_info_free (rn->info);
		rn->info = NULL;
		route_unlock_node (rn);
		route_unlock_node (rn);
		
		// Debug info
		if (isis->debugs & DEBUG_ZEBRA)
			zlog_debug ("Redistribute[%s]: %s/%d external info removed.",
				isis_redist_string(type),
				inet_ntoa (p->prefix), p->prefixlen);
	}
	else
	{
		// Debug info
		if (isis->debugs & DEBUG_ZEBRA)
			zlog_debug ("Redistribute[%s]: %s/%d external info could not be found for removal.",
				isis_redist_string(type),
				inet_ntoa (p->prefix), p->prefixlen);
	}
}

/* Add an External info. */
struct isis_external_info * isis_external_info_new (u_char type)
{
  struct isis_external_info *new_info;

  new_info = (struct isis_external_info *)
    XCALLOC (MTYPE_ISIS_EXTERNAL_INFO, sizeof (struct isis_external_info));
  new_info->type = type;
	
  return new_info;
}

/* Free external info. */
static void isis_external_info_free (struct isis_external_info *ei)
{
  XFREE (MTYPE_ISIS_EXTERNAL_INFO, ei);
}

static int
isis_zebra_read_ipv4 (int command, struct zclient *zclient,
		      zebra_size_t length)
{
  struct stream *stream;
  struct zapi_ipv4 api;
  struct prefix_ipv4 p;
  unsigned long ifindex;
  struct in_addr nexthop;

  stream = zclient->ibuf;
  memset (&p, 0, sizeof (struct prefix_ipv4));
  ifindex = 0;

  api.type = stream_getc (stream);
  api.flags = stream_getc (stream);
  api.message = stream_getc (stream);

  p.family = AF_INET;
  p.prefixlen = stream_getc (stream);
  stream_get (&p.prefix, stream, PSIZE (p.prefixlen));

  if (CHECK_FLAG (api.message, ZAPI_MESSAGE_NEXTHOP))
    {
      api.nexthop_num = stream_getc (stream);
      nexthop.s_addr = stream_get_ipv4 (stream);
    }
  if (CHECK_FLAG (api.message, ZAPI_MESSAGE_IFINDEX))
    {
      api.ifindex_num = stream_getc (stream);
      ifindex = stream_getl (stream);
    }
  if (CHECK_FLAG (api.message, ZAPI_MESSAGE_DISTANCE))
    api.distance = stream_getc (stream);
  if (CHECK_FLAG (api.message, ZAPI_MESSAGE_METRIC))
    api.metric = stream_getl (stream);
  else
    api.metric = 0;

  if (command == ZEBRA_IPV4_ROUTE_ADD)
    {
      if (isis->debugs & DEBUG_ZEBRA)
	zlog_debug ("IPv4 Route add from Z");
	
			/*
			if (api.type == ZEBRA_ROUTE_STATIC)
			{
				char tmp[INET_ADDRSTRLEN+1], tmp2[64];
				inet_ntop(AF_INET, &(p.prefix.s_addr), tmp, INET_ADDRSTRLEN+1);
				strcpy(tmp2, "Static Route: ");
				strcat(tmp2, tmp);
				zlog_debug (tmp2);
			}
			*/
			
			// SS: TODO: Should I use api.distance or api.metric as the metric?
			isis_redistribute_add(&p, &nexthop, api.distance, api.type);
    }
	else if (command == ZEBRA_IPV4_ROUTE_DELETE)
	{
		// Delete route
		isis_redistribute_delete(&p, api.type);
	}
	
	// Regenerate LSPs
	struct isis_area *area;
  struct listnode *node;
  if (isis->area_list)
		for (ALL_LIST_ELEMENTS_RO (isis->area_list, node, area))
			lsp_regenerate_schedule(area);
	
  return 0;
}

#ifdef HAVE_IPV6
static int
isis_zebra_read_ipv6 (int command, struct zclient *zclient,
		      zebra_size_t length)
{
  return 0;
}
#endif

#define ISIS_TYPE_IS_REDISTRIBUTED(T) \
T == ZEBRA_ROUTE_MAX ? zclient->default_information : zclient->redist[type]

int
isis_distribute_list_update (int routetype)
{
  return 0;
}

#if 0 /* Not yet. */
static int
isis_redistribute_default_set (int routetype, int metric_type,
			       int metric_value)
{
  return 0;
}
#endif /* 0 */

// SS: Adding isis redistribute connected
// SS: Added for redistribute (Copy from bgp)
/* Utility function to convert user input route type string to route
   type.  */
static int
isis_str2route_type (int afi, const char *str)
{
  if (! str)
    return 0;

  if (afi == AFI_IP)
		{
      if (strncmp (str, "k", 1) == 0)
	return ZEBRA_ROUTE_KERNEL;
      else if (strncmp (str, "c", 1) == 0)
	return ZEBRA_ROUTE_CONNECT;
      else if (strncmp (str, "s", 1) == 0)
	return ZEBRA_ROUTE_STATIC;
      else if (strncmp (str, "r", 1) == 0)
	return ZEBRA_ROUTE_RIP;
      else if (strncmp (str, "o", 1) == 0)
	return ZEBRA_ROUTE_OSPF;
			else if (strncmp (str, "b", 1) == 0)
	return ZEBRA_ROUTE_BGP;
    }
  if (afi == AFI_IP6)
    {
      if (strncmp (str, "k", 1) == 0)
	return ZEBRA_ROUTE_KERNEL;
      else if (strncmp (str, "c", 1) == 0)
	return ZEBRA_ROUTE_CONNECT;
      else if (strncmp (str, "s", 1) == 0)
	return ZEBRA_ROUTE_STATIC;
      else if (strncmp (str, "r", 1) == 0)
	return ZEBRA_ROUTE_RIPNG;
      else if (strncmp (str, "o", 1) == 0)
	return ZEBRA_ROUTE_OSPF6;
    }
  return 0;
}

#if 0
void
isis_zebra_withdraw (struct prefix *p, struct isis_info *info)
{
  int flags;
  struct peer *peer;

  if (zclient->sock < 0)
    return;

  if (! zclient->redist[ZEBRA_ROUTE_ISIS])
    return;

  peer = info->peer;
  flags = 0;

  if (peer_sort (peer) == ISIS_PEER_IBGP)
    {
      SET_FLAG (flags, ZEBRA_FLAG_INTERNAL);
      SET_FLAG (flags, ZEBRA_FLAG_IBGP);
    }

  if ((peer_sort (peer) == ISIS_PEER_EBGP && peer->ttl != 1)
      || CHECK_FLAG (peer->flags, PEER_FLAG_DISABLE_CONNECTED_CHECK))
    SET_FLAG (flags, ZEBRA_FLAG_INTERNAL);

  if (p->family == AF_INET)
    {
      struct zapi_ipv4 api;
      struct in_addr *nexthop;

      api.flags = flags;
      nexthop = &info->attr->nexthop;

      api.type = ZEBRA_ROUTE_BGP;
      api.message = 0;
      SET_FLAG (api.message, ZAPI_MESSAGE_NEXTHOP);
      api.nexthop_num = 1;
      api.nexthop = &nexthop;
      api.ifindex_num = 0;
      SET_FLAG (api.message, ZAPI_MESSAGE_METRIC);
      api.metric = info->attr->med;

      if (isis->debugs & DEBUG_ZEBRA)
	{
	  char buf[2][INET_ADDRSTRLEN];
	  zlog_debug("Zebra send: IPv4 route delete %s/%d nexthop %s metric %u",
		     inet_ntop(AF_INET, &p->u.prefix4, buf[0], sizeof(buf[0])),
		     p->prefixlen,
		     inet_ntop(AF_INET, nexthop, buf[1], sizeof(buf[1])),
		     api.metric);
	}

      zapi_ipv4_route (ZEBRA_IPV4_ROUTE_DELETE, zclient, 
                       (struct prefix_ipv4 *) p, &api);
    }
#ifdef HAVE_IPV6
  /* We have to think about a IPv6 link-local address curse. */
  if (p->family == AF_INET6)
    {
      struct zapi_ipv6 api;
      unsigned int ifindex;
      struct in6_addr *nexthop;
      
      assert (info->attr->extra);
      
      ifindex = 0;
      nexthop = NULL;

      /* Only global address nexthop exists. */
      if (info->attr->extra->mp_nexthop_len == 16)
	nexthop = &info->attr->extra->mp_nexthop_global;

      /* If both global and link-local address present. */
      if (info->attr->extra->mp_nexthop_len == 32)
	{
	  nexthop = &info->attr->extra->mp_nexthop_local;
	  if (info->peer->nexthop.ifp)
	    ifindex = info->peer->nexthop.ifp->ifindex;
	}

      if (nexthop == NULL)
	return;

      if (IN6_IS_ADDR_LINKLOCAL (nexthop) && ! ifindex)
	if (info->peer->ifname)
	  ifindex = if_nametoindex (info->peer->ifname);

      api.flags = flags;
      api.type = ZEBRA_ROUTE_BGP;
      api.message = 0;
      SET_FLAG (api.message, ZAPI_MESSAGE_NEXTHOP);
      api.nexthop_num = 1;
      api.nexthop = &nexthop;
      SET_FLAG (api.message, ZAPI_MESSAGE_IFINDEX);
      api.ifindex_num = 1;
      api.ifindex = &ifindex;
      SET_FLAG (api.message, ZAPI_MESSAGE_METRIC);
      api.metric = info->attr->med;

      if (isis->debugs & DEBUG_ZEBRA)
	{
	  char buf[2][INET6_ADDRSTRLEN];
	  zlog_debug("Zebra send: IPv6 route delete %s/%d nexthop %s metric %u",
		     inet_ntop(AF_INET6, &p->u.prefix6, buf[0], sizeof(buf[0])),
		     p->prefixlen,
		     inet_ntop(AF_INET6, nexthop, buf[1], sizeof(buf[1])),
		     api.metric);
	}

      zapi_ipv6_route (ZEBRA_IPV6_ROUTE_DELETE, zclient, 
                       (struct prefix_ipv6 *) p, &api);
    }
#endif /* HAVE_IPV6 */
}
#endif


/* Other routes redistribution into ISIS. (Copy from bgp) */
int isis_redistribute_set (struct isis_area *area, afi_t afi, int type)
{
  /* Set flag to ISIS instance. */
  area->redist[afi][type] = 1;

  /* Return if already redistribute flag is set. */
  if (zclient->redist[type])
    return CMD_WARNING;

  zclient->redist[type] = 1;

  /* Return if zebra connection is not established. */
  if (zclient->sock < 0)
    return CMD_WARNING;
	
  if (isis->debugs & DEBUG_ZEBRA)
    zlog_debug("Zebra send: redistribute add %s", zebra_route_string(type));
	
  /* Send distribute add message to zebra. */
  zebra_redistribute_send (ZEBRA_REDISTRIBUTE_ADD, zclient, type);
	
	// Regenerate LSPs
	lsp_regenerate_schedule(area);

  return CMD_SUCCESS;
}

/* Redistribute with metric specification.  */
int isis_redistribute_metric_set (struct isis_area *area, afi_t afi, int type, u_int32_t metric)
{
  if (area->redist_metric_flag[afi][type]
      && area->redist_metric[afi][type] == metric)
    return 0;

  area->redist_metric_flag[afi][type] = 1;
  area->redist_metric[afi][type] = metric;

  return 1;
}

/* Unset redistribution.  */
int isis_redistribute_unset (struct isis_area *area, afi_t afi, int type)
{
  /* Unset flag from ISIS instance. */
  area->redist[afi][type] = 0;

  /* Unset route-map. */
	/*
  if (area->rmap[afi][type].name)
    free (area->rmap[afi][type].name);
  area->rmap[afi][type].name = NULL;
  area->rmap[afi][type].map = NULL;
	*/
  /* Unset metric. */
  area->redist_metric_flag[afi][type] = 0;
  area->redist_metric[afi][type] = 0;

  /* Return if zebra connection is disabled. */
  if (! zclient->redist[type])
    return CMD_WARNING;
  zclient->redist[type] = 0;

  if (area->redist[AFI_IP][type] == 0 
      && area->redist[AFI_IP6][type] == 0 
      && zclient->sock >= 0)
    {
      /* Send distribute delete message to zebra. */
			if (isis->debugs & DEBUG_ZEBRA)
				zlog_debug("Zebra send: redistribute delete %s", zebra_route_string(type));
      zebra_redistribute_send (ZEBRA_REDISTRIBUTE_DELETE, zclient, type);
    }
  
  /* Withdraw redistributed routes from current ISIS's routing table. */
  //isis_redistribute_withdraw (area, afi, type);
	// Regenerate LSPs
	lsp_regenerate_schedule(area);

  return CMD_SUCCESS;
}

/* Unset redistribution metric configuration.  */
int isis_redistribute_metric_unset (struct isis_area *area, afi_t afi, int type)
{
  if (! area->redist_metric_flag[afi][type])
    return 0;

  /* Unset metric. */
  area->redist_metric_flag[afi][type] = 0;
  area->redist_metric[afi][type] = 0;

  return 1;
}

DEFUN (isis_redistribute_ipv4,
       isis_redistribute_ipv4_cmd,
       "redistribute (connected|kernel|ospf|rip|bgp|static)",
       "Redistribute information from another routing protocol\n"
       "Connected\n"
       "Kernel routes\n"
       "Open Shurtest Path First (OSPF)\n"
       "Routing Information Protocol (RIP)\n"
			 "Border Gateway Protocol (BGP)\n"
       "Static routes\n")
{
  int type;

  type = isis_str2route_type (AFI_IP, argv[0]);
  if (! type)
    {
      vty_out (vty, "%% Invalid route type%s", VTY_NEWLINE);
      return CMD_WARNING;
    }
	
  return isis_redistribute_set (vty->index, AFI_IP, type);
}

DEFUN (no_isis_redistribute_ipv4,
       no_isis_redistribute_ipv4_cmd,
       "no redistribute (connected|kernel|ospf|rip|bgp|static)",
			 NO_STR
       "Redistribute information from another routing protocol\n"
       "Connected\n"
       "Kernel routes\n"
       "Open Shurtest Path First (OSPF)\n"
       "Routing Information Protocol (RIP)\n"
			 "Border Gateway Protocol (BGP)\n"
       "Static routes\n"
       "Metric for redistributed routes\n"
       "Default metric\n")
{
  int type;

  type = isis_str2route_type (AFI_IP, argv[0]);
  if (! type)
    {
      vty_out (vty, "%% Invalid route type%s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  return isis_redistribute_unset (vty->index, AFI_IP, type);
}

DEFUN (isis_redistribute_ipv4_metric,
       isis_redistribute_ipv4_metric_cmd,
       "redistribute (connected|kernel|ospf|rip|bgp|static) metric <0-4294967295>",
       "Redistribute information from another routing protocol\n"
       "Connected\n"
       "Kernel routes\n"
       "Open Shurtest Path First (OSPF)\n"
       "Routing Information Protocol (RIP)\n"
			 "Border Gateway Protocol (BGP)\n"
       "Static routes\n"
       "Metric for redistributed routes\n"
       "Default metric\n")
{
  int type;
  u_int32_t metric;
	
	// Determine what will be redistributed
  type = isis_str2route_type (AFI_IP, argv[0]);
  if (! type)
    {
      vty_out (vty, "%% Invalid route type%s", VTY_NEWLINE);
      return CMD_WARNING;
    }
	// Get metric
  VTY_GET_INTEGER ("metric", metric, argv[1]);

	isis_redistribute_metric_set (vty->index, AFI_IP, type, metric);
  return isis_redistribute_set (vty->index, AFI_IP, type);
}

DEFUN (no_isis_redistribute_ipv4_metric,
       no_isis_redistribute_ipv4_metric_cmd,
       "no redistribute (connected|kernel|ospf|rip|static) metric <0-4294967295>",
       NO_STR
			 "Redistribute information from another routing protocol\n"
       "Connected\n"
       "Kernel routes\n"
       "Open Shurtest Path First (OSPF)\n"
       "Routing Information Protocol (RIP)\n"
			 "Border Gateway Protocol (BGP)\n"
       "Static routes\n"
       "Metric for redistributed routes\n"
       "Default metric\n")
{
  int type;

  type = isis_str2route_type (AFI_IP, argv[0]);
  if (! type)
    {
      vty_out (vty, "%% Invalid route type%s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  isis_redistribute_metric_unset (vty->index, AFI_IP, type);
  return CMD_SUCCESS;
}

u_char isis_distance_apply (struct prefix *p, struct isis_route_info* route_info)
{
  if (!isis)
    return 0;
	
	if (p->family != AF_INET)
    return 0;
	
	// SS: TODO: Don't know what goes in this function
  return 0;
}

int isis_is_type_redistributed (struct isis_area *area, afi_t afi, int type)
{
  return area->redist[afi][type];
}

void
isis_zebra_init ()
{
  zclient = zclient_new ();
  zclient_init (zclient, ZEBRA_ROUTE_ISIS);
  zclient->router_id_update = isis_router_id_update_zebra;
  zclient->interface_add = isis_zebra_if_add;
  zclient->interface_delete = isis_zebra_if_del;
  zclient->interface_up = isis_zebra_if_state_up;
  zclient->interface_down = isis_zebra_if_state_down;
  zclient->interface_address_add = isis_zebra_if_address_add;
  zclient->interface_address_delete = isis_zebra_if_address_del;
  zclient->ipv4_route_add = isis_zebra_read_ipv4;
  zclient->ipv4_route_delete = isis_zebra_read_ipv4;
#ifdef HAVE_IPV6
  zclient->ipv6_route_add = isis_zebra_read_ipv6;
  zclient->ipv6_route_delete = isis_zebra_read_ipv6;
#endif /* HAVE_IPV6 */

	/* SS: "redistribute" commands.  */
  install_element (ISIS_NODE, &isis_redistribute_ipv4_cmd);
  install_element (ISIS_NODE, &no_isis_redistribute_ipv4_cmd);
  //install_element (ISIS_NODE, &isis_redistribute_ipv4_rmap_cmd);
  //install_element (ISIS_NODE, &no_isis_redistribute_ipv4_rmap_cmd);
  install_element (ISIS_NODE, &isis_redistribute_ipv4_metric_cmd);
  install_element (ISIS_NODE, &no_isis_redistribute_ipv4_metric_cmd);
  //install_element (ISIS_NODE, &isis_redistribute_ipv4_rmap_metric_cmd);
  //install_element (ISIS_NODE, &isis_redistribute_ipv4_metric_rmap_cmd);
  //install_element (ISIS_NODE, &no_isis_redistribute_ipv4_rmap_metric_cmd);
  //install_element (ISIS_NODE, &no_isis_redistribute_ipv4_metric_rmap_cmd);

  return;
}
