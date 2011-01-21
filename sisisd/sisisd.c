/*
 * SIS-IS Rout(e)ing protocol - sisisd.c
 *
 * Copyright (C) 2010,2011   Stephen Sigwart
 *                           University of Delaware
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

#include "prefix.h"
#include "zebra/interface.h"
#include "zclient.h"

#include "thread.h"
#include "sockunion.h"
#include "sockopt.h"
#include "memory.h"
#include "log.h"
#include "if.h"
#include "prefix.h"
#include "command.h"
#include "privs.h"
#include "linklist.h"

#include "sisisd/sisisd.h"
#include "sisisd/sisis_zebra.h"

extern struct zebra_privs_t sisisd_privs;

/* All information about zebra. */
struct zclient *zclient = NULL;

static struct sisis_info sisis_info_real;

/* SIS-IS process wide configuration pointer to export.  */
struct sisis_info *sisis_info;

void sisis_init ()
{
  /* Init zebra. */
  sisis_zebra_init ();
	
	// Start listener
	//zlog_debug("Port: %d; Address:%s\n", sisis_info->port, sisis_info->address);
	sisis_socket(sisis_info->port, sisis_info->address);
	
	/* NOTES:
	  zapi_interface_address(ZEBRA_INTERFACE_ADDRESS_ADD, zclient, (struct prefix_ipv4 *) p, &api);
	  zapi_interface_address(ZEBRA_INTERFACE_ADDRESS_DELETE, zclient, (struct prefix_ipv4 *) p, &api);
	  X zapi_ipv4_route (ZEBRA_IPV4_ROUTE_ADD, zclient, (struct prefix_ipv4 *) p, &api);
	  X zapi_ipv4_route (ZEBRA_IPV4_ROUTE_DELETE, zclient, (struct prefix_ipv4 *) p, &api);
	*/
}

/* time_t value that is monotonicly increasing
 * and uneffected by adjustments to system clock
 */
time_t sisis_clock (void)
{
  struct timeval tv;

  quagga_gettime(QUAGGA_CLK_MONOTONIC, &tv);
  return tv.tv_sec;
}

void sisis_master_init (void)
{
  memset (&sisis_info_real, 0, sizeof (struct sisis_info));

  sisis_info = &sisis_info_real;
  sisis_info->sisis_addrs = list_new();
  sisis_info->listen_sockets = list_new();
  sisis_info->port = SISIS_PORT_DEFAULT;
  sisis_info->master = thread_master_create();
  sisis_info->start_time = sisis_clock();
	sisis_info->address = "127.0.0.1";
}

void sisis_zebra_init (void)
{
  /* Set default values. */
  zclient = zclient_new ();
  zclient_init (zclient, ZEBRA_ROUTE_BGP);
  zclient->router_id_update = NULL;
  zclient->interface_add = NULL;
  zclient->interface_delete = NULL;
  zclient->interface_address_add = NULL;
  zclient->interface_address_delete = NULL;
  zclient->ipv4_route_add = NULL;
  zclient->ipv4_route_delete = NULL;
  zclient->interface_up = NULL;
  zclient->interface_down = NULL;
#ifdef HAVE_IPV6
  zclient->ipv6_route_add = NULL;
  zclient->ipv6_route_delete = NULL;
#endif /* HAVE_IPV6 */

  /* Interface related init. */
  // TODO: if_init ();
}

void sisis_terminate (void)
{
  // TODO
}


// TODO: Add the header file
void sisis_process_message(char * msg, int msg_len, int sock, struct sockaddr * from, socklen_t from_len)
{
	char * ip_addr = "26.0.1.1";
	
	// Get loopback ifindex
	int ifindex = if_nametoindex("lo");
	
	// Set up prefix
	struct prefix_ipv4 p;
	p.family = AF_INET;
	p.prefixlen = stream_getc (s);
	if (inet_pton(AF_INET, ip_addr, &p.prefix.s_addr) != 1)
	{
		zlog_err ("sisis_process_message: Invalid SIS-IS address: %d", ip_addr);
		return;
	}
	
	zapi_interface_address(ZEBRA_INTERFACE_ADDRESS_ADD, zclient, &p, ifindex);
	
	char * reply = "Received message.";
	sendto(sock, reply, strlen(reply), 0, from, from_len); 
}


/* SIS-IS listening socket. */
struct sisis_listener
{
  int fd;
  union sockunion su;
  struct thread *thread;
};

/* Receive a message */
static int sisis_recvfrom(struct thread *thread)
{
	int sisis_sock;
	union sockunion su;
  struct sisis_listener *listener = THREAD_ARG(thread);
	
	sisis_sock = THREAD_FD (thread);
	if (sisis_sock < 0)
	{
		zlog_err ("sisis_sock is negative value %d", sisis_sock);
		return -1;
	}
	
	// Add thread again
	listener->thread = thread_add_read (sisis_info->master, sisis_recvfrom, listener, sisis_sock);
	
	// Get message
	struct sockaddr from;
	memset (&from, 0, sizeof (struct sockaddr));
	char buf[1024];
	memset (buf, 0, 1024);
	int recv_len;
	int from_len = sizeof from;
	recv_len = recvfrom(sisis_sock, buf, 1024, 0, &from, &from_len);
	if (recv_len < 0)
	{
		zlog_err ("Receive length is negative value %d.  Error #%d: %s", recv_len, errno, safe_strerror(errno));
		return -1;
	}
	
	char fromStr[256];
	inet_ntop(from.sa_family, &(from.sa_data), fromStr, sizeof fromStr);
	printf("Message from %s[%d]: %s\n", fromStr, recv_len, buf);
	
	// Process message
	sisis_process_message(buf, recv_len, sisis_sock, &from, from_len);
}

// Create SIS-IS listener from existing socket
static int sisis_listener (int sock, struct sockaddr *sa, socklen_t salen)
{
  struct sisis_listener *listener;
  int ret, en;

  sockopt_reuseaddr (sock);
  sockopt_reuseport (sock);

#ifdef IPTOS_PREC_INTERNETCONTROL
  if (sa->sa_family == AF_INET)
    setsockopt_ipv4_tos (sock, IPTOS_PREC_INTERNETCONTROL);
#endif

#ifdef IPV6_V6ONLY
  /* Want only IPV6 on ipv6 socket (not mapped addresses) */
  if (sa->sa_family == AF_INET6) {
    int on = 1;
    setsockopt (sock, IPPROTO_IPV6, IPV6_V6ONLY,
		(void *) &on, sizeof (on));
  }
#endif

  if (sisisd_privs.change (ZPRIVS_RAISE) )
    zlog_err ("sisis_socket: could not raise privs");

	// Bind
  ret = bind (sock, sa, salen);
  en = errno;
  if (sisisd_privs.change (ZPRIVS_LOWER) )
    zlog_err ("sisis_bind_address: could not lower privs");

  if (ret < 0)
	{
		zlog_err ("bind: %s", safe_strerror (en));
		return ret;
	}
	
	// Update listener infprmation
  listener = XMALLOC (MTYPE_SISIS_LISTENER, sizeof(*listener));
  listener->fd = sock;
  memcpy(&listener->su, sa, salen);
  listener->thread = thread_add_read (sisis_info->master, sisis_recvfrom, listener, sock);
  listnode_add (sisis_info->listen_sockets, listener);
/*
	// Start listening
  ret = listen (sock, 3);
  if (ret < 0)
	{
		zlog_err ("listen: %s", safe_strerror (errno));
		return ret;
	}

	// Update listener infprmation
  listener = XMALLOC (MTYPE_SISIS_LISTENER, sizeof(*listener));
  listener->fd = sock;
  memcpy(&listener->su, sa, salen);
  listener->thread = thread_add_read (master, sisis_accept, listener, sock);
  listnode_add (bm->listen_sockets, listener);
*/
  return 0;
}

// Open SIS-IS socket
int sisis_socket (unsigned short port, const char *address)
{
  int sock;
  int socklen;
  struct sockaddr_in sin;
  int ret, en;
	
	// Open socket
  sock = socket (AF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
	{
		zlog_err ("socket: %s", safe_strerror (errno));
		return sock;
	}
	
	// Set address and port
  memset (&sin, 0, sizeof (struct sockaddr_in));
  sin.sin_family = AF_INET;
  sin.sin_port = htons (port);
  socklen = sizeof (struct sockaddr_in);

  if (address && ((ret = inet_aton(address, &sin.sin_addr)) < 1))
	{
		zlog_err("bgp_socket: could not parse ip address %s: %s",
							address, safe_strerror (errno));
		return ret;
	}
#ifdef HAVE_STRUCT_SOCKADDR_IN_SIN_LEN
  sin.sin_len = socklen;
#endif /* HAVE_STRUCT_SOCKADDR_IN_SIN_LEN */

	// Create listener
  ret = sisis_listener (sock, (struct sockaddr *) &sin, socklen);
  if (ret < 0) 
	{
		close (sock);
		return ret;
	}
  return sock;
}



#if 0
// TODO: Remove
/* Accept SIS-IS connection. */
static int sisis_accept (struct thread *thread)
{
  int sisis_sock;
  int accept_sock;
  union sockunion su;
  struct sisis_listener *listener = THREAD_ARG(thread);
  struct peer *peer;
  struct peer *peer1;
  char buf[SU_ADDRSTRLEN];

  /* Register accept thread. */
  accept_sock = THREAD_FD (thread);
  if (accept_sock < 0)
	{
		zlog_err ("accept_sock is negative value %d", accept_sock);
		return -1;
	}
  listener->thread = thread_add_read (master, bgp_accept, listener, accept_sock);

  /* Accept client connection. */
  bgp_sock = sockunion_accept (accept_sock, &su);
  if (bgp_sock < 0)
    {
      zlog_err ("[Error] BGP socket accept failed (%s)", safe_strerror (errno));
      return -1;
    }

  if (BGP_DEBUG (events, EVENTS))
    zlog_debug ("[Event] BGP connection from host %s", inet_sutop (&su, buf));
  
  /* Check remote IP address */
  peer1 = peer_lookup (NULL, &su);
  if (! peer1 || peer1->status == Idle)
    {
      if (BGP_DEBUG (events, EVENTS))
	{
	  if (! peer1)
	    zlog_debug ("[Event] BGP connection IP address %s is not configured",
		       inet_sutop (&su, buf));
	  else
	    zlog_debug ("[Event] BGP connection IP address %s is Idle state",
		       inet_sutop (&su, buf));
	}
      close (bgp_sock);
      return -1;
    }

  /* In case of peer is EBGP, we should set TTL for this connection.  */
  if (peer_sort (peer1) == BGP_PEER_EBGP)
    sockopt_ttl (peer1->su.sa.sa_family, bgp_sock, peer1->ttl);

  /* Make dummy peer until read Open packet. */
  if (BGP_DEBUG (events, EVENTS))
    zlog_debug ("[Event] Make dummy peer structure until read Open packet");

  {
    char buf[SU_ADDRSTRLEN + 1];

    peer = peer_create_accept (peer1->bgp);
    SET_FLAG (peer->sflags, PEER_STATUS_ACCEPT_PEER);
    peer->su = su;
    peer->fd = bgp_sock;
    peer->status = Active;
    peer->local_id = peer1->local_id;
    peer->v_holdtime = peer1->v_holdtime;
    peer->v_keepalive = peer1->v_keepalive;

    /* Make peer's address string. */
    sockunion2str (&su, buf, SU_ADDRSTRLEN);
    peer->host = XSTRDUP (MTYPE_BGP_PEER_HOST, buf);
  }

  BGP_EVENT_ADD (peer, TCP_connection_open);

  return 0;
}
#endif