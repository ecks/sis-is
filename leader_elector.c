/*
 * SIS-IS Test program.
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
#include <pthread.h>
#include "leader_elector.h"
#include "../machine_monitor/machine_monitor.h"
#include "../remote_spawn/remote_spawn.h"

#include "../tests/sisis_api.h"
#include "../tests/sisis_structs.h"
#include "../tests/sisis_process_types.h"

#define MAX(a,b) ((a) > (b) ? (a) : (b));

int sockfd = -1, con = -1;
int host_num, pid;

void close_listener()
{
	if (sockfd != -1)
	{
		printf("Closing listening socket...\n");
		close(sockfd);
		
		// Unregister
		sisis_unregister(SISIS_PTYPE_LEADER_ELECTOR, host_num, pid);
		
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

void * recv_thread(void * nil)
{
	struct sockaddr_in fromaddr;
	int fromaddr_size = sizeof(fromaddr);
	memset(&fromaddr, 0, fromaddr_size);
	char buf[65508];
	int len;
	while(1)
	{
		len = recvfrom(sockfd, buf, 65507, 0, (struct sockaddr *)&fromaddr, &fromaddr_size);
		buf[len] = '\0';
		printf("%s", buf);
	}
}

int main (int argc, char ** argv)
{
	// Check number of args
	if (argc != 2)
	{
		printf("Usage: %s <host_num>\n", argv[0]);
		exit(1);
	}
	
	// Get host number
	sscanf (argv[1], "%d", &host_num);
	char sisis_addr[INET6_ADDRSTRLEN+1];
	
	// Get pid
	pid = getpid();
	
	// Register address
	if (sisis_register(SISIS_PTYPE_LEADER_ELECTOR, host_num, pid, sisis_addr) != 0)
	{
		printf("Failed to register SIS-IS address.\n");
		exit(1);
	}
	
	// Status
	printf("Opening socket at %s on port %i.\n", sisis_addr, LEADER_ELECTOR_PORT);
	
	// Set up socket address info
	struct addrinfo hints, *addr;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET6;	// IPv6
	hints.ai_socktype = SOCK_DGRAM;
	char port_str[8];
	sprintf(port_str, "%u", LEADER_ELECTOR_PORT);
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
	
	// Set up receive thread
	pthread_t recv_thread_t;
	pthread_create(&recv_thread_t, NULL, recv_thread, NULL);
	
	// Check how many other processes there are
	struct list * monitor_addrs = get_sisis_addrs_for_process_type(SISIS_PTYPE_MACHINE_MONITOR);
	if (monitor_addrs == NULL || monitor_addrs->size == 0)
	{
		printf("No other SIS-IS hosts found.\n");
		close_listener();
		exit(2);
	}
	
	// Check how many leader_elector processes there are
	int leader_elector_processes_needed = 0;
	struct list * leader_elector_addrs = get_sisis_addrs_for_process_type(SISIS_PTYPE_LEADER_ELECTOR);
	if (!leader_elector_addrs)
	{
		printf("Error.\n");
		close_listener();
		exit(2);
	}
	else
	{
		leader_elector_processes_needed = MAX(3, monitor_addrs->size*.2) - leader_elector_addrs->size;
		if (leader_elector_processes_needed)
			printf("Need to start %d process%s.\n", leader_elector_processes_needed, leader_elector_processes_needed == 1 ? "" : "es");
		else
			printf("Enought processes already running.\n");
		
		// Free memory
		if (leader_elector_addrs)
			FREE_LINKED_LIST(leader_elector_addrs);
	}

	// List other hosts
	int num_hosts = 0;
	struct listnode * node;
	LIST_FOREACH(monitor_addrs, node)
	{
		num_hosts++;
		
		// Print address
		struct in_addr * remote_addr = (struct in_addr *)node->data;
		char addr_str[INET6_ADDRSTRLEN+1];
		if (inet_ntop(AF_INET6, remote_addr, addr_str, INET6_ADDRSTRLEN+1) != 1)
		{
			// Get SIS-IS address info
			struct sisis_addr_components sisis_comp = get_sisis_addr_components(addr_str);
			
			printf("Host[%u]: %s\n", sisis_comp.host_num, addr_str);
			printf("--------------------------------------------------------------------------------\n");
			
			// Set up socket info
			struct sockaddr_in6 sockaddr;
			int sockaddr_size = sizeof(sockaddr);
			memset(&sockaddr, 0, sockaddr_size);
			sockaddr.sin_family = AF_INET6;
			sockaddr.sin_port = htons(MACHINE_MONITOR_PORT);
			sockaddr.sin_addr = *remote_addr;
			
			// Get memory stats
			char * req = "data\n";
			if (sendto(sockfd, req, strlen(req), 0, (struct sockaddr *)&sockaddr, sockaddr_size) == -1)
				printf("Failed to send message.  Error: %i\n", errno);
			else
			{
				/*
				struct sockaddr_in fromaddr;
				int fromaddr_size = sizeof(fromaddr);
				memset(&fromaddr, 0, fromaddr_size);
				char buf[65508];
				int len;
				printf("Waiting for response.\n");
				do
				{
					// TODO: Handle error/timeout
					len = recvfrom(sockfd, buf, 65507, 0, (struct sockaddr *)&fromaddr, &fromaddr_size);
					printf("Received packet of length %d.\n", len);
				}while (sockaddr_size != fromaddr_size || memcmp(&sockaddr, &fromaddr, fromaddr_size) != 0);
				
				buf[len] = '\0';
				printf("%s", buf);
				*/
				
				// Do we need to start another process
				if (leader_elector_processes_needed)
				{
					// Check that the machine doesn't already have a leader elector process running
					struct list * host_leader_elector_addrs = get_sisis_addrs_for_process_type_and_host(SISIS_PTYPE_LEADER_ELECTOR, sisis_comp.host_num);
					if (host_leader_elector_addrs && host_leader_elector_addrs->size)
						printf("Host is already running leader elector.\n");
					else
					{
						// Check if the spawn process is running
						struct list * spawn_addrs = get_sisis_addrs_for_process_type_and_host(SISIS_PTYPE_REMOTE_SPAWN, sisis_comp.host_num);
						if (spawn_addrs && spawn_addrs->size)
						{
							printf("Starting leader elector on host %u.", sisis_comp.host_num);
							
							// Set up socket info
							struct sockaddr_in6 spawn_sockaddr;
							int spawn_sockaddr_size = sizeof(spawn_sockaddr);
							memset(&spawn_sockaddr, 0, spawn_sockaddr_size);
							spawn_sockaddr.sin_family = AF_INET6;
							spawn_sockaddr.sin_port = htons(REMOTE_SPAWN_PORT);
							spawn_sockaddr.sin_addr = *(struct in_addr *)spawn_addrs->head->data;
							
							char req2[32];
							sprintf(req2, "%d %d", REMOTE_SPAWN_REQ_START, SISIS_PTYPE_LEADER_ELECTOR);
							if (sendto(sockfd, req2, strlen(req2), 0, (struct sockaddr *)&spawn_sockaddr, spawn_sockaddr_size) == -1)
								printf("Failed to send message.  Error: %i\n", errno);
							
							// TODO: Check for response
							
							// Decrement # of processes to start
							leader_elector_processes_needed--;
						}
						
						// Free memory
						if (spawn_addrs)
							FREE_LINKED_LIST(spawn_addrs);
					}
					
					// Free memory
					if (host_leader_elector_addrs)
						FREE_LINKED_LIST(host_leader_elector_addrs);
				}
			}
			
			printf("\n\n");
		}
	}
	
	FREE_LINKED_LIST(monitor_addrs);
	
	printf("Ending...\n");
	
	/*
	// Wait for message
	struct sockaddr remote_addr;
	int len;
	char buf[256];
	socklen_t addr_size = sizeof remote_addr;
	while ((len = recvfrom(sockfd, buf, 255, 0, &remote_addr, &addr_size)) != -1)
	{
		
		// Send response
		char out[16];
		sprintf(out, "%d\n", resp);
		if (sendto(sockfd, &out, strlen(out), 0, &remote_addr, addr_size) == -1)
			printf("Failed to send message.\n");
	}
	*/
	
	// Close socket
	close_listener();
}
