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
#include "shim.h"
#include "table.h"

#include "../tests/sisis_api.h"
#include "../tests/sisis_process_types.h"

#define VERSION 1

int sockfd = -1;

void terminate(int signal)
{
	printf("Terminating...\n");
	if (sockfd != -1)
	{
		printf("Closing remove connection socket...\n");
		close(sockfd);
	}
	exit(0);
}

int main (int argc, char ** argv)
{
	// Sleep time
	unsigned int sleep_time = 5;
	if (argc == 2)
		sscanf(argv[1], "%u", &sleep_time);
	
	printf("Opening socket...\n");
	
	// Create socket
	if ((sockfd = socket(AF_INET6, SOCK_DGRAM, 0)) == -1)
	{
		printf("Failed to open socket.\n");
		exit(1);
	}
	
	// Set up signal handling
	signal(SIGABRT, terminate);
	signal(SIGTERM, terminate);
	signal(SIGINT, terminate);
	
	
	// Seed random number generator
	srand(time(NULL));
	
	// Loop forever
	while (1)
	{
		// Create random tables
		int i;
		
		// Table 1
		printf("Building table 1...\n");
		demo_table1_entry table1[MAX_TABLE_SIZE];
		short user_id_pool[100];
		for (i = 0; i < 100; i++)
			user_id_pool[i] = 0;
		for (i = 0; i < MAX_TABLE_SIZE; i++)
		{
			do {
				table1[i].user_id = rand() % 100;
			} while (user_id_pool[table1[i].user_id]);
			user_id_pool[table1[i].user_id] = 1;
			
			sprintf(table1[i].name, "User #%d", i+1);
		}
		
		// Table 2
		printf("Building table 2...\n");
		demo_table2_entry table2[MAX_TABLE_SIZE];
		for (i = 0; i < 100; i++)
			user_id_pool[i] = 0;
		for (i = 0; i < MAX_TABLE_SIZE; i++)
		{
			do {
				table2[i].user_id = rand() % 100;
			} while (user_id_pool[table2[i].user_id]);
			user_id_pool[table2[i].user_id] = 1;
			
			table2[i].gender = (i % 3) ? 'M' : 'F';
		}
		
		// Send real result to voter
		send_real_result_to_voter(table1, MAX_TABLE_SIZE, table2, MAX_TABLE_SIZE);
		
		// Serialize
		printf("Serializing...\n");
		char buf[RECV_BUFFER_SIZE];
		int buflen, buflen2;
		buflen = serialize_table1(table1, MAX_TABLE_SIZE, buf, RECV_BUFFER_SIZE);
		if (buflen != -1)
			buflen2 = serialize_table2(table2, MAX_TABLE_SIZE, buf+buflen, RECV_BUFFER_SIZE - buflen);
		if (buflen == -1 || buflen2 == -1)
			printf("Failed to serialize tables.\n");
		else
		{
			// Find all sort processes
			printf("Searching for sort processes...\n");
			char sort_addr[INET6_ADDRSTRLEN+1];
			sisis_create_addr(sort_addr, (uint64_t)SISIS_PTYPE_DEMO1_SORT, (uint64_t)1, (uint64_t)0, (uint64_t)0, (uint64_t)0);
			struct prefix_ipv6 sort_prefix = sisis_make_ipv6_prefix(sort_addr, 42);
			struct list * sort_addrs = get_sisis_addrs_for_prefix(&sort_prefix);
			if (sort_addrs == NULL || sort_addrs->size == 0)
				printf("No sort processes found.\n");
			else
			{
				// Send to all sort processes
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
					sockaddr.sin6_port = htons(SORT_PORT);
					sockaddr.sin6_addr = *remote_addr;
					
					printf("Sending data to sort process...\n");
					if (sendto(sockfd, buf, buflen+buflen2, 0, (struct sockaddr *)&sockaddr, sockaddr_size) == -1)
						printf("Failed to send message.  Error: %i\n", errno);
				}
				
				// Free memory
				FREE_LINKED_LIST(sort_addrs);
			}
		}
		
		// Sleep
		sleep(sleep_time);
	}
	
	// Close socket
	if (sockfd != -1)
		close(sockfd);
}

void send_real_result_to_voter(demo_table1_entry * table1, int rows1, demo_table2_entry * table2, int rows2)
{
	// Sort tables
	sort_table1_by_user_id(table1, rows1);
	sort_table2_by_user_id(table2, rows2);
	
	// Join
	demo_merge_table_entry join_table[MAX_TABLE_SIZE];
	int rows = merge_join(table1, rows1, table2, rows2, join_table, MAX_TABLE_SIZE);
	
	// Serialize
	char buf[SEND_BUFFER_SIZE];
	int buflen = serialize_join_table(join_table, rows, buf, SEND_BUFFER_SIZE);
	if (buflen == -1)
		printf("Failed to serialize table.\n");
	else
	{
		// Find all voter processes
		struct list * voter_addrs = get_processes_by_type((uint64_t)SISIS_PTYPE_DEMO1_VOTER);
		if (voter_addrs == NULL || voter_addrs->size == 0)
			printf("No voter processes found.\n");
		else
		{
			// Send to all voter processes
			struct listnode * node;
			LIST_FOREACH(voter_addrs, node)
			{
				// Get address
				struct in6_addr * remote_addr = (struct in6_addr *)node->data;
				
				// Set up socket info
				struct sockaddr_in6 sockaddr;
				int sockaddr_size = sizeof(sockaddr);
				memset(&sockaddr, 0, sockaddr_size);
				sockaddr.sin6_family = AF_INET6;
				sockaddr.sin6_port = htons(VOTER_ANSWER_PORT);
				sockaddr.sin6_addr = *remote_addr;
				
				if (sendto(sockfd, buf, buflen, 0, (struct sockaddr *)&sockaddr, sockaddr_size) == -1)
					printf("Failed to send message.  Error: %i\n", errno);
			}
			
			// Free memory
			FREE_LINKED_LIST(voter_addrs);
		}
	}
}