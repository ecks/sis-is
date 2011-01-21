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

#include "sisis_api.h"

int sisis_socket = 0;
struct sockaddr_in sisis_listener_addr;

int sisis_listener_port = 54345;
char * sisis_listener_ip_addr = "127.0.0.1";

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
	return -1;
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
 * Registers SIS-IS process.
 *
 * sisis_addr String to store resulting SIS-IS/IP address in.
 * 
 * Returns zero on success.
 */
int sisis_register(unsigned int ptype, unsigned int host_num, char * sisis_addr)
{
	// Construct SIS-IS address
	sprintf(sisis_addr, "26.0.%u.%u", ptype, host_num);
	
	// Setup socket
	sisis_socket_open();
	
	// Send message
	char * buf;
	unsigned int buf_len = sisis_construct_message(&buf, SISIS_VERSION, SISIS_CMD_REGISTER_ADDRESS, sisis_addr, strlen(sisis_addr));
	sisis_send(buf, buf_len);
	free(buf);
	
	// TODO: Wait for message back
	
	return 0;
}

/**
 * Unregisters SIS-IS process.
 * Returns zero on success.
 */
int sisis_unregister(unsigned int ptype, unsigned int host_num)
{
	// Construct SIS-IS address
	char sisis_addr[INET_ADDRSTRLEN];
	sprintf(sisis_addr, "26.0.%u.%u", ptype, host_num);
	
	// Setup socket
	sisis_socket_open();
	
	// Send message
	char * buf;
	unsigned int buf_len = sisis_construct_message(&buf, SISIS_VERSION, SISIS_CMD_UNREGISTER_ADDRESS, sisis_addr, strlen(sisis_addr));
	sisis_send(buf, buf_len);
	free(buf);
	
	return 0;
}