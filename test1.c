/*
 * SIS-IS Test program.
 * Stephen Sigwart
 * University of Delaware
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

#include "sisis_api.h"

int sockfd = -1, con = -1;

void terminate(int signal)
{
	printf("Terminating...\n");
	if (sockfd != -1)
	{
		printf("Closing listening socket...\n");
		close(sockfd);
	}
	if (con != -1)
	{
		printf("Closing remove connection socket...\n");
		close(con);
	}
	exit(0);
}

int main (int argc, char ** argv)
{
	
	struct addrinfo hints, *addr;
	
	// Check if the IP address and port are set
	if (argc != 4)
	{
		printf("Usage: %s <host_num> <process_type> <port>\n", argv[0]);
		exit(1);
	}
	
	// Get process type and host number
	int ptype, host_num;
	sscanf (argv[1], "%d", &ptype);
	sscanf (argv[2], "%d", &host_num);
	char sisis_addr[INET_ADDRSTRLEN];
	
	// Register address
	if (sisis_register(ptype, host_num, sisis_addr) != 0)
	{
		printf("Failed to register SIS-IS address.\n");
		exit(1);
	}
	
	// Status
	printf("Opening socket at %s on port %s.\n", sisis_addr, argv[3]);
	
	// Set up socket address info
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;	// IPv4
	hints.ai_socktype = SOCK_STREAM;
	getaddrinfo(sisis_addr, argv[3], &hints, &addr);
	
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
		exit(2);
	}
	
	// Status message
	inet_ntop(AF_INET, &((struct sockaddr_in *)(addr->ai_addr))->sin_addr, sisis_addr, INET_ADDRSTRLEN);
	printf("Socket opened at %s on port %u.\n", sisis_addr, ntohs(((struct sockaddr_in *)(addr->ai_addr))->sin_port));
	
	// Listen on the socket
	if (listen(sockfd, 5) == -1)
	{
		printf("Failed to listen on socket.\n");
		exit(3);
	}
	
	// Set up signal handling
	signal(SIGABRT, terminate);
	signal(SIGTERM, terminate);
	signal(SIGINT, terminate);
	
	// Wait for connection
	struct sockaddr_storage remote_addr;
	int len;
	socklen_t addr_size = sizeof remote_addr;
	while ((con = accept(sockfd, (struct sockaddr *)&remote_addr, &addr_size)) != -1)
	{
		char buf[1024];
		if ((len = recv(con, buf, 1023, 0)) > 0)
		{
			buf[len] = '\0';
			printf("Received \"%s\".\n", buf);
			
			// Send data back
			if (send(con, buf, len, 0) == -1)
				printf("Failed to send message.\n");
		}
		else
			printf("Failed to receive message.\n");
		
		// Close connection
		close(con);
		con = -1;
	}
	
	// Close socket
	close(sockfd);
	sockfd = -1;
}