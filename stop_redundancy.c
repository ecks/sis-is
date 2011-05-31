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

void stop_redundancy_for_process_type(uint64_t proc_type, uint64_t proc_version)
{
	// Find all sort processes
	printf("Searching for processes...\n");
	char addr[INET6_ADDRSTRLEN+1];
	sisis_create_addr(addr, proc_type, proc_version, (uint64_t)0, (uint64_t)0, (uint64_t)0);
	struct prefix_ipv6 prefix = sisis_make_ipv6_prefix(addr, 42);
	struct list * addrs = get_sisis_addrs_for_prefix(&prefix);
	if (addrs == NULL || addrs->size == 0)
		printf("No processes found.\n");
	else
	{
		// Send message to all processes
		struct listnode * node;
		LIST_FOREACH(addrs, node)
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
		
		// Kill all processes
		struct listnode * node;
		LIST_FOREACH(addrs, node)
		{
			// Get address
			struct in6_addr * remote_addr = (struct in6_addr *)node->data;
			
			// Get system id and PID
			uint64_t kill_sys_id, kill_pid;
			if (inet_ntop(AF_INET6, remote_addr, addr, INET6_ADDRSTRLEN) != NULL)
			{
				if (get_sisis_addr_components(addr, NULL, NULL, NULL, NULL, &kill_sys_id, &kill_pid, NULL) == 0)
				{
					// Find machine monitor process for the system on which we are killing the process
					char mm_addr[INET6_ADDRSTRLEN+1];
					sisis_create_addr(mm_addr, (uint64_t)SISIS_PTYPE_MACHINE_MONITOR, (uint64_t)1, kill_sys_id, (uint64_t)0, (uint64_t)0);
					struct prefix_ipv6 mm_prefix = sisis_make_ipv6_prefix(mm_addr, 74);
					struct list * mm_addrs = get_sisis_addrs_for_prefix(&mm_prefix);
					if (mm_addrs != NULL)
					{
						// Send to all mm processes
						struct listnode * node;
						LIST_FOREACH(mm_addrs, node)
						{
							// Set up socket info
							struct sockaddr_in6 sockaddr;
							int sockaddr_size = sizeof(sockaddr);
							memset(&sockaddr, 0, sockaddr_size);
							sockaddr.sin6_family = AF_INET6;
							sockaddr.sin6_port = htons(STOP_REDUNDANCY_PORT);
							sockaddr.sin6_addr = *(struct in6_addr *)node->data;
							
							// Construct message
							char buf[64];
							sprintf(buf, "kill %llu", kill_pid);
							
							// Send message
							printf("Killing process #%llu on host #%llu ...\n", kill_pid, kill_sys_id);
							if (sendto(sockfd, buf, strlen(buf), 0, (struct sockaddr *)&sockaddr, sockaddr_size) == -1)
								printf("Failed to send message.  Error: %i\n", errno);
						}
						
						// Free memory
						FREE_LINKED_LIST(mm_addrs);
					}
				}
			}
		}
		
		// Free memory
		FREE_LINKED_LIST(addrs);
	}
}

int main (int argc, char ** argv)
{
	printf("Opening socket...\n");
	
	// Create socket
	if ((sockfd = socket(AF_INET6, SOCK_DGRAM, 0)) == -1)
	{
		printf("Failed to open socket.\n");
		exit(1);
	}
	
	if (argc < 2 || strcmp(argv[1], "sort") == 0)
		stop_redundancy_for_process_type((uint64_t)SISIS_PTYPE_DEMO1_SORT, 1llu);
	if (argc < 2 || strcmp(argv[1], "sortv2") == 0)
		stop_redundancy_for_process_type((uint64_t)SISIS_PTYPE_DEMO1_SORT, 2llu);
	if (argc < 2 || strcmp(argv[1], "sort") == 0)
		stop_redundancy_for_process_type((uint64_t)SISIS_PTYPE_DEMO1_JOIN, 1llu);
	
	// Close socket
	if (sockfd != -1)
		close(sockfd);
}