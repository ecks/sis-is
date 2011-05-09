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
#include <stdarg.h>

#include "sisis_api.h"
#include "sisis_structs.h"
#include "sisis_netlink.h"



//#define TIME_DEBUG

#define MIN(a,b) ((a) < (b) ? (a) : (b))

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

#ifdef USE_IPV6
#include "sisis_addr_format.h"
#endif /* USE_IPV6 */

// Reregistration information
pthread_mutex_t reregistration_array_mutex = PTHREAD_MUTEX_INITIALIZER;
reregistration_info_t * reregistrations[MAX_REREGISTRATION_THREADS] = { NULL };

// Listen for messages
pthread_t sisis_recv_from_thread;
void * sisis_recv_loop(void *);

// Request which we are waiting for an ACK or NACK for
pthread_mutex_t awaiting_acks_pool_mutex = PTHREAD_MUTEX_INITIALIZER;
struct sisis_request_ack_info awaiting_acks_pool[AWAITING_ACK_POOL_SIZE] = { { 0, 0, PTHREAD_MUTEX_INITIALIZER, 0} };

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
		
		int i;
		switch (command)
		{
			case SISIS_ACK:
				// Find associated info
				pthread_mutex_lock(&awaiting_acks_pool_mutex);
				for (i =  0; i < AWAITING_ACK_POOL_SIZE && (!awaiting_acks_pool[i].valid || awaiting_acks_pool[i].request_id != request_id); i++);
				if (i < AWAITING_ACK_POOL_SIZE)
				{
					awaiting_acks_pool[i].flags |= SISIS_REQUEST_ACK_INFO_ACKED;
				
					// Free mutex
					pthread_mutex_unlock(&awaiting_acks_pool[i].mutex);
				}
				pthread_mutex_unlock(&awaiting_acks_pool_mutex);
				break;
			case SISIS_NACK:
				// Find associated info
				pthread_mutex_lock(&awaiting_acks_pool_mutex);
				for (i =  0; i < AWAITING_ACK_POOL_SIZE && (!awaiting_acks_pool[i].valid || awaiting_acks_pool[i].request_id != request_id); i++);
				if (i < AWAITING_ACK_POOL_SIZE)
				{
					awaiting_acks_pool[i].flags |= SISIS_REQUEST_ACK_INFO_NACKED;
				
					// Free mutex
					pthread_mutex_unlock(&awaiting_acks_pool[i].mutex);
				}
				pthread_mutex_unlock(&awaiting_acks_pool_mutex);
				
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
	if (*buf == NULL)
		return 0;
	version = htons(version);
	request_id = htonl(request_id);
	cmd = htons(cmd);
	memcpy(*buf, &version, 2);
	memcpy(*buf+2, &request_id, 4);
	memcpy(*buf+6, &cmd, 2);
	memcpy(*buf+8, data, data_len);
	return buf_len;
}

#ifdef USE_IPV6
/**
 * Construct SIS-IS address.
 *
 * sisis_addr String to store resulting SIS-IS/IP address in.
 *
 * Returns zero on success.
 */
int sisis_create_addr_from_va_list(char * sisis_addr, va_list args)
{
	// Check that the components were set up
	if (components == NULL)
		return 1;
	
	int comp = 0, bit = 0, consumed_bits = 0, comp_bits = components[comp].bits;
	unsigned short part = 0;
	uint64_t arg = (components[comp].flags & SISIS_COMPONENT_FIXED) ? components[comp].fixed_val : va_arg(args, uint64_t);
	for (; bit < 128; bit+=consumed_bits)
	{
		// Find next component with available bits
		while (comp_bits == 0 && comp + 1 < num_components)
		{
			comp++;
			comp_bits = components[comp].bits;
			if (components[comp].flags & SISIS_COMPONENT_FIXED)
				arg = components[comp].fixed_val;
			else
				arg = va_arg(args, uint64_t);
		}
		// Fill remainder with zeros if there are no more components
		if (comp_bits == 0)
		{
			consumed_bits = 16 - (bit % 16);
			part <<= consumed_bits;
		}
		// Otherwise, copy from arg
		else
		{
			consumed_bits = MIN(16 - (bit % 16), comp_bits);
			//printf("Consumed: %d\n", consumed_bits);
			//printf("Arg: %llu\n", arg);
			int i = 0;
			for (; i < consumed_bits; i++)
			{
				part <<= 1;
				comp_bits--;
				part |= (arg >> comp_bits) & 0x1;
			}
			//printf("%hu\n", part);
		}
		
		// Print now?
		if ((bit + consumed_bits) % 16 == 0)
		{
			sprintf(sisis_addr+(bit/16)*5, "%04hx%s", part, bit + consumed_bits == 128 ? "" : ":");
			part = 0;
		}
	}
	
	return 0;
}

/**
 * Construct SIS-IS address.
 *
 * sisis_addr String to store resulting SIS-IS/IP address in.
 * 
 * Returns zero on success.
 */
int sisis_create_addr(char * sisis_addr, ...)
{
	va_list args;
	va_start(args, sisis_addr);
	int rtn = sisis_create_addr_from_va_list(sisis_addr, args);
	va_end(args);
	
	return rtn;
}

/**
 * Split an SIS-IS address into components.
 *
 * sisis_addr SIS-IS/IP address.
 *
 * Returns zero on success.
 */
int get_sisis_addr_components_from_va_list(char * sisis_addr, va_list args)
{
	// Check that the components were set up
	if (components == NULL)
		return 1;
	
	// Remove :: and ensure 4 numbers per 16 bits
	int idx, idx2, len = strlen(sisis_addr);
	char before[INET6_ADDRSTRLEN], after[INET6_ADDRSTRLEN], full[INET6_ADDRSTRLEN+1];
	full[0] = 0;
	// Count colons
	int colons = 0;
	for (idx = 0; idx < len; idx++)
		if (sisis_addr[idx] == ':')
			colons++;
	for (idx = 0, idx2 = 0; idx < len; idx++)
	{
		if (idx > 1 && sisis_addr[idx-1] == ':' && sisis_addr[idx] == ':')
		{
			for (colons--; colons < 7; colons++)
			{
				full[idx2++] = '0';
				full[idx2++] = ':';
			}
			if (idx + 1 == len)
				full[idx2++] = '0';
			full[idx2] = 0;
		}
		else
		{
			full[idx2] = sisis_addr[idx];
			idx2++;
			full[idx2] = 0;
		}
	}
	char * full_ptr = full;
	
	// Parse into args
	int comp = 0, bit = 0, consumed_bits = 0, comp_bits = components[comp].bits;
	unsigned short part = 0;
	uint64_t * arg = va_arg(args, uint64_t *);
	if (arg != NULL)
		memset(arg, 0, sizeof(*arg));
	for (; bit < 128; bit+=consumed_bits)
	{
		// Next part?
		if (bit % 16 == 0)
		{
			sscanf(full_ptr, "%4hx", &part);
			while (*full_ptr != '\0' && *full_ptr != ':')
				full_ptr++;
			if (*full_ptr == ':')
				full_ptr++;
		}
		
		consumed_bits = 16;
		// Find next component with available bits
		while (comp_bits == 0 && comp + 1 < num_components)
		{
			comp++;
			comp_bits = components[comp].bits;
			arg = va_arg(args, uint64_t *);
			if (arg != NULL)
				memset(arg, 0, sizeof(*arg));
		}
		// Make sure there are no more components
		if (comp_bits > 0)
		{
			consumed_bits = MIN(16 - (bit % 16), comp_bits);
			int i = 0;
			for (; i < consumed_bits; i++)
			{
				comp_bits--;
				if (arg != NULL)
				{
					*arg <<= 1;
					*arg |= (part >> (15-((bit+i)%16))) & 0x1;
				}
			}
		}
	}
	
	return 0;
}

/**
 * Split an SIS-IS address into components.
 *
 * sisis_addr SIS-IS/IP address
 */
int get_sisis_addr_components(char * sisis_addr, ...)
{
	// Parse into args
	va_list args;
	va_start(args, sisis_addr);
	int rtn = get_sisis_addr_components_from_va_list(sisis_addr, args);
	va_end(args);
	
	return rtn;
}

#else /* IPv4 Version */
/**
 * Construct SIS-IS address.
 *
 * sisis_addr String to store resulting SIS-IS/IP address in.
 * 
 * Returns zero on success.
 */
int sisis_create_addr(unsigned int ptype, unsigned int host_num, unsigned int pid, char * sisis_addr)
{
	// Check bounds
	if (ptype > 255 || host_num > 255)
		return 1;
	pid %= 256;
	
	// Construct SIS-IS address
	sprintf(sisis_addr, "26.%u.%u.%u", ptype, host_num, pid);
	
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
	sscanf(sisis_addr, "26.%u.%u.%u", &rtn.ptype, &rtn.host_num, &rtn.pid);
	return rtn;
}
#endif /* USE_IPV6 */

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
	
	// Find empty awaiting ack info from pool
	int idx;
	pthread_mutex_lock(&awaiting_acks_pool_mutex);
	for (idx =  0; idx < AWAITING_ACK_POOL_SIZE && awaiting_acks_pool[idx].valid; idx++);
	awaiting_acks_pool[idx].valid = 1;
	awaiting_acks_pool[idx].request_id = request_id;
	awaiting_acks_pool[idx].flags = 0;
	// Be sure that mutex is locked
	pthread_mutex_unlock(&awaiting_acks_pool[idx].mutex);
	pthread_mutex_lock(&awaiting_acks_pool[idx].mutex);
	// Release lock on pool
	pthread_mutex_unlock(&awaiting_acks_pool_mutex);
	
#ifdef USE_IPV6
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
#else /* IPv4 Version */
	// Send message
	char * buf;
	unsigned int buf_len = sisis_construct_message(&buf, SISIS_VERSION, request_id, SISIS_CMD_REGISTER_ADDRESS, sisis_addr, strlen(sisis_addr));
#endif /* USE_IPV6 */
	
#ifdef TIME_DEBUG
	char * ts1, * ts2;
	// Get time
	struct timespec time;
  clock_gettime(CLOCK_REALTIME, &time);
	asprintf(&ts1, "[%ld.%09ld] Sending SIS-IS request to zebra.\n", time.tv_sec, time.tv_nsec);
#endif
	sisis_send(buf, buf_len);
	free(buf);
	
	// Wait for ack, nack, or timeout
	struct timespec timeout;
  clock_gettime(CLOCK_REALTIME, &timeout);
	timeout.tv_sec += 5;
  int status = pthread_mutex_timedlock(&awaiting_acks_pool[idx].mutex, &timeout);
	if (status != 0)
		return 1;

#ifdef TIME_DEBUG
	// Get time
	clock_gettime(CLOCK_REALTIME, &time);
	asprintf(&ts2, "[%ld.%09ld] Received SIS-IS response from zebra.\n", time.tv_sec, time.tv_nsec);
	
	printf("%s%s", ts1, ts2);
	free(ts1);
	free(ts2);
#endif
	
	// Check if it was an ack of nack and return to pool
	pthread_mutex_lock(&awaiting_acks_pool_mutex);
	int rtn = (awaiting_acks_pool[idx].request_id == request_id && (awaiting_acks_pool[idx].flags & SISIS_REQUEST_ACK_INFO_ACKED)) ? 0 : 1;
	awaiting_acks_pool[idx].valid = 0;
	pthread_mutex_unlock(&awaiting_acks_pool_mutex);
	
	return rtn;
}

void * sisis_reregister(void * arg)
{
	reregistration_info_t * info = (reregistration_info_t *)arg;
	int active = 1;
	while (active)
	{
		// Sleep
		sleep(SISIS_REREGISTRATION_TIMEOUT);
		
		// Check if this is still active
		pthread_mutex_lock(&reregistration_array_mutex);
		active = info->active;
		pthread_mutex_unlock(&reregistration_array_mutex);
		
		// Register
		if (active)
			sisis_do_register(info->addr);
	}
	
	// Delete info
	pthread_mutex_lock(&reregistration_array_mutex);
	reregistrations[info->idx] = NULL;
	pthread_mutex_unlock(&reregistration_array_mutex);
	
	// Free struct
	free(info->addr);
	free(info);
}

#ifdef USE_IPV6
/**
 * Registers SIS-IS process.
 *
 * sisis_addr String to store resulting SIS-IS/IP address in.
 * 
 * Returns zero on success.
 */
int sisis_register(char * sisis_addr, ...)
{
	// Construct SIS-IS addresss
	va_list args;
	va_start(args, sisis_addr);
	int rtn = sisis_create_addr_from_va_list(sisis_addr, args);
	va_end(args);
	if (rtn)
		return 1;
	
	// Register
	rtn = sisis_do_register(sisis_addr);
	
	// Set up reregistration
	pthread_mutex_lock(&reregistration_array_mutex);
	int idx;
	for (idx = 0; idx < MAX_REREGISTRATION_THREADS && reregistrations[idx] != NULL; idx++);
	pthread_mutex_unlock(&reregistration_array_mutex);
	
	// Check if there is an empty spot
	if (idx == MAX_REREGISTRATION_THREADS)
		return 2;
	if ((reregistrations[idx] = malloc(sizeof(*reregistrations[idx]))) == NULL)
		return 3;
	if ((reregistrations[idx]->addr = malloc(sizeof(char) * (strlen(sisis_addr)+1))) == NULL)
		return 4;
	strcpy(reregistrations[idx]->addr, sisis_addr);
	reregistrations[idx]->active = 1;
	reregistrations[idx]->idx = idx;
	pthread_create(&reregistrations[idx]->thread, NULL, sisis_reregister, (void *)reregistrations[idx]);
	
	return rtn;
}

/**
 * Unregisters SIS-IS process.
 *
 * First parameter is ignored.  Set to NULL
 * 
 * Returns zero on success.
 */
int sisis_unregister(void * nil, ...)
{
	// Construct SIS-IS address
	char sisis_addr[INET6_ADDRSTRLEN+1];
	va_list args;
	va_start(args, nil);
	int rtn = sisis_create_addr_from_va_list(sisis_addr, args);
	va_end(args);
	if (rtn)
		return 1;

	// Find and stop deregistration thread	
	pthread_mutex_lock(&reregistration_array_mutex);
	int idx;
	for (idx = 0; idx < MAX_REREGISTRATION_THREADS && (reregistrations[idx] == NULL || strcmp(reregistrations[idx]->addr, sisis_addr) != 0); idx++);
	if (idx < MAX_REREGISTRATION_THREADS)
		reregistrations[idx]->active = 0;
	pthread_mutex_unlock(&reregistration_array_mutex);
	
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
#else /* IPv4 Version */
/**
 * Registers SIS-IS process.
 *
 * sisis_addr String to store resulting SIS-IS/IP address in.
 * 
 * Returns zero on success.
 */
int sisis_register(unsigned int ptype, unsigned int host_num, unsigned int pid, char * sisis_addr)
{
	// Construct SIS-IS address
	if (sisis_create_addr(ptype, host_num, pid, sisis_addr))
		return 1;
	
	// Register
	int rtn = sisis_do_register(sisis_addr);
	
	// Set up reregistration
	pthread_mutex_lock(&reregistration_array_mutex);
	int idx;
	for (idx = 0; idx < MAX_REREGISTRATION_THREADS && reregistrations[idx] != NULL; idx++);
	pthread_mutex_unlock(&reregistration_array_mutex);
	
	// Check if there is an empty spot
	if (idx == MAX_REREGISTRATION_THREADS)
		return 2;
	if ((reregistrations[idx] = malloc(sizeof(*reregistrations[idx]))) == NULL)
		return 3;
	if ((reregistrations[idx]->addr = malloc(sizeof(char) * (strlen(sisis_addr)+1))) == NULL)
		return 4;
	strcpy(reregistrations[idx]->addr, sisis_addr);
	reregistrations[idx]->active = 1;
	reregistrations[idx]->idx = idx;
	pthread_create(&reregistrations[idx]->thread, NULL, sisis_reregister, (void *)reregistrations[idx]);
	
	return rtn;
}

/**
 * Unregisters SIS-IS process.
 * Returns zero on success.
 */
int sisis_unregister(unsigned int ptype, unsigned int host_num, unsigned int pid)
{
	// Construct SIS-IS address
	char sisis_addr[INET_ADDRSTRLEN+1];
	if (sisis_create_addr(ptype, host_num, pid, sisis_addr))
		return 1;
	
	// Find and stop deregistration thread	
	pthread_mutex_lock(&reregistration_array_mutex);
	int idx;
	for (idx = 0; idx < MAX_REREGISTRATION_THREADS && (reregistrations[idx] == NULL || strcmp(reregistrations[idx]->addr, sisis_addr) != 0); idx++);
	if (idx < MAX_REREGISTRATION_THREADS)
		reregistrations[idx]->active = 0;
	pthread_mutex_unlock(&reregistration_array_mutex);
	
	// Setup socket
	sisis_socket_open();
	
	// Get request id
	unsigned int request_id = next_request_id++;
	
	// Send message
	char * buf;
	unsigned int buf_len = sisis_construct_message(&buf, SISIS_VERSION, request_id, SISIS_CMD_UNREGISTER_ADDRESS, sisis_addr, strlen(sisis_addr));
	sisis_send(buf, buf_len);
	free(buf);
	
	return 0;
}
#endif /* USE_IPV6 */

/**
 * Dump kernel routing table.
 * Returns zero on success.
 */
int sisis_dump_kernel_routes()
{
	// Set up list of IPv4 rib entries
	if (ipv4_rib_routes)
		FREE_LINKED_LIST(ipv4_rib_routes);
	if ((ipv4_rib_routes = malloc(sizeof(*ipv4_rib_routes))) != NULL)
		memset(ipv4_rib_routes, 0, sizeof(*ipv4_rib_routes));

#ifdef HAVE_IPV6
	// Set up list of IPv6 rib entries
	if (ipv6_rib_routes)
		FREE_LINKED_LIST(ipv6_rib_routes);
	if ((ipv6_rib_routes = malloc(sizeof(*ipv6_rib_routes))) != NULL)
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
	if (ipv4_rib_routes != NULL && node != NULL)
	{
		node->data = (void *)route;
		LIST_APPEND(ipv4_rib_routes, node);
	}
	
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
	if (ipv6_rib_routes != NULL && node != NULL)
	{
		node->data = (void *)route;
		LIST_APPEND(ipv6_rib_routes, node);
	}
	
	return 0;
}
#endif /* HAVE_IPV6 */

/** Subscribe to route add/remove messages */
int subscribe_to_rib_changes(struct subscribe_to_rib_changes_info * info)
{
	int rtn = 0;
	
	// Set up callbacks
	struct sisis_netlink_routing_table_info * subscribe_info = malloc(sizeof(struct sisis_netlink_routing_table_info));
	if (subscribe_info == NULL)
		return -1;
	memset(subscribe_info, 0, sizeof(*subscribe_info));
	subscribe_info->rib_add_ipv4_route = info->rib_add_ipv4_route;
	subscribe_info->rib_remove_ipv4_route = info->rib_remove_ipv4_route;
	#ifdef HAVE_IPV6
	subscribe_info->rib_add_ipv6_route = info->rib_add_ipv6_route;
	subscribe_info->rib_remove_ipv6_route = info->rib_remove_ipv6_route;
	#endif /* HAVE_IPV6 */
	
	// Subscribe to changes
	sisis_netlink_subscribe_to_rib_changes(subscribe_info);
	info->subscribe_info = subscribe_info;
	
	return rtn;
}

/** Unsubscribe to route add/remove messages */
int unsubscribe_to_rib_changes(struct subscribe_to_rib_changes_info * info)
{
	// Subscribe to changes
	sisis_netlink_unsubscribe_to_rib_changes(info->subscribe_info);
	free(info->subscribe_info);
	info->subscribe_info = NULL;
}

#ifdef USE_IPV6
/**
 * Get SIS-IS addresses that match a given IP prefix.  It is the receiver's
 * responsibility to free the list when done with it.
 */
struct list * get_sisis_addrs_for_prefix(struct prefix_ipv6 * p)
{
	// Update kernel routes
	sisis_dump_kernel_routes();
	
	// IPv6 version
	// Create prefix mask IPv6 addr
	char prefix_addr_str[INET6_ADDRSTRLEN+1] = "";
	int i;
	if (p->prefixlen / 16 > 0)
		strcat(prefix_addr_str, "ffff");
	for (i = 1; i < (p->prefixlen / 16); i++)
		strcat(prefix_addr_str, ":ffff");
	int tmp = 0;
	for (i = 0; i < p->prefixlen % 16; i++)
	{
		tmp >>= 1;
		tmp |= 0x8000;
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
	if (rtn != NULL)
	{
		memset(rtn, 0, sizeof(*rtn));
		struct listnode * node;
		LIST_FOREACH(ipv6_rib_routes, node)
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
				if (new_node != NULL)
				{
					memset(new_node, 0, sizeof(*new_node));
					if ((new_node->data = malloc(sizeof(*new_node->data))) != NULL)
					{
						memcpy(new_node->data, &route->p->prefix, sizeof(route->p->prefix));
						LIST_APPEND(rtn, new_node);
					}
				}
			}
		}
	}
	
	return rtn;
}

/**
 * Creates an IPv6 prefix
 */
struct prefix_ipv6 sisis_make_ipv6_prefix(char * addr, int prefix_len)
{
	struct prefix_ipv6 p;
	p.prefixlen = prefix_len;
	inet_pton(AF_INET6, addr, &p.prefix);
	return p;
}
#else /* IPv4 Version */
/**
 * Get SIS-IS addresses that match a given ipv4 prefix.  It is the receivers
 * responsibility to free the list when done with it.
 */
struct list * get_sisis_addrs_for_prefix(struct prefix_ipv4 * p)
{
	// Update kernel routes
	sisis_dump_kernel_routes();
	
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
	if (rtn != NULL)
	{
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
				if (new_node != NULL)
				{
					if ((new_node->data = malloc(sizeof(*new_node->data))) != NULL)
					{
						memcpy(new_node->data, &route->p->prefix, sizeof(route->p->prefix));
						LIST_APPEND(rtn,new_node);
					}
				}
			}
		}
	}
	
	return rtn;
}

/**
 * Get SIS-IS addresses for a specific process type.  It is the receivers
 * responsibility to free the list when done with it.
 */
struct list * get_sisis_addrs_for_process_type(unsigned int ptype)
{
	// Create prefix
	char prefix_addr_str[INET_ADDRSTRLEN+1];
	memset(prefix_addr_str, 0, sizeof(prefix_addr_str));
	if (sisis_create_addr(ptype, 0, 0, prefix_addr_str))
		return NULL;
	struct prefix_ipv4 p;
	p.prefixlen = SISIS_ADD_PREFIX_LEN_PTYPE;
	inet_pton(AF_INET, prefix_addr_str, &p.prefix);
	
	return get_sisis_addrs_for_prefix(&p);
}

/**
 * Get SIS-IS addresses for a specific process type and host.  It is the receivers
 * responsibility to free the list when done with it.
 */
struct list * get_sisis_addrs_for_process_type_and_host(unsigned int ptype, unsigned int host_num)
{
	// Create prefix
	char prefix_addr_str[INET_ADDRSTRLEN+1];
	memset(prefix_addr_str, 0, sizeof(prefix_addr_str));
	if (sisis_create_addr(ptype, host_num, 0, prefix_addr_str))
		return NULL;
	struct prefix_ipv4 p;
	p.prefixlen = SISIS_ADD_PREFIX_LEN_HOST_NUM;
	inet_pton(AF_INET, prefix_addr_str, &p.prefix);
	
	return get_sisis_addrs_for_prefix(&p);
}
#endif /* USE_IPV6 */