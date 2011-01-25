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
	
	// Init kernel communication
	sisis_kernel_init();
	
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
	// Get message version
	unsigned short version = -1;
	if (msg_len >= 2)
		version = ntohs(*(unsigned short *)msg);
	printf("Message:\n");
	printf("\tVersion: %u\n", version);
	if (version == 1)
	{
		// Get command
		unsigned short command = -1;
		if (msg_len >= 4)
			command = ntohs(*(unsigned short *)(msg+2));
		printf("\tCommand: %u\n", command);
		switch (command)
		{
			case SISIS_CMD_REGISTER_ADDRESS:
			case SISIS_CMD_UNREGISTER_ADDRESS:
				{
					char ip_addr[INET_ADDRSTRLEN];
					memcpy(ip_addr, msg+4, from_len-4);
					
					// Get loopback ifindex
					int ifindex = if_nametoindex("lo");
					
					// Set up prefix
					struct prefix_ipv4 p;
					p.family = AF_INET;
					p.prefixlen = 32;
					if (inet_pton(AF_INET, ip_addr, &p.prefix.s_addr) != 1)
					{
						zlog_err ("sisis_process_message: Invalid SIS-IS address: %s", ip_addr);
						return;
					}
					
					int zcmd = (command == SISIS_CMD_REGISTER_ADDRESS) ? ZEBRA_INTERFACE_ADDRESS_ADD : ZEBRA_INTERFACE_ADDRESS_DELETE;
					zapi_interface_address(zcmd, zclient, &p, ifindex);
					
					// TODO: Change reply
					char reply[256];
					sprintf(reply, "%s SIS-IS address: %s.\n", (zcmd == ZEBRA_INTERFACE_ADDRESS_ADD) ? "Added " : "Removed ", ip_addr);
					sendto(sock, reply, strlen(reply), 0, from, from_len);
				}
				break;
			case SISIS_CMD_DUMP_ROUTES:
				printf("Dumping Kernel Routes:\n");
				switch(sisis_netlink_route_read())
				{
					case 0:
						printf("Done dumping kernel routes.\n");
						break;
					default:
						printf("Error dumping kernel routes.\n");
				}
				break;
		}
	}
}

/* Add an IPv4 Address to RIB. */
int sisis_rib_add_ipv4 (int type, int flags, struct prefix_ipv4 *p, 
	      struct in_addr *gate, struct in_addr *src,
	      unsigned int ifindex, u_int32_t vrf_id,
	      u_int32_t metric, u_char distance)
{
	// Set up prefix
	char prefix_str[INET_ADDRSTRLEN];
	p->family = AF_INET;
	p->prefixlen = 32;
	if (inet_ntop(AF_INET, &(p->prefix.s_addr), prefix_str, INET_ADDRSTRLEN) != 1)
		printf("%s/%d [%u/%u]\n", prefix_str, p->prefixlen, distance, metric);
	return 0;
}

// TODO
int sisis_rib_add_ipv6 (int type, int flags, struct prefix_ipv6 *p,
	      struct in6_addr *gate, unsigned int ifindex, u_int32_t vrf_id,
	      u_int32_t metric, u_char distance)
{
	return 0;
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
	//printf("Message from %s[%d]: %s\n", fromStr, recv_len, buf);
	
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