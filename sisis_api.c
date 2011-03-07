/*
 * SIS-IS Test program.
 * Stephen Sigwart
 * University of Delaware
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <stdint.h>

#include "sisis_api.h"
#include "sisis_structs.h"
#include "sisis_netlink.h"

int socket_opened = 0;
int sisis_socket = 0;
struct sockaddr_in sisis_listener_addr;

int sisis_listener_port = 54345;
char * sisis_listener_ip_addr = "127.0.0.1";
unsigned int next_request_id = 1;

// IPv4/IPv6 Ribs
struct list * ipv4_rib_routes = NULL;
#ifdef HAVE_IPV6
struct list * ipv6_rib_routes = NULL;
#endif /* HAVE_IPV6 */

// TODO: Support multiple addresses at once.
pthread_t sisis_reregistration_thread;

// Listen for messages
pthread_t sisis_recv_from_thread;
void * sisis_recv_loop(void *);

// TODO: Handle multiple outstanding requests later
// Request which we are waiting for an ACK or NACK for
struct sisis_request_ack_info awaiting_ack;

/**
 * Sets up socket to SIS-IS listener.
 */
int sisis_socket_open()
{
	if (socket_opened)
		return 0;
	
	// Open socket
	if ((sisis_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		return 1;

	// Set up address info
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));	// Clear structure
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY); // Any host interface
	addr.sin_port = htons(0);	// Any port

	// Bind client socket
	if (bind(sisis_socket, (struct sockaddr *) &addr, sizeof (addr)) < 0)
		return 1;
	
	/* Not needed anymore
	// Set timeout
	struct timeval tv;
	tv.tv_sec = 5;
	setsockopt(sisis_socket, SOL_SOCKET, SO_RCVTIMEO,(struct timeval *)&tv,sizeof(struct timeval));
	*/
	
	// Set up SIS-IS listener address structure
	memset(&sisis_listener_addr, 0, sizeof(sisis_listener_addr));
	sisis_listener_addr.sin_family = AF_INET;
	inet_pton(AF_INET, sisis_listener_ip_addr, &(sisis_listener_addr.sin_addr));
	sisis_listener_addr.sin_port = htons((in_port_t) sisis_listener_port);
	
	// Listen for messages
	pthread_create(&sisis_recv_from_thread, NULL, sisis_recv_loop, NULL);
	
	// Make that the socket was opened
	socket_opened = 1;
	return 0;
}

/**
 * Send a message to SIS-IS listener.
 */
int sisis_send(char * buf, unsigned int buf_len)
{
	unsigned int rtn = -1;
	if (sisis_socket)
		rtn = sendto(sisis_socket, buf, buf_len, 0, (struct sockaddr *) &sisis_listener_addr, sizeof (sisis_listener_addr));
	return rtn;
}

/**
 * Receive messages from the SIS-IS listener.
 */
void * sisis_recv_loop(void * null)
{
	char buf[1024];
	int buf_len = 0;
	while (1)
	{
		buf_len = sisis_recv(buf, 1024);
		sisis_process_message(buf, buf_len);
	}
}

// Similar function in sisisd.c
void sisis_process_message(char * msg, int msg_len)
{
	// Get message version
	unsigned short version = 0;
	if (msg_len >= 2)
		version = ntohs(*(unsigned short *)msg);
	if (version == 1)
	{
		// Get request id
		unsigned int request_id = 0;
		if (msg_len >= 6)
			request_id = ntohl(*(unsigned int *)(msg+2));
		
		// Get command
		unsigned short command = -1;
		if (msg_len >= 8)
			command = ntohs(*(unsigned short *)(msg+6));
		
		switch (command)
		{
			case SISIS_ACK:
				if (awaiting_ack.request_id == request_id)
					awaiting_ack.flags |= SISIS_REQUEST_ACK_INFO_ACKED;
				
				// Free mutex
				if (awaiting_ack.mutex)
					pthread_mutex_unlock(awaiting_ack.mutex);
				break;
			case SISIS_NACK:
				if (awaiting_ack.request_id == request_id)
					awaiting_ack.flags |= SISIS_REQUEST_ACK_INFO_NACKED;
				
				// Free mutex
				if (awaiting_ack.mutex)
					pthread_mutex_unlock(awaiting_ack.mutex);
				
				break;
		}
	}
}

/**
 * Receive a message from the SIS-IS listener.
 */
int sisis_recv(char * buf, unsigned int buf_len)
{
	// Set up address info
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));	// Clear structure
	int addr_len = sizeof(addr);
	
	unsigned int rtn = -1;
	if (sisis_socket)
	{
		do
		{
			rtn = -1;
			rtn = recvfrom(sisis_socket, buf, buf_len, 0, (struct sockaddr *) &addr, &addr_len);
		}while (addr.sin_family != sisis_listener_addr.sin_family || addr.sin_addr.s_addr != sisis_listener_addr.sin_addr.s_addr || addr.sin_port != sisis_listener_addr.sin_port);
	}
	return rtn;
}

/**
 * Constructs SIS-IS message.  Remember to free memory when done.
 * Duplicated in sisisd.c
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

/**
 * Construct SIS-IS address.
 *
 * sisis_addr String to store resulting SIS-IS/IP address in.
 * 
 * Returns zero on success.
 */
int sisis_create_addr(uint16_t ptype, uint32_t host_num, uint64_t pid, char * sisis_addr)
{
	// Construct SIS-IS address
	sprintf(sisis_addr, "fcff:%04hx:%04hx:%04hx:%04hx:%04hx:%04hx:%04hx", (unsigned short)ptype & 0xffff, (unsigned short)(host_num >> 16) & 0xffff, (unsigned short)host_num & 0xffff, (unsigned short)(pid >> 48) & 0xffff, (unsigned short)(pid >> 32) & 0xffff, (unsigned short)(pid >> 16) & 0xffff, (unsigned short)pid & 0xffff);
	
	return 0;
}

/**
 * Split an SIS-IS address into components.
 *
 * sisis_addr SIS-IS/IP address
 */
struct sisis_addr_components get_sisis_addr_components(char * sisis_addr)
{
	struct sisis_addr_components rtn;
	
	// Remove ::
	char before[INET6_ADDRSTRLEN], after[INET6_ADDRSTRLEN], full[INET6_ADDRSTRLEN] = "";
	if (!sscanf(sisis_addr, "%s::%s", before, after))
		strcpy(full, sisis_addr);
	else
	{
		// Count colons
		int cnt = 0, i;
		for (i = strlen(before) - 1; i >= 0; i--)
			if (before[i] == ':')
				cnt++;
		for (i = strlen(after) - 1; i >= 0; i--)
			if (after[i] == ':')
				cnt++;
		
		// Create new string
		strcat(full, before);
		strcat(full, ":");
		cnt++;
		for (; cnt < 7; cnt++)
			strcat(full, "0:");
		strcat(full, after);
	}
	
	sscanf(full, "fcff:%hx:%hx:%hx:%hx:%hx:%hx:%hx", (short *)&rtn.ptype, (short *)&rtn.host_num, (short *)&rtn.host_num + 2, (short *)&rtn.pid, (short *)&rtn.pid + 2, (short *)&rtn.pid + 4, (short *)&rtn.pid + 3);
	return rtn;
}

/**
 * Does actual registration of SIS-IS address.
 *
 * sisis_addr String to store resulting SIS-IS/IP address in.
 * 
 * Returns zero on success.
 */
int sisis_do_register(char * sisis_addr)
{
	// Setup socket
	sisis_socket_open();
	
	// Get request id
	unsigned int request_id = next_request_id++;
	
	// Setup and lock mutex
	pthread_mutex_t * mutex = malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(mutex, NULL);
	pthread_mutex_lock(mutex);
	
	// Fill in the awaiting ack info
	awaiting_ack.request_id = request_id;
	awaiting_ack.mutex = mutex;
	awaiting_ack.flags = 0;
	
	// Setup message
	char msg[128];
	unsigned short tmp = htons(AF_INET6);
	memcpy(msg, &tmp, 2);
	tmp = htons(strlen(sisis_addr));
	memcpy(msg+2, &tmp, 2);
	memcpy(msg+4, sisis_addr, strlen(sisis_addr));
	
	// Send message
	char * buf;
	unsigned int buf_len = sisis_construct_message(&buf, SISIS_VERSION, request_id, SISIS_CMD_REGISTER_ADDRESS, msg, strlen(sisis_addr)+4);
	sisis_send(buf, buf_len);
	free(buf);
	
	// Wait for ack, nack, or timeout
	struct timespec timeout;
  clock_gettime(CLOCK_REALTIME, &timeout);
	timeout.tv_sec += 5;
  int status = pthread_mutex_timedlock(mutex, &timeout);
	if (status != 0)
		return 1;
	
	// Remove mutex
	pthread_mutex_destroy(mutex);
	free(mutex);
	
	// Check if it was an ack of nack
	return (awaiting_ack.request_id == request_id && (awaiting_ack.flags & SISIS_REQUEST_ACK_INFO_ACKED)) ? 0 : 1;
}

void * sisis_reregister(void * arg)
{
	char * addr = (char *)arg;
	do
	{
		// Sleep
		sleep(SISIS_REREGISTRATION_TIMEOUT);
		
		// Register
		sisis_do_register(addr);
	} while (1);	// TODO: Stop when the address is deregistered
}

/**
 * Registers SIS-IS process.
 *
 * sisis_addr String to store resulting SIS-IS/IP address in.
 * 
 * Returns zero on success.
 */
int sisis_register(uint16_t ptype, uint32_t host_num, uint64_t pid, char * sisis_addr)
{
	// Construct SIS-IS address
	if (sisis_create_addr(ptype, host_num, pid, sisis_addr))
		return 1;
	
	// Register
	int rtn = sisis_do_register(sisis_addr);
	
	// TODO: Support multiple addresses at once.
	char * thread_sisis_addr = malloc(sizeof(char) * (strlen(sisis_addr)+1));
	strcpy(thread_sisis_addr, sisis_addr);
	pthread_create(&sisis_reregistration_thread, NULL, sisis_reregister, (void *)thread_sisis_addr);
	
	return rtn;
}

/**
 * Unregisters SIS-IS process.
 * Returns zero on success.
 */
int sisis_unregister(uint16_t ptype, uint32_t host_num, uint64_t pid)
{
	// Construct SIS-IS address
	char sisis_addr[INET6_ADDRSTRLEN+1];
	if (sisis_create_addr(ptype, host_num, pid, sisis_addr))
		return 1;
	
	// Setup socket
	sisis_socket_open();
	
	// Get request id
	unsigned int request_id = next_request_id++;
	
	// Setup message
	char msg[128];
	unsigned short tmp = htons(AF_INET6);
	memcpy(msg, &tmp, 2);
	tmp = htons(strlen(sisis_addr));
	memcpy(msg+2, &tmp, 2);
	memcpy(msg+4, sisis_addr, strlen(sisis_addr));
	
	// Send message
	char * buf;
	unsigned int buf_len = sisis_construct_message(&buf, SISIS_VERSION, request_id, SISIS_CMD_UNREGISTER_ADDRESS, msg, strlen(sisis_addr)+4);
	sisis_send(buf, buf_len);
	free(buf);
	
	return 0;
}

/**
 * Dump kernel routing table.
 * Returns zero on success.
 */
int sisis_dump_kernel_routes()
{
	// Set up list of IPv4 rib entries
	if (ipv4_rib_routes)
		FREE_LINKED_LIST(ipv4_rib_routes);
	ipv4_rib_routes = malloc(sizeof(struct list));
	memset(ipv4_rib_routes, 0, sizeof(*ipv4_rib_routes));

#ifdef HAVE_IPV6
	// Set up list of IPv6 rib entries
	if (ipv6_rib_routes)
		FREE_LINKED_LIST(ipv6_rib_routes);
	ipv6_rib_routes = malloc(sizeof(struct list));
	memset(ipv6_rib_routes, 0, sizeof(*ipv6_rib_routes));
#endif /* HAVE_IPV6 */

	// Set up callbacks
	struct sisis_netlink_routing_table_info info;
	memset(&info, 0, sizeof(info));
	info.rib_add_ipv4_route = sisis_rib_add_ipv4;
	#ifdef HAVE_IPV6
	info.rib_add_ipv6_route = sisis_rib_add_ipv6;
	#endif /* HAVE_IPV6 */
	
	// Get routes
	sisis_netlink_route_read(&info);
	
	return 0;
}

/* Add an IPv4 Address to RIB. */
int sisis_rib_add_ipv4 (struct route_ipv4 * route)
{
	struct listnode * node = malloc(sizeof(struct listnode));
	node->data = (void *)route;
	LIST_APPEND(ipv4_rib_routes,node);
	
	/*
	// Set up prefix
	char prefix_str[INET_ADDRSTRLEN];
	if (inet_ntop(AF_INET, &(route->p->prefix.s_addr), prefix_str, INET_ADDRSTRLEN) != 1)
		printf("%s/%d [%u/%u]\n", prefix_str, route->p->prefixlen, route->distance, route->metric);
	*/
	
	return 0;
}

#ifdef HAVE_IPV6
int sisis_rib_add_ipv6 (struct route_ipv6 * route)
{
	struct listnode * node = malloc(sizeof(struct listnode));
	node->data = (void *)route;
	LIST_APPEND(ipv6_rib_routes,node);
	
	return 0;
}
#endif /* HAVE_IPV6 */

/** Subscribe to route add/remove messages */
int subscribe_to_rib_changes(struct subscribe_to_rib_changes_info * info)
{
	int rtn = 0;
	
	// Set up callbacks
	struct sisis_netlink_routing_table_info * subscribe_info = malloc(sizeof(struct sisis_netlink_routing_table_info));
	memset(subscribe_info, 0, sizeof(*subscribe_info));
	subscribe_info->rib_add_ipv4_route = info->rib_add_ipv4_route;
	subscribe_info->rib_remove_ipv4_route = info->rib_remove_ipv4_route;
	#ifdef HAVE_IPV6
	subscribe_info->rib_add_ipv6_route = info->rib_add_ipv6_route;
	subscribe_info->rib_remove_ipv6_route = info->rib_remove_ipv6_route;
	#endif /* HAVE_IPV6 */
	
	// Subscribe to changes
	sisis_netlink_subscribe_to_rib_changes(subscribe_info);
	
	return rtn;
}

/**
 * Get SIS-IS addresses that match a given ipv4 prefix.  It is the receivers
 * responsibility to free the list when done with it.
 */
struct list * get_sisis_addrs_for_prefix(struct prefix_ipv6 * p)
{
	// Update kernel routes
	sisis_dump_kernel_routes();
	
	/*
	// IPv4 Version
	unsigned long prefix_mask = 0xffffffff;
	int i = 0;
	for (; i < 32-p->prefixlen; i++)
	{
		prefix_mask <<= 1;
		prefix_mask |= 0;
	}
	prefix_mask = htonl(prefix_mask);
	
	// Create list of relevant SIS-IS addresses
	struct list * rtn = malloc(sizeof(struct list));
	memset(rtn, 0, sizeof(*rtn));
	struct listnode * node;
	LIST_FOREACH(ipv4_rib_routes, node)
	{
		struct route_ipv4 * route = (struct route_ipv4 *)node->data;
		
		// Check if the route matches the prefix
		if (route->p->prefixlen == 32 && (route->p->prefix.s_addr & prefix_mask) == (p->prefix.s_addr & prefix_mask))
		{
			// Add to list
			struct listnode * new_node = malloc(sizeof(struct listnode));
			new_node->data = malloc(sizeof(route->p->prefix));
			memcpy(new_node->data, &route->p->prefix, sizeof(route->p->prefix));
			LIST_APPEND(rtn,new_node);
		}
	}
	*/
	
	// TODO: IPv6 version
	// Create prefix mask IPv6 addr
	char prefix_addr_str[INET6_ADDRSTRLEN+1] = "";
	int i;
	if (p->prefixlen / 16 > 0)
		strcat(prefix_addr_str, "ffff");
	for (i = 1; i < (p->prefixlen / 16); i++)
		strcat(prefix_addr_str, ":ffff");
	int tmp = 0;
	for (; i < p->prefixlen % 16; i++)
	{
		tmp <<= 1;
		tmp |= 1;
	}
	if (tmp != 0)
	{
		char tmp_str[8];
		sprintf(tmp_str, "%s%04x", (p->prefixlen > 16) ? ":" : "", tmp);
		strcat(prefix_addr_str, tmp_str);
	}
	// Finish off
	if (p->prefixlen <= 96)
		strcat(prefix_addr_str, "::");
	else if (p->prefixlen <= 112)
		strcat(prefix_addr_str, ":0");
	// Create struct
	struct in6_addr prefix_mask;
	inet_pton(AF_INET6, prefix_addr_str, &prefix_mask);
	
	// Create list of relevant SIS-IS addresses
	struct list * rtn = malloc(sizeof(struct list));
	memset(rtn, 0, sizeof(*rtn));
	struct listnode * node;
	LIST_FOREACH(ipv4_rib_routes, node)
	{
		struct route_ipv6 * route = (struct route_ipv6 *)node->data;
		
		int match = 1;
		for (i = 0; match && i < 16; i++)
			match = ((route->p->prefix.s6_addr[i] & prefix_mask.s6_addr[i]) == (p->prefix.s6_addr[i] & prefix_mask.s6_addr[i]));
		
		// Check if the route matches the prefix
		if (route->p->prefixlen == 128 && match)
		{
			// Add to list
			struct listnode * new_node = malloc(sizeof(struct listnode));
			new_node->data = malloc(sizeof(route->p->prefix));
			memcpy(new_node->data, &route->p->prefix, sizeof(route->p->prefix));
			LIST_APPEND(rtn,new_node);
		}
	}
	
	return rtn;
}

/**
 * Get SIS-IS addresses for a specific process type.  It is the receivers
 * responsibility to free the list when done with it.
 */
struct list * get_sisis_addrs_for_process_type(uint16_t ptype)
{
	// Create prefix
	char prefix_addr_str[INET6_ADDRSTRLEN+1];
	memset(prefix_addr_str, 0, sizeof(prefix_addr_str));
	if (sisis_create_addr(ptype, 0, 0, prefix_addr_str))
		return NULL;
	struct prefix_ipv6 p;
	p.prefixlen = SISIS_ADD_PREFIX_LEN_PTYPE;
	inet_pton(AF_INET6, prefix_addr_str, &p.prefix);
	
	return get_sisis_addrs_for_prefix(&p);
}

/**
 * Get SIS-IS addresses for a specific process type and host.  It is the receivers
 * responsibility to free the list when done with it.
 */
struct list * get_sisis_addrs_for_process_type_and_host(uint16_t ptype, uint32_t host_num)
{
	// Create prefix
	char prefix_addr_str[INET6_ADDRSTRLEN+1];
	memset(prefix_addr_str, 0, sizeof(prefix_addr_str));
	if (sisis_create_addr(ptype, host_num, 0, prefix_addr_str))
		return NULL;
	struct prefix_ipv6 p;
	p.prefixlen = SISIS_ADD_PREFIX_LEN_HOST_NUM;
	inet_pton(AF_INET6, prefix_addr_str, &p.prefix);
	
	return get_sisis_addrs_for_prefix(&p);
}