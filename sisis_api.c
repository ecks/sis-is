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

#define TIME_DEBUG

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

// SIS-IS address component info
int num_components;
sisis_component_t * components;

// TODO: Support multiple addresses at once.
pthread_t sisis_reregistration_thread;

// Listen for messages
pthread_t sisis_recv_from_thread;
void * sisis_recv_loop(void *);

// TODO: Handle multiple outstanding requests later
// Request which we are waiting for an ACK or NACK for
struct sisis_request_ack_info awaiting_ack;

/**
 * Setup SIS-IS address format.  Must be called before any other functions.
 *
 * filename Name of file defining SIS-IS address format.
 *
 * Returns 0 on success
 */
int setup_sisis_addr_format(const char * filename)
{
	// Components (at most 128)
	num_components = 0;
	components = (sisis_component_t*)malloc(sizeof(sisis_component_t)*128);
	memset(components, 0, sizeof(sisis_component_t)*128);
	int total_bits = 0;
	
	// Open file
	FILE * file = fopen(filename, "r");
	if (!file)
		return 1;
	
	// Read each line
	int line_num = 1;
	int line_size = 512;
	char * line = malloc(sizeof(char) * line_size);
	while (fgets(line, line_size, file))
	{
		// Read until we get the full line
		int len = strlen(line);
		while (len > 0 && line[len-1] != '\n')
		{
			line_size *= 2;
			line = realloc(line, line_size);
			// Set len to 0 to stop looping on EOF or error
			if (!fgets(line+len, line_size-len, file))
				len = 0;
			// Get new length
			else
				len = strlen(line);
		}
		
		// Parse line
		int i, start = 0, field = 1;
#define SISIS_DAT_FIELD_NAME			1
#define SISIS_DAT_FIELD_BITS			2
#define SISIS_DAT_FIELD_FIXED			3
#define SISIS_DAT_FIELD_FIXED_VAL	4
		for (i = 0, len = strlen(line) + 1; i < len; i++)	// Intentionally go to '\0'
		{
			if (line[i] == ' ' || line[i] == '\t' || line[i] == '\r' || line[i] == '\n' || line[i] == '\0')
			{
				if (start == i)
					start++;
				else
				{
					// Copy to new buffer
					int buf_len = i - start + 1;	// Add 1 for '\0'
					char * buf = malloc(sizeof(char)*buf_len);
					buf[buf_len-1] = '\0';
					memcpy(buf, line+start, buf_len-1);
					start = i+1;
					
					/* Save to correct component */
					switch (field)
					{	
						// Name
						case SISIS_DAT_FIELD_NAME:
							components[num_components].name = buf;
							break;
						// # of bits
						case SISIS_DAT_FIELD_BITS:
							{
								int h = 0;
								for (; h < buf_len-1; h++)
								{
									if (buf[h] >= '0' && buf[h] <= '9')
										components[num_components].bits = components[num_components].bits*10+(buf[h]-'0');
									else
									{
										printf("Unexpected \"%c\" on line %d around \"%s\".\n", buf[h], line_num, buf);
										return 1;
									}
								}
								free(buf);
								
								// Restrict components to 64 bits
								if (components[num_components].bits > 64)
								{
									printf("Components may be at most 64 bits.\n");
									return 1;
								}
								
								// Check total number of bits
								total_bits += components[num_components].bits;
								if (total_bits > 128)
								{
									printf("Too many bits.\n");
									return 1;
								}
							}
							break;
						// Fixed or default
						case SISIS_DAT_FIELD_FIXED:
							if (strcmp(buf, "fixed") == 0)
								components[num_components].flags |= SISIS_COMPONENT_FIXED;
							else
							{
								printf("Unexpected \"%s\" on line %d.  Expecting \"fixed\".\n", buf, line_num);
								return 1;
							}
							free(buf);
							break;
						// Default value
						case SISIS_DAT_FIELD_FIXED_VAL:
							{
								short base = 10;
								int h = 0;
								for (; h < buf_len-1; h++)
								{
									if (buf[h] >= '0' && buf[h] <= '9')
										components[num_components].fixed_val = components[num_components].fixed_val*base+(buf[h]-'0');
									else if (base == 16 && buf[h] >= 'a' && buf[h] <= 'f')
										components[num_components].fixed_val = components[num_components].fixed_val*base+(buf[h]-'a'+10);
									else if (h == 1 && buf[0] == '0' && buf[1] <= 'x')
										base = 16;
									else
									{
										printf("Unexpected \"%c\" on line %d around \"%s\".\n", buf[h], line_num, buf);
										exit(1);
									}
								}
								free(buf);
							}
							break;
					}
					
					// Next field
					field++;
				}
			}
		}
		
		// Add to number of components
		num_components++;
		
		// Next line
		line_num++;
	}
	fclose(file);
	
	// Shrink memory for components
	components = realloc(components, sizeof(sisis_component_t)*num_components);
	
	return 0;
}

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
	
	// Parse into args
	int comp = 0, bit = 0, consumed_bits = 0, comp_bits = components[comp].bits;
	unsigned short part = 0;
	uint64_t * arg = va_arg(args, uint64_t *);
	memset(arg, 0, sizeof(*arg));
	for (; bit < 128; bit+=consumed_bits)
	{
		// Next part?
		if (bit % 16 == 0)
			sscanf(full+(bit/16)*5, "%4hx", &part);
		
		consumed_bits = 16;
		// Find next component with available bits
		while (comp_bits == 0 && comp + 1 < num_components)
		{
			comp++;
			comp_bits = components[comp].bits;
			arg = va_arg(args, uint64_t *);
			memset(arg, 0, sizeof(*arg));
		}
		// Make sure there are no more components
		if (comp_bits > 0)
		{
			consumed_bits = MIN(16 - (bit % 16), comp_bits);
			int i = 0;
			for (; i < consumed_bits; i++)
			{
				*arg <<= 1;
				comp_bits--;
				*arg |= (part >> 15-((bit+i)%16)) & 0x1;
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
  int status = pthread_mutex_timedlock(mutex, &timeout);
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
	
	// TODO: Support multiple addresses at once.
	char * thread_sisis_addr = malloc(sizeof(char) * (strlen(sisis_addr)+1));
	strcpy(thread_sisis_addr, sisis_addr);
	pthread_create(&sisis_reregistration_thread, NULL, sisis_reregister, (void *)thread_sisis_addr);
	
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
	int rtn = sisis_create_addr_from_va_list(&sisis_addr, args);
	va_end(args);
	if (rtn)
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
 * Get SIS-IS addresses that match a given IP prefix.  It is the receiver's
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
	
	// IPv6 version
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
			new_node->data = malloc(sizeof(route->p->prefix));
			memcpy(new_node->data, &route->p->prefix, sizeof(route->p->prefix));
			LIST_APPEND(rtn,new_node);
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