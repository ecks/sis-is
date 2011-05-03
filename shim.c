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

#include "shim.h"
#include "table.h"

#include "../tests/sisis_api.h"
#include "../tests/sisis_process_types.h"

#define VERSION 1

int sockfd = -1;
uint64_t ptype, host_num, pid;
uint64_t timestamp;

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
	// Get start time
	timestamp = time(NULL);
	
	// Check number of args
	if (argc != 2)
	{
		printf("Usage: %s <host_num>\n", argv[0]);
		exit(1);
	}
	
	// Get host number
	sscanf (argv[1], "%llu", &host_num);
	char sisis_addr[INET6_ADDRSTRLEN+1];
	
	// Get pid
	pid = getpid();
	
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
	
	/* Test Voter */
	// Table 1 bad
	demo_table1_entry table1_bad[MAX_TABLE_SIZE];
	for (i = 0; i < 100; i++)
		user_id_pool[i] = 0;
	for (i = 0; i < MAX_TABLE_SIZE; i++)
	{
		do {
			table1_bad[i].user_id = rand() % 100;
		} while (user_id_pool[table1_bad[i].user_id]);
		user_id_pool[table1_bad[i].user_id] = 1;
		
		sprintf(table1_bad[i].name, "User #%d", i+1);
	}
	
	// Setup list of tables
	table_group_t table1_group;
	// 1
	table1_group.first = malloc(sizeof(table_group_item_t));
	table_group_item_t * cur_item = table1_group.first;
	cur_item->table = (void *)table1;
	cur_item->table_size = MAX_TABLE_SIZE;
	// 2
	cur_item->next = malloc(sizeof(table_group_item_t));
	cur_item = cur_item.next;
	cur_item->table = (void *)table1_bad;
	cur_item->table_size = MAX_TABLE_SIZE;
	// 3
	cur_item->next = malloc(sizeof(table_group_item_t));
	cur_item = cur_item.next;
	cur_item->table = (void *)table1;
	cur_item->table_size = MAX_TABLE_SIZE;
	cur_item->next = NULL;
	
	// Vote
	demo_table1_entry * table1_voted = NULL;
	cur_item = (table_group_item_t *)table1_vote(&table1_group);
	if (cur_item)
		table1_voted = cur_item;
	
	// Print
	printf("GOOD TABLE\n");
	for (i = 0; i < MAX_TABLE_SIZE; i++)
		printf("User Id: %d\tGender: %c\n", table1[i].user_id, table1[i].gender);
	printf("BAD TABLE\n");
	for (i = 0; i < MAX_TABLE_SIZE; i++)
		printf("User Id: %d\tGender: %c\n", table1_bad[i].user_id, table1_bad[i].gender);
	printf("VOTED TABLE\n");
	if (!table1_voted)
		printf("No table.\n");
	else
		for (i = 0; i < MAX_TABLE_SIZE; i++)
			printf("User Id: %d\tGender: %c\n", table1_voted[i].user_id, table1_voted[i].gender);
	
	exit(0);
	
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
	
	// Close socket
	if (sockfd != -1)
		close(sockfd);
}