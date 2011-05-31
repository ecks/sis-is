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
#include "sort.h"
#include "redundancy.h"
#include "table.h"

#include "../tests/sisis_api.h"
#include "../tests/sisis_process_types.h"

#ifdef BUBBLE_SORT
	#define VERSION 2
#else
	#define VERSION 1
#endif

// Setup tables
// We only need one set of tables since there is a single shim
demo_table1_entry table1[MAX_TABLE_SIZE];
int table1_size;
demo_table2_entry table2[MAX_TABLE_SIZE];
int table2_size;

/** Bubble sort */
void bubble_sort(void * base, size_t num, size_t size, int (*comparator) (const void *, const void *))
{
	void * swap_elem = malloc(size);
	size_t i;
	short swapped;
	do
	{
		swapped = 0;
		for (i = 0; i < num - 1; i++)
		{
			if (comparator(base+i*size, base+(i+1)*size) > 0)
			{
				// Swap
				memcpy(swap_elem, base+i*size, size);
				memcpy(base+i*size, base+(i+1)*size, size);
				memcpy(base+(i+1)*size, swap_elem, size);
				swapped = 1;
			}
		}
	}while (swapped);
}

int main (int argc, char ** argv)
{
	// Start main loop
	redundancy_main((uint64_t)SISIS_PTYPE_DEMO1_SORT, (uint64_t)VERSION, SORT_PORT, 0, process_input, vote_and_process, NULL, REDUNDANCY_MAIN_FLAG_SINGLE_INPUT, argc, argv);
}

/** Process input from a single process. */
void process_input(char * buf, int buflen)
{
	// Deserialize
	int bytes_used;
	table1_size = deserialize_table1(table1, MAX_TABLE_SIZE, buf, buflen, &bytes_used);
	table2_size = deserialize_table2(table2, MAX_TABLE_SIZE, buf+bytes_used, buflen-bytes_used, NULL);
#ifdef DEBUG
	printf("Table 1 Rows: %d\n", table1_size);
	printf("Table 2 Rows: %d\n", table2_size);
#endif
}

/** Vote on input and process */
void vote_and_process()
{
	// No need to vote since there is only one shim
	// Process tables
	process_tables(table1, table1_size, table2, table2_size);
}

/** Sort tables and send results to join processes. */
void process_tables(demo_table1_entry * table1, int rows1, demo_table2_entry * table2, int rows2)
{
	// Sort tables
	sort_table1_by_user_id(table1, rows1);
	sort_table2_by_user_id(table2, rows2);
	
#ifdef DEBUG
	// Print
	for (i = 0; i < rows1; i++)
		printf("User Id: %d\tName: %s\n", table1[i].user_id, table1[i].name);
	for (i = 0; i < rows2; i++)
		printf("User Id: %d\tGender: %c\n", table2[i].user_id, table2[i].gender);
#endif
	
	// Serialize
	char buf[SEND_BUFFER_SIZE];
	int buflen2;
	int buflen = serialize_table1(table1, rows1, buf, SEND_BUFFER_SIZE);
	if (buflen != -1)
		buflen2 = serialize_table2(table2, rows2, buf+buflen, SEND_BUFFER_SIZE - buflen);
	if (buflen == -1 || buflen2 == -1)
		printf("Failed to serialize tables.\n");
	else
	{
		// Find all join processes
		char join_addr[INET6_ADDRSTRLEN+1];
		sisis_create_addr(join_addr, (uint64_t)SISIS_PTYPE_DEMO1_JOIN, (uint64_t)1, (uint64_t)0, (uint64_t)0, (uint64_t)0);
		struct prefix_ipv6 join_prefix = sisis_make_ipv6_prefix(join_addr, 42);
		struct list * join_addrs = get_sisis_addrs_for_prefix(&join_prefix);
		if (join_addrs == NULL || join_addrs->size == 0)
			printf("No join processes found.\n");
		else
		{
			// Send to all join processes
			struct listnode * node;
			LIST_FOREACH(join_addrs, node)
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
				
				if (sendto(sockfd, buf, buflen+buflen2, 0, (struct sockaddr *)&sockaddr, sockaddr_size) == -1)
					printf("Failed to send message.  Error: %i\n", errno);
			}
			
			// Free memory
			FREE_LINKED_LIST(join_addrs);
		}
	}
}