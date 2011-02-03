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

#include <errno.h>
#include "leader_elector.h"
#include "../memory_monitor/memory_monitor.h"

#include "../tests/sisis_api.h"
#include "../tests/sisis_structs.h"
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
		sisis_unregister(SISIS_PTYPE_LEADER_ELECTOR, host_num, pid);
		
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
	if (sisis_register(SISIS_PTYPE_LEADER_ELECTOR, host_num, pid, sisis_addr) != 0)
	{
		printf("Failed to register SIS-IS address.\n");
		exit(1);
	}
	
	// Status
	printf("Opening socket at %s on port %i.\n", sisis_addr, LEADER_ELECTOR_PORT);
	
	// Set up socket address info
	struct addrinfo hints, *addr;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;	// IPv4
	hints.ai_socktype = SOCK_DGRAM;
	char port_str[8];
	sprintf(port_str, "%u", LEADER_ELECTOR_PORT);
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
	
	// Check how many other processes there are
	struct list * monitor_addrs = get_sisis_addrs_for_process_type(SISIS_PTYPE_MEMORY_MONITOR);
	if (monitor_addrs == NULL)
	{
		printf("No other SIS-IS hosts found.\n");
		close_listener();
		exit(2);
	}
	
	// List other hosts
	int num_hosts = 0;
	struct listnode * node;
	LIST_FOREACH(monitor_addrs, node)
	{
		num_hosts++;
		
		// Print address
		struct in_addr * remote_addr = (struct in_addr *)node->data;
		char addr_str[INET_ADDRSTRLEN+1];
		if (inet_ntop(AF_INET, remote_addr, addr_str, INET_ADDRSTRLEN+1) != 1)
			printf("Host: %s\n", addr_str);
		printf("--------------------------------------------------------------------------------\n");
		
		// Set up socket info
		struct sockaddr_in sockaddr;
		int sockaddr_size = sizeof(sockaddr);
		memset(&sockaddr, 0, sockaddr_size);
		sockaddr.sin_family = AF_INET;
		sockaddr.sin_port = MEMORY_MONITOR_PORT;
    sockaddr.sin_addr = *remote_addr;
		
		// Get memory stats
		char * req = "data";
		if (sendto(sockfd, req, strlen(req), 0, (struct sockaddr *)&sockaddr, sockaddr_size) == -1)
			printf("Failed to send message.  Error: %i\n", errno);
		else
		{
			struct sockaddr_in fromaddr;
			int fromaddr_size = sizeof(fromaddr);
			char buf[65508];
			int len;
			if (len = recvfrom(sockfd, buf, 65507, 0, (struct sockaddr *)&fromaddr, &fromaddr_size))
			{
				buf[len] = '\0';
				printf("%s", buf);
			}
		}
		
		printf("\n\n");
	}
	
	FREE_LINKED_LIST(monitor_addrs);
	
	printf("Ending...\n");
	
	/*
	// Wait for message
	struct sockaddr remote_addr;
	int len;
	char buf[256];
	socklen_t addr_size = sizeof remote_addr;
	while ((len = recvfrom(sockfd, buf, 255, 0, &remote_addr, &addr_size)) != -1)
	{
		
		// Send response
		char out[16];
		sprintf(out, "%d\n", resp);
		if (sendto(sockfd, &out, strlen(out), 0, &remote_addr, addr_size) == -1)
			printf("Failed to send message.\n");
	}
	*/
	
	// Close socket
	close_listener();
}
