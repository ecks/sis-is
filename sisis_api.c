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

#include "sisis_api.h"

int sisis_socket = 0;
struct sockaddr_in sisis_listener_addr;

int sisis_listener_port = 54345;
char * sisis_listener_ip_addr = "127.0.0.1";
unsigned int next_request_id = 1;

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
		printf("Waiting for message.\n");
		buf_len = sisis_recv(buf, 1024);
		printf("Message received.\n");
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
	printf("Message[%d]:\n", msg_len);
	printf("\tVersion: %u\n", version);
	if (version == 1)
	{
		// Get request id
		unsigned int request_id = 0;
		if (msg_len >= 6)
			request_id = ntohl(*(unsigned short *)(msg+2));
		printf("\tRequest Id: %u\n", request_id);
		
		// Get command
		unsigned short command = -1;
		if (msg_len >= 4)
			command = ntohs(*(unsigned short *)(msg+2));
		printf("Command: %u\n", command);
		switch (command)
		{
			case SISIS_ACK:
				if (awaiting_ack.request_id == request_id)
					awaiting_ack.flags |= SISIS_REQUEST_ACK_INFO_ACKED;
				printf("Here1b\n");
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
			printf("Message received[%d]\n", rtn);
		}while (addr.sin_family != sisis_listener_addr.sin_family || addr.sin_addr.s_addr != sisis_listener_addr.sin_addr.s_addr || addr.sin_port != sisis_listener_addr.sin_port);
	}
	return rtn;
}

/**
 * Constructs SIS-IS message.  Remember to free memory when done.
 * Duplicated in sisisd.c
 * Returns length of message.
 */
int sisis_construct_message(char ** buf, unsigned short version, unsigned int request_id, unsigned short cmd, void * data, unsigned short data_len)
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
int sisis_create_addr(unsigned int ptype, unsigned int host_num, unsigned int pid, char * sisis_addr)
{
	// Check bounds
	if (ptype > 255 || host_num > 255)
		return 1;
	pid %= 256;
	
	// Construct SIS-IS address
	sprintf(sisis_addr, "26.%u.%u.%u", ptype, host_num, pid);
	printf("Address: %s\n", sisis_addr);
	
	return 0;
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
	
	// Send message
	char * buf;
	unsigned int buf_len = sisis_construct_message(&buf, SISIS_VERSION, request_id, SISIS_CMD_REGISTER_ADDRESS, sisis_addr, strlen(sisis_addr));
	sisis_send(buf, buf_len);
	free(buf);
	printf("Here0\n");
	
	// Wait for ack, nack, or timeout
	struct timespec timeout;
  clock_gettime(CLOCK_REALTIME, &timeout);
	timeout.tv_sec += 5;
  int status = pthread_mutex_timedlock(mutex, &timeout);
	if (!status)
		return 1;
	printf("Here2\n");
	// Remove mutex
	pthread_mutex_destroy(mutex);
	free(mutex);
	printf("Here4\n");
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
int sisis_register(unsigned int ptype, unsigned int host_num, unsigned int pid, char * sisis_addr)
{
	// Construct SIS-IS address
	if (sisis_create_addr(ptype, host_num, pid, sisis_addr))
		return 1;
	
	// Register
	int rtn = sisis_do_register(sisis_addr);
	
	// TODO: Support multiple addresses at once.
	char * thread_sisis_addr = malloc(sizeof(char) * strlen(sisis_addr));
	strcpy(thread_sisis_addr, sisis_addr);
	pthread_create(&sisis_reregistration_thread, NULL, sisis_reregister, (void *)thread_sisis_addr);
	
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

extern int sisis_netlink_route_read (void);

/**
 * Dump kernel routing table.
 * Returns zero on success.
 */
int sisis_dump_kernel_routes()
{
	sisis_netlink_route_read();
	
	return 0;
}

/* Add an IPv4 Address to RIB. */
int sisis_rib_add_ipv4 (struct route_ipv4 route)
{
	// Set up prefix
	char prefix_str[INET_ADDRSTRLEN];
	route.p->family = AF_INET;
	route.p->prefixlen = 32;
	if (inet_ntop(AF_INET, &(route.p->prefix.s_addr), prefix_str, INET_ADDRSTRLEN) != 1)
		printf("%s/%d [%u/%u]\n", prefix_str, route.p->prefixlen, route.distance, route.metric);
	return 0;
}

#ifdef HAVE_IPV6
// TODO
int sisis_rib_add_ipv6 (struct route_ipv6 route)
{
	return 0;
}
#endif /* HAVE_IPV6 */
