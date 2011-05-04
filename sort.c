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

#include "sort.h"
#include "table.h"

#include "../tests/sisis_api.h"
#include "../tests/sisis_process_types.h"

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
		sisis_unregister(NULL, (uint64_t)SISIS_PTYPE_DEMO1_SORT, (uint64_t)VERSION, host_num, pid, timestamp);
		
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
	if (sisis_register(sisis_addr, (uint64_t)SISIS_PTYPE_DEMO1_SORT, (uint64_t)VERSION, host_num, pid, timestamp) != 0)
	{
		printf("Failed to register SIS-IS address.\n");
		exit(1);
	}
	
	// Status
	printf("Opening socket at %s on port %i.\n", sisis_addr, SORT_PORT);
	
	// Set up socket address info
	struct addrinfo hints, *addr;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET6;	// IPv6
	hints.ai_socktype = SOCK_DGRAM;
	char port_str[8];
	sprintf(port_str, "%u", SORT_PORT);
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
	
	// Wait for message
	struct sockaddr_in6 remote_addr;
	int i;
	int buflen;
	char buf[RECV_BUFFER_SIZE];
	socklen_t addr_size = sizeof remote_addr;
	while ((buflen = recvfrom(sockfd, buf, RECV_BUFFER_SIZE, 0, (struct sockaddr *)&remote_addr, &addr_size)) != -1)
	{
		// Deserialize
		demo_table1_entry table1[MAX_TABLE_SIZE];
		int bytes_used;
		int rows1 = deserialize_table1(table1, MAX_TABLE_SIZE, buf, buflen, &bytes_used);
		demo_table2_entry table2[MAX_TABLE_SIZE];
		int rows2 = deserialize_table2(table2, MAX_TABLE_SIZE, buf+bytes_used, buflen-bytes_used, NULL);
#ifdef DEBUG
		printf("Table 1 Rows: %d\n", rows1);
		printf("Table 2 Rows: %d\n", rows2);
#endif
		
		// Process tables
		process_tables(table1, rows1, table2, rows2);
	}
	
	// Close socket
	close_listener();
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