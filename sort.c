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
	int len;
	char buf[RECV_BUFFER_SIZE];
	socklen_t addr_size = sizeof remote_addr;
	while ((len = recvfrom(sockfd, buf, RECV_BUFFER_SIZE, 0, (struct sockaddr *)&remote_addr, &addr_size)) != -1)
	{
		// Deserialize
		demo_table1_entry table1[MAX_TABLE_SIZE];
		int bytes_used;
		int rows1 = deserialize_table1(table1, MAX_TABLE_SIZE, buf, buflen, len);
		demo_table2_entry table2[MAX_TABLE_SIZE];
		int rows2 = deserialize_table2(table2, MAX_TABLE_SIZE, buf+bytes_used, len-bytes_used);
		printf("Table 1 Rows: %d\n", rows1);
		printf("Table 2 Rows: %d\n", rows2);
		
		// Sort tables
		sort_table1_by_user_id(table1, rows1);
		sort_table2_by_user_id(table2, rows2);
		
		// Print
		for (i = 0; i < rows1; i++)
			printf("User Id: %d\tName: %s\n", table1[i].user_id, table1[i].name);
		for (i = 0; i < rows2; i++)
			printf("User Id: %d\tName: %s\n", table2[i].user_id, table2[i].name);
		
		// Serialize
		int buflen, buflen2;
		buflen = serialize_table1(table1, MAX_TABLE_SIZE, buf, RECV_BUFFER_SIZE);
		if (buflen != -1)
			buflen2 = serialize_table2(table2, MAX_TABLE_SIZE, buf, RECV_BUFFER_SIZE - buflen);
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
				// Send to all sort processes
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
					sockaddr.sin6_port = htons(SORT_PORT);
					sockaddr.sin6_addr = *remote_addr;
				}
				
				if (sendto(sockfd, buf, buflen+buflen2, 0, (struct sockaddr *)&sockaddr, sockaddr_size) == -1)
					printf("Failed to send message.  Error: %i\n", errno);
				
				// Free memory
				FREE_LINKED_LIST(join_addrs);
			}
		}
		
#if 0
		char send_buf[16384];
		
		// Print sender address
		char sender[INET6_ADDRSTRLEN+1];
		inet_ntop(AF_INET6, &remote_addr.sin6_addr, sender, INET6_ADDRSTRLEN);
		printf("Sending data back to %s.\n", sender);
		
		// Send data back
		int len = strlen(send_buf);
		char * send_buf_cur = send_buf;
		while (len > 0)
		{
			// TODO: Deal with fragmented packet order
			/* For netcat:
			int send_len = (len > 1024) ? 1024 : len;	// Accommodate netcats 1024 byte limit (TODO: Remove this in the future)
			int sent = sendto(sockfd, send_buf_cur, send_len, 0, (struct sockaddr *)&remote_addr, addr_size);
			*/
			int sent = sendto(sockfd, send_buf_cur, len, 0, (struct sockaddr *)&remote_addr, addr_size);
			if (sent == -1)
			{
				perror("\tFailed to send message");
				break;
			}
			len -= sent;
			send_buf_cur += sent;
		}
#endif
	}
	
	// Close socket
	close_listener();
}




#if 0
int main (int argc, char ** argv)
{
	srand(time(NULL));
	
#define TABLE_SIZE 50
#define BUFFER_SIZE 65536
	int i;
	demo_table1_entry table1[TABLE_SIZE];
	short user_id_pool[100];
	for (i = 0; i < 100; i++)
		user_id_pool[i] = 0;
	for (i = 0; i < TABLE_SIZE; i++)
	{
		do {
			table1[i].user_id = rand() % 100;
		} while (user_id_pool[table1[i].user_id]);
		user_id_pool[table1[i].user_id] = 1;
		
		sprintf(table1[i].name, "User #%d", i+1);
	}
	
/*
	// Print
	for (i = 0; i < TABLE_SIZE; i++)
		printf("User Id: %d\tName: %s\n", table1[i].user_id, table1[i].name);
*/
	// Serialize
	char buf[BUFFER_SIZE];
	int buflen = serialize_table1(table1, TABLE_SIZE, buf, BUFFER_SIZE);
/*
	for (i = 0; i < buflen; i++)
	{
		printf("%02x", buf[i] & 0xff);
		if (i == buflen - 1 || i % 10 == 9)
			printf("\n");
		else
			printf("\t");
	}
*/
	
	// Deserialize
	demo_table1_entry new_table1[TABLE_SIZE];
	memset(new_table1, 0, sizeof(*new_table1) * TABLE_SIZE);
	int rows = deserialize_table1(new_table1, TABLE_SIZE, buf, buflen);
	printf("Table 1 Rows: %d\n", rows);
	
	// Sort
	sort_table1_by_user_id(new_table1, rows);
	
	// Print
	for (i = 0; i < rows; i++)
		printf("User Id: %d\tName: %s\n", new_table1[i].user_id, new_table1[i].name);
	
	
	
	printf("\n\n");
	
	
	
	demo_table2_entry table2[TABLE_SIZE];
	for (i = 0; i < 100; i++)
		user_id_pool[i] = 0;
	for (i = 0; i < TABLE_SIZE; i++)
	{
		do {
			table2[i].user_id = rand() % 100;
		} while (user_id_pool[table2[i].user_id]);
		user_id_pool[table2[i].user_id] = 1;
		
		table2[i].gender = (i % 3) ? 'M' : 'F';
	}
	
/*
	// Print
	for (i = 0; i < TABLE_SIZE; i++)
		printf("User Id: %d\tGender: %c\n", table2[i].user_id, table2[i].gender);
*/
	
	// Serialize
	buflen = serialize_table2(table2, TABLE_SIZE, buf, BUFFER_SIZE);
/*
	for (i = 0; i < buflen; i++)
	{
		printf("%02x", buf[i] & 0xff);
		if (i == buflen - 1 || i % 10 == 9)
			printf("\n");
		else
			printf("\t");
	}
*/
	
	// Deserialize
	demo_table2_entry new_table2[TABLE_SIZE];
	memset(new_table2, 0, sizeof(*new_table2) * TABLE_SIZE);
	rows = deserialize_table2(new_table2, TABLE_SIZE, buf, buflen);
	printf("Table 2 Rows: %d\n", rows);
	
	// Sort
	sort_table2_by_user_id(new_table2, rows);
	
	// Print
	for (i = 0; i < rows; i++)
		printf("User Id: %d\tGender: %c\n", new_table2[i].user_id, new_table2[i].gender);
	
	
	
	
	
	// Join
	demo_merge_table_entry join_table[TABLE_SIZE];
	rows = merge_join(new_table1, TABLE_SIZE, new_table2, TABLE_SIZE, join_table, TABLE_SIZE);
	
	// Print
	if (rows == -1)
		printf("Join error.\n");
	else
	{
		printf("Joined Rows: %d\n", rows);
		for (i = 0; i < rows; i++)
		printf("User Id: %d\tName: %s\tGender: %c\n", join_table[i].user_id, join_table[i].name, join_table[i].gender);
	}
}
#endif
