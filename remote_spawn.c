/*
 * SIS-IS Test program.
 * Stephen Sigwart
 * University of Delaware
 */

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

#include "remote_spawn.h"

#include "../tests/sisis_api.h"
#include "../tests/sisis_process_types.h"

int sockfd = -1, con = -1;
int host_num, pid;

void close_listener()
{
	if (sockfd != -1)
	{
		printf("Closing listening socket...\n");
		close(sockfd);
		
		// Unregister
		sisis_unregister(SISIS_PTYPE_REMOTE_SPAWN, host_num, pid);
		
		sockfd = -1;
	}
}

void terminate(int signal)
{
	printf("Terminating...\n");
	close_listener();
	if (con != -1)
	{
		printf("Closing remove connection socket...\n");
		close(con);
	}
	exit(0);
}

int main (int argc, char ** argv)
{
	// Check number of args
	if (argc != 2)
	{
		printf("Usage: %s <host_num>\n", argv[0]);
		exit(1);
	}
	
	// Get host number
	sscanf (argv[1], "%d", &host_num);
	char sisis_addr[INET_ADDRSTRLEN+1];
	
	// Get pid
	pid = getpid();
	
	// Register address
	if (sisis_register(SISIS_PTYPE_REMOTE_SPAWN, host_num, pid, sisis_addr) != 0)
	{
		printf("Failed to register SIS-IS address.\n");
		exit(1);
	}
	
	// Status
	printf("Opening socket at %s on port %i.\n", sisis_addr, REMOTE_SPAWN_PORT);
	
	// Set up socket address info
	struct addrinfo hints, *addr;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;	// IPv4
	hints.ai_socktype = SOCK_DGRAM;
	char port_str[8];
	sprintf(port_str, "%u", REMOTE_SPAWN_PORT);
	getaddrinfo(sisis_addr, port_str, &hints, &addr);
	
	// Create socket
	if ((sockfd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol)) == -1)
	{
		printf("Failed to open socket.\n");
		exit(1);
	}
	
	// Bind to port
	if (bind(sockfd, addr->ai_addr, addr->ai_addrlen) == -1)
	{
		printf("Failed to bind socket to port.\n");
		close_listener();
		exit(2);
	}
	
	// Status message
	inet_ntop(AF_INET, &((struct sockaddr_in *)(addr->ai_addr))->sin_addr, sisis_addr, INET_ADDRSTRLEN);
	printf("Socket opened at %s on port %u.\n", sisis_addr, ntohs(((struct sockaddr_in *)(addr->ai_addr))->sin_port));
	
	// Set up signal handling
	signal(SIGABRT, terminate);
	signal(SIGTERM, terminate);
	signal(SIGINT, terminate);
	
	// Wait for message
	struct sockaddr remote_addr;
	int len;
	char buf[256];
	socklen_t addr_size = sizeof remote_addr;
	while ((len = recvfrom(sockfd, buf, 256, 0, &remote_addr, &addr_size)) != -1)
	{
		int resp = REMOTE_SPAWN_RESP_INVALID_REQUEST;
		if (strlen(buf) == 8)
		{
			int request = ntol(buf);
			int ptype = ntol(buf+4);
			
			// Check if this is a known process
			switch (ptype)
			{
				// Memory monitor
				case SISIS_PTYPE_MEMORY_MONITOR:
					if (request == REMOTE_SPAWN_REQ_START)
					{
						pid_t pid;
						if ((pid == fork()) == 0)
						{
							// TODO: Remove full path later and use execlp
							// TODO: How do I check for error
							execl("/home/ssigwart/sis-is/memory_monitor/memory_monitor", "memory_monitor", argv[1], NULL);
						}
						else if (pid > 0)
							resp = REMOTE_SPAWN_RESP_OK;
						else
							resp = REMOTE_SPAWN_RESP_SPAWN_FAILED;
					}
					else
						resp = REMOTE_SPAWN_RESP_NOT_IMPLEMENTED;
					break;
				// Don't spawn yourself
				case SISIS_PTYPE_REMOTE_SPAWN:
					resp = REMOTE_SPAWN_RESP_NOT_SPAWNABLE;
					break;
				// Unknown process
				default:
					resp = REMOTE_SPAWN_RESP_INVALID_PROCESS_TYPE;
					break;
			}
			
			
			// Send response
			char * send_buf_cur = send_buf;
			if (sendto(sockfd, send_buf_cur, len, 0, &remote_addr, addr_size) == -1)
				printf("Failed to send message.\n");
		}
	}
	
	// Close socket
	close_listener();
}
