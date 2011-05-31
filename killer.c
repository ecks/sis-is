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
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>

#include <time.h>

#include "demo.h"
#include "shim.h"
#include "table.h"

#include "../tests/sisis_api.h"
#include "../tests/sisis_process_types.h"

#define VERSION 1

int sockfd = -1;

void close_listener()
{
	if (sockfd != -1)
	{
		printf("Closing remove connection socket...\n");
		close(sockfd);
	}
}

void terminate(int signal)
{
	printf("Terminating...\n");
	close_listener();
	exit(0);
}

int main (int argc, char ** argv)
{
	// Sleep time
	unsigned long usleep_time = 5000000;	// 5 sec
	
	// Number of processes to kill at each interval
	int num_proc_to_kill = 1;
		
	// Parse args
	int arg = 1;
	for (; arg < argc; arg++)
	{
		// Help/Usage
		if (strcmp(argv[arg], "-h") || strcmp(argv[arg], "--help"))
		{
			printf("Usage: %s [options]\n", argv[0]);
			printf("Options:\n");
			printf("\t-i <sec>\t(--interval) Number of seconds between killing processes.\n");
			printf("\t-n <num>\t(--num) Number of processes to kill at each interval.\n");
			exit(0);
		}
		// Sleep time
		else if (strcmp(argv[arg], "-i") || strcmp(argv[arg], "--interval"))
		{
			arg++;
			float tmp_sleep;
			if (arg < argc && sscanf(argv[arg], "%f", &tmp_sleep) == 1)
				usleep_time = tmp_sleep * 1000000;
			else
			{
				printf("Expecting interval after %s.\n", argv[arg-1]);
				exit(1);
			}
		}
		// Number of processes to kill at each interval
		else if (strcmp(argv[arg], "-i") || strcmp(argv[arg], "--interval"))
		{
			arg++;
			if (arg < argc && sscanf(argv[arg], "%d", &num_proc_to_kill) == 1)
			{}
			else
			{
				printf("Expecting number after %s.\n", argv[arg-1]);
				exit(1);
			}
		}
	}
	
	// Set up signal handling
	signal(SIGABRT, terminate);
	signal(SIGTERM, terminate);
	signal(SIGINT, terminate);
	
	// Create socket
	if ((sockfd = socket(AF_INET6, SOCK_DGRAM, 0)) == -1)
	{
		printf("Failed to open socket.\n");
		exit(1);
	}
	
	// Seed random number generator
	srand(time(NULL));
	
	// Loop forever
	while (1)
	{
		// Kill appropriate number of processes
		int i;
		for (i = 0; i < num_proc_to_kill; i++)
		{
			// Get pid to kill
			uint64_t kill_pid = 0;
			uint64_t kill_sys_id = 0;
			
			// Address of machine monitor needed to kill the process
			struct in6_addr mm_remote_addr;
			short mm_found = 0;
			
			do
			{
				// Randomly find a process to kill
				do
				{
					// Randomly choose between sort and join processes to kill
					uint64_t kill_ptype = (uint64_t)SISIS_PTYPE_DEMO1_SORT;
					if (rand() % 2 == 0)
						kill_ptype = (uint64_t)SISIS_PTYPE_DEMO1_JOIN;
						
					// Find all processes
					printf("Searching for processes to kill...\n");
					char addr[INET6_ADDRSTRLEN];
					sisis_create_addr(addr, kill_ptype, (uint64_t)0, (uint64_t)0, (uint64_t)0, (uint64_t)0);
					struct prefix_ipv6 prefix = sisis_make_ipv6_prefix(addr, 37);
					struct list * addrs = get_sisis_addrs_for_prefix(&prefix);
					if (addrs != NULL)
					{
						struct listnode * node;
						LIST_FOREACH(addrs, node)
						{
							// Randomly choose 1
							if (rand() % addrs->size == 0)
							{
								// Get address
								struct in6_addr * remote_addr = (struct in6_addr *)node->data;
								
								// Get system id and PID
								if (inet_ntop(AF_INET6, remote_addr, addr, INET6_ADDRSTRLEN) != NULL)
									if (get_sisis_addr_components(addr, NULL, NULL, NULL, NULL, &kill_sys_id, &kill_pid, NULL) == 0)
										break;
							}
						}
						
						// Free memory
						FREE_LINKED_LIST(addrs);
					}
				} while (kill_pid == 0);
				
				// Find machine monitor process for the system on which we are killing the process
				printf("Searching for machine monitor processes...\n");
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
						// Get address
						memcpy(&mm_remote_addr, (struct in6_addr *)node->data, sizeof mm_remote_addr);
						
						// Mark as found
						mm_found = 1;
					}
					
					// Free memory
					FREE_LINKED_LIST(mm_addrs);
				}
			} while (!mm_found);
			
			// Set up socket info
			struct sockaddr_in6 sockaddr;
			int sockaddr_size = sizeof(sockaddr);
			memset(&sockaddr, 0, sockaddr_size);
			sockaddr.sin6_family = AF_INET6;
			sockaddr.sin6_port = htons(MACHINE_MONITOR_PORT);
			sockaddr.sin6_addr = mm_remote_addr;
			
			// Construct message
			char buf[64];
			sprintf(buf, "kill %llu", kill_pid);
			
			// Send message
			printf("Killing process #%llu on host #%llu ...\n", kill_pid, kill_sys_id);
			if (sendto(sockfd, buf, strlen(buf), 0, (struct sockaddr *)&sockaddr, sockaddr_size) == -1)
				printf("Failed to send message.  Error: %i\n", errno);
		}
		
		// Sleep
		usleep(usleep_time);
	}
	
	// Close socket
	if (sockfd != -1)
		close(sockfd);
}