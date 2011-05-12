/*
 * SIS-IS Demo program.
 * Stephen Sigwart
 * University of Delaware
 */

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>

#include <time.h>

#include "demo.h"
#include "join.h"
#include "redundancy.h"
#include "table.h"

#include "../remote_spawn/remote_spawn.h"
#include "../tests/sisis_api.h"
#include "../tests/sisis_process_types.h"
#include "../tests/sisis_addr_format.h"

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

#define VERSION 1

// Setup list of tables
table_group_t table1_group;
table_group_item_t * cur_table1_item;
table_group_t table2_group;
table_group_item_t * cur_table2_item;

int main (int argc, char ** argv)
{
	// Setup list of tables
	// Table 1
	table1_group.first = NULL;
	// Table 2
	table2_group.first = NULL;
	
	// Start main loop
	redundancy_main((uint64_t)SISIS_PTYPE_DEMO1_JOIN, (uint64_t)VERSION, JOIN_PORT, (uint64_t)SISIS_PTYPE_DEMO1_SORT, process_input, vote_and_process, argc, argv);
}

/** Process input from a single process. */
void process_input(char * buf, int buflen)
{
	// Allocate memory
	if (table1_group.first == NULL)
	{
		// Table 1
		cur_table1_item = malloc(sizeof(*cur_table1_item));
		table1_group.first = cur_table1_item;
		// Table 2
		cur_table2_item = malloc(sizeof(*cur_table2_item));
		table2_group.first = cur_table2_item;
	}
	else
	{
		// Table 1
		cur_table1_item->next = malloc(sizeof(*cur_table1_item->next));
		cur_table1_item = cur_table1_item->next;
		// Table 2
		cur_table2_item->next = malloc(sizeof(*cur_table2_item->next));
		cur_table2_item = cur_table2_item->next;
	}
	
	// Check memory
	if (cur_table1_item == NULL || cur_table2_item == NULL)
	{ printf("Out of memory.\n"); exit(0); }
	cur_table1_item->table = malloc(sizeof(demo_table1_entry)*MAX_TABLE_SIZE);
	cur_table1_item->next = NULL;
	cur_table2_item->table = malloc(sizeof(demo_table2_entry)*MAX_TABLE_SIZE);
	cur_table2_item->next = NULL;
	
	// Check memory
	if (cur_table1_item->table == NULL || cur_table2_item->table == NULL)
	{ printf("Out of memory.\n"); exit(0); }
	
	// Deserialize
	int bytes_used;
	cur_table1_item->table_size = deserialize_table1(cur_table1_item->table, MAX_TABLE_SIZE, buf, buflen, &bytes_used);
	cur_table2_item->table_size = deserialize_table2(cur_table2_item->table, MAX_TABLE_SIZE, buf+bytes_used, buflen-bytes_used, NULL);
#ifdef DEBUG
	printf("Table 1 Rows: %d\n", cur_table1_item->table_size);
	printf("Table 2 Rows: %d\n", cur_table2_item->table_size);
#endif
}

/** Vote on input and process */
void vote_and_process()
{
	// Vote
	table_group_item_t * table1_item = table1_vote(&table1_group);
	table_group_item_t * table2_item = table2_vote(&table2_group);
	if (!table1_item || !table2_item)
		printf("Failed to vote on tables.\n");
	else
	{
		// Process tables
		process_tables(table1_item->table, table1_item->table_size, table2_item->table, table2_item->table_size);
	}
}

/** Join tables and send result to voter processes. */
void process_tables(demo_table1_entry * table1, int rows1, demo_table2_entry * table2, int rows2)
{
	int i;
	
	// Join
	demo_merge_table_entry join_table[MAX_TABLE_SIZE];
	int rows = merge_join(table1, rows1, table2, rows2, join_table, MAX_TABLE_SIZE);
	
#ifdef DEBUG
	// Print
	if (rows == -1)
		printf("Join error.\n");
	else
	{
		printf("Joined Rows: %d\n", rows);
		//for (i = 0; i < rows; i++)
			//printf("User Id: %d\tName: %s\tGender: %c\n", join_table[i].user_id, join_table[i].name, join_table[i].gender);
	}
#endif
	
	// Serialize
	char buf[SEND_BUFFER_SIZE];
	int buflen = serialize_join_table(join_table, rows, buf, SEND_BUFFER_SIZE);
	if (buflen == -1)
		printf("Failed to serialize table.\n");
	else
	{
		// Find all voter processes
		char voter_addr[INET6_ADDRSTRLEN+1];
		sisis_create_addr(voter_addr, (uint64_t)SISIS_PTYPE_DEMO1_VOTER, (uint64_t)1, (uint64_t)0, (uint64_t)0, (uint64_t)0);
		struct prefix_ipv6 voter_prefix = sisis_make_ipv6_prefix(voter_addr, 42);
		struct list * voter_addrs = get_sisis_addrs_for_prefix(&voter_prefix);
		if (voter_addrs == NULL || voter_addrs->size == 0)
			printf("No voter processes found.\n");
		else
		{
			// Send to all join processes
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
				sockaddr.sin6_port = htons(JOIN_PORT);
				sockaddr.sin6_addr = *remote_addr;
				
				if (sendto(sockfd, buf, buflen, 0, (struct sockaddr *)&sockaddr, sockaddr_size) == -1)
					printf("Failed to send message.  Error: %i\n", errno);
			}
			
			// Free memory
			FREE_LINKED_LIST(voter_addrs);
		}
	}
}