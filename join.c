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

#include "join.h"
#include "table.h"

#include "../tests/sisis_api.h"
#include "../tests/sisis_process_types.h"

#define DEBUG

#define VERSION 1
int sockfd = -1, con = -1;
uint64_t ptype, host_num, pid;
uint64_t timestamp;

void close_listener()
{
	if (sockfd != -1)
	{
		printf("Closing listening socket...\n");
		close(sockfd);
		
		// Unregister
		sisis_unregister(NULL, (uint64_t)SISIS_PTYPE_DEMO1_JOIN, (uint64_t)VERSION, host_num, pid, timestamp);
		
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
	
	// Register address
	if (sisis_register(sisis_addr, (uint64_t)SISIS_PTYPE_DEMO1_JOIN, (uint64_t)VERSION, host_num, pid, timestamp) != 0)
	{
		printf("Failed to register SIS-IS address.\n");
		exit(1);
	}
	
	// Status
	printf("Opening socket at %s on port %i.\n", sisis_addr, JOIN_PORT);
	
	// Set up socket address info
	struct addrinfo hints, *addr;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET6;	// IPv6
	hints.ai_socktype = SOCK_DGRAM;
	char port_str[8];
	sprintf(port_str, "%u", JOIN_PORT);
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
	inet_ntop(AF_INET6, &((struct sockaddr_in6 *)(addr->ai_addr))->sin6_addr, sisis_addr, INET6_ADDRSTRLEN);
	printf("Socket opened at %s on port %u.\n", sisis_addr, ntohs(((struct sockaddr_in *)(addr->ai_addr))->sin_port));
	
	// Set up signal handling
	signal(SIGABRT, terminate);
	signal(SIGTERM, terminate);
	signal(SIGINT, terminate);
	
	// TODO: Thread to be sure that there are enough processes
	
	// Setup list of tables
	int num_table1s = 0;
	table_group_t table1_group;
	table1_group.first = NULL;
	table_group_item_t * cur_table1_item = NULL;
	
	// Set of sockets for select call
	fd_set socks;
	FD_ZERO(&socks);
	FD_SET(sockfd, &socks);
	
	// Timeout information for select call
	struct timeval select_timeout;
	struct timeval start_time, cur_time, tmp1, tmp2;
	
	// Number of sort processes
	int sort_count;
	
	
	// Wait for message
	struct sockaddr_in6 remote_addr;
	int buflen;
	char buf[RECV_BUFFER_SIZE];
	socklen_t addr_size = sizeof remote_addr;
	while ((buflen = recvfrom(sockfd, buf, RECV_BUFFER_SIZE, 0, (struct sockaddr *)&remote_addr, &addr_size)) != -1)
	{
		// TODO: Remove
		demo_table2_entry table2[MAX_TABLE_SIZE];
		int rows2 = 0;
		
		do
		{
			// Setup table
			if (num_table1s == 0)
			{
				// TODO: Check for NULL
				cur_table1_item = malloc(sizeof(table_group_item_t));
				table1_group.first = cur_table1_item;
				
				// Set socket select timeout
				select_timeout.tv_sec = GATHER_RESULTS_TIMEOUT_USEC / 1000000;
				select_timeout.tv_usec = GATHER_RESULTS_TIMEOUT_USEC % 1000000;
				
				// Get start time
				gettimeofday(&start_time, NULL);
			}
			else
			{
				// TODO: Check for NULL
				cur_table1_item->next = malloc(sizeof(table_group_item_t));
				cur_table1_item = cur_table1_item->next;
				
				// Determine new socket select timeout
				gettimeofday(&cur_time, NULL);
				timersub(&cur_time, &start_time, &tmp1);
				timersub(&select_timeout, &tmp1, &tmp2);
				select_timeout.tv_sec = tmp2.tv_sec;
				select_timeout.tv_usec = tmp2.tv_usec;
			}
			cur_table1_item->table = malloc(sizeof(demo_table1_entry)*MAX_TABLE_SIZE);
			cur_table1_item->next = NULL;
			num_table1s++;
			
			// Deserialize
			int bytes_used;
			cur_table1_item->table_size = deserialize_table1(cur_table1_item->table, MAX_TABLE_SIZE, buf, buflen, &bytes_used);
			
			// Deserialize
			rows2 = deserialize_table2(table2, MAX_TABLE_SIZE, buf+bytes_used, buflen-bytes_used, NULL);
	#ifdef DEBUG
			printf("Table 1 Rows: %d\n", cur_table1_item->table_size);
			printf("Table 2 Rows: %d\n", rows2);
	#endif

			// Check how many sort processes there are
			sort_count = get_sort_process_count();
	
		} while(num_table1s < sort_count && select(sockfd+1, &socks, NULL, NULL, &select_timeout) > 0);
		
		// Vote
		printf("Voting...\n");
		table_group_item_t * cur_item = table1_vote(&table1_group);
		if (!cur_item)
			printf("Failed to vote on table 1.\n");
		else
		{
			// Process tables
			process_tables(cur_item->table, cur_item->table_size, table2, rows2);
		}
	}
	
	// Close socket
	close_listener();
}

/** Count number of sort processes */
int get_sort_process_count()
{
	int cnt = 0;
	
	char addr[INET6_ADDRSTRLEN+1];
	sisis_create_addr(addr, (uint64_t)SISIS_PTYPE_LEADER_ELECTOR, (uint64_t)1, (uint64_t)0, (uint64_t)0, (uint64_t)0);
	struct prefix_ipv6 prefix = sisis_make_ipv6_prefix(addr, 42);
	struct list * addrs = get_sisis_addrs_for_prefix(&prefix);
	if (addrs != NULL)
	{
		cnt = addrs->size;
		
		// Free memory
		if (addrs)
			FREE_LINKED_LIST(addrs);
	}
	
	return cnt;
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
		for (i = 0; i < rows; i++)
			printf("User Id: %d\tName: %s\tGender: %c\n", join_table[i].user_id, join_table[i].name, join_table[i].gender);
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