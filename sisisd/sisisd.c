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
}

void sisis_terminate (void)
{
  // TODO
}

// Similar function in sisis_api.c
void sisis_process_message(char * msg, int msg_len, int sock, struct sockaddr * from, socklen_t from_len)
{
	// Get message version
	unsigned short version = 0;
	if (msg_len >= 2)
		version = ntohs(*(unsigned short *)msg);
	printf("Message:\n");
	printf("\tVersion: %u\n", version);
	if (version == 1)
	{
		// Get request id
		unsigned int request_id = 0;
		if (msg_len >= 6)
			request_id = ntohl(*(unsigned int *)(msg+2));
		printf("\tRequest Id: %u\n", request_id);
		
		// Get command
		unsigned short command = -1;
		if (msg_len >= 8)
			command = ntohs(*(unsigned short *)(msg+6));
		printf("\tCommand: %u\n", command);
		switch (command)
		{
			case SISIS_CMD_REGISTER_ADDRESS:
			case SISIS_CMD_UNREGISTER_ADDRESS:
				{
					if (msg_len >= 12)
					{
						// Set up prefix
						struct prefix p;
						p.family = ntohs(*(unsigned short *)(msg+8));
						p.prefixlen = 128;
						
						// Get address
						short len = ntohs(*(unsigned short *)(msg+10));
						char ip_addr[64];
						memset(ip_addr, 0, 64);
						if (len >= 64 || msg_len > msg-12)
							printf("Invalid IP address length: %hd\n", len);
						else
						{
							memcpy(ip_addr, msg+12, len);
							printf("\tIP Address: %s\n", ip_addr);
							
							// Set expiration
							time_t expires = time(NULL) + SISIS_ADDRESS_TIMEOUT;
							
							// Get loopback ifindex
							int ifindex = if_nametoindex("lo");
							
							// Set up prefix
							if (inet_pton(p.family, ip_addr, &p.u.prefix) != 1)
							{
								// Construct reply
								char * buf;
								int buf_len = sisis_construct_message(&buf, SISIS_MESSAGE_VERSION, request_id, SISIS_NACK, NULL, 0);
								sendto(sock, buf, buf_len, 0, from, from_len);
								free(buf);
								
								zlog_err ("sisis_process_message: Invalid SIS-IS address: %s", ip_addr);
								return;
							}
							
							int zcmd = (command == SISIS_CMD_REGISTER_ADDRESS) ? ZEBRA_INTERFACE_ADDRESS_ADD : ZEBRA_INTERFACE_ADDRESS_DELETE;
							int status = zapi_interface_address(zcmd, zclient, &p, ifindex, &expires);
							
							// Construct reply
							char * buf;
							int buf_len = sisis_construct_message(&buf, SISIS_MESSAGE_VERSION, request_id, (status == 0) ? SISIS_ACK : SISIS_NACK, NULL, 0);
							printf("\tSending %s\n", (status == 0) ? "ACK" : "NACK");
							sendto(sock, buf, buf_len, 0, from, from_len);
							free(buf);
						}
					}
				}
				break;
		}
	}
}

/**
 * Constructs SIS-IS message.  Remember to free memory when done.
 * Duplicated in sisis_api.c
 * Returns length of message.
 */
int sisis_construct_message(char ** buf, unsigned short version, unsigned int request_id, unsigned short cmd, void * data, unsigned int data_len)
{
	unsigned int buf_len = data_len + 8;
	*buf = malloc(sizeof(char) * buf_len);
	version = htons(version);
	request_id = htonl(request_id);
	cmd = htons(cmd);
	memcpy(*buf, &version, 2);
	memcpy(*buf+2, &request_id, 4);
	memcpy(*buf+6, &cmd, 2);
	memcpy(*buf+8, data, data_len);
	return buf_len;
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
	unsigned int from_len = sizeof from;
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
	sisis_process_message(buf, recv_len, sisis_sock, &from, recv_len);
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
  int ret;
	
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