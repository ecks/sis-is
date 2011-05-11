/*
 * SIS-IS Demo program.
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

#include <time.h>

#include "demo.h"

#include "../tests/sisis_api.h"
#include "../tests/sisis_process_types.h"

#define VERSION 1

int sockfd = -1;

void stop_redundancy_for_process_type(uint64_t proc_type)
{
	// Find all sort processes
	printf("Searching for processes...\n");
	char addr[INET6_ADDRSTRLEN+1];
	sisis_create_addr(addr, proc_type, (uint64_t)1, (uint64_t)0, (uint64_t)0, (uint64_t)0);
	struct prefix_ipv6 prefix = sisis_make_ipv6_prefix(addr, 42);
	struct list * addrs = get_sisis_addrs_for_prefix(&prefix);
	if (addrs == NULL || addrs->size == 0)
		printf("No processes found.\n");
	else
	{
		// Send message to all processes
		struct listnode * node;
		LIST_FOREACH(sort_addrs, node)
		{
			// Get address
			struct in6_addr * remote_addr = (struct in6_addr *)node->data;
			
			// Set up socket info
			struct sockaddr_in6 sockaddr;
			int sockaddr_size = sizeof(sockaddr);
			memset(&sockaddr, 0, sockaddr_size);
			sockaddr.sin6_family = AF_INET6;
			sockaddr.sin6_port = htons(STOP_REDUNDANCY_PORT);
			sockaddr.sin6_addr = *remote_addr;
			
			printf("Sending data to process...\n");
			if (sendto(sockfd, PASSWORD, strlen(PASSWORD), 0, (struct sockaddr *)&sockaddr, sockaddr_size) == -1)
				printf("Failed to send message.  Error: %i\n", errno);
		}
		
		// Free memory
		FREE_LINKED_LIST(sort_addrs);
	}
}

int main (int argc, char ** argv)
{
	printf("Opening socket...\n");
	
	stop_redundancy_for_process_type((uint64_t)SISIS_PTYPE_DEMO1_SORT);
	stop_redundancy_for_process_type((uint64_t)SISIS_PTYPE_DEMO1_JOIN);
	
	// Create socket
	if ((sockfd = socket(AF_INET6, SOCK_DGRAM, 0)) == -1)
	{
		printf("Failed to open socket.\n");
		exit(1);
	}
	
	
	
	// Close socket
	if (sockfd != -1)
		close(sockfd);
}