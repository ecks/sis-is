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

#include "sisis_api.h"

int sisis_socket = 0;
struct sockaddr_in sisis_listener_addr;

int sisis_listener_port = 54345;
char * sisis_listener_ip_addr = "127.0.0.1";

// TODO: Support multiple addresses at once.
pthread_t sisis_reregistration_thread;

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

	// Set up SIS-IS listener address structure
	memset(&sisis_listener_addr, 0, sizeof(sisis_listener_addr));
	sisis_listener_addr.sin_family = AF_INET;
	inet_pton(AF_INET, sisis_listener_ip_addr, &(sisis_listener_addr.sin_addr));
	sisis_listener_addr.sin_port = htons((in_port_t) sisis_listener_port);
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
 * Returns length of message.
 */
int sisis_construct_message(char ** buf, unsigned short version, unsigned short cmd, void * data, unsigned short data_len)
{
	unsigned int buf_len = data_len + 4;
	*buf = malloc(sizeof(char) * buf_len);
	version = htons(version);
	cmd = htons(cmd);
	memcpy(*buf, &version, 2);
	memcpy(*buf+2, &cmd, 2);
	memcpy(*buf+4, data, data_len);
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
	
	// Send message
	char * buf;
	unsigned int buf_len = sisis_construct_message(&buf, SISIS_VERSION, SISIS_CMD_REGISTER_ADDRESS, sisis_addr, strlen(sisis_addr));
	sisis_send(buf, buf_len);
	free(buf);
	
	// TODO: Wait for message back
	// TODO: Set timeout on receive
	char recv_buf[1024];
	int recv_buf_len = sisis_recv(recv_buf, 1024);
	
	return 0;
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
	sisis_do_register(sisis_addr);
	
	// TODO: Support multiple addresses at once.
	char * thread_sisis_addr = malloc(sizeof(char) * strlen(sisis_addr));
	strcpy(thread_sisis_addr, sisis_addr);
	pthread_create(&sisis_reregistration_thread, NULL, sisis_reregister, (void *)thread_sisis_addr);
	
	return 0;
}

/**
 * Unregisters SIS-IS process.
 * Returns zero on success.
 */
int sisis_unregister(unsigned int ptype, unsigned int host_num, unsigned int pid)
{
	// Construct SIS-IS address
	char sisis_addr[INET_ADDRSTRLEN];
	if (sisis_create_addr(ptype, host_num, pid, sisis_addr))
		return 1;
	
	// Setup socket
	sisis_socket_open();
	
	// Send message
	char * buf;
	unsigned int buf_len = sisis_construct_message(&buf, SISIS_VERSION, SISIS_CMD_UNREGISTER_ADDRESS, sisis_addr, strlen(sisis_addr));
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
	/*
	// Setup socket
	sisis_socket_open();
	
	// Send message
	char * buf;
	unsigned int buf_len = sisis_construct_message(&buf, SISIS_VERSION, SISIS_CMD_DUMP_ROUTES, NULL, 0);
	sisis_send(buf, buf_len);
	free(buf);
	*/
	
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
