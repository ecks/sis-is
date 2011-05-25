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

#include <stdarg.h>
#include <time.h>
#include "sisis_api.h"

int sockfd = -1;

#ifdef HAVE_IPV6
int rib_monitor_add_ipv6_route(struct route_ipv6 * route)
{
	char prefix_str[INET6_ADDRSTRLEN];
	if (inet_ntop(AF_INET6, &(route->p->prefix.s6_addr), prefix_str, INET6_ADDRSTRLEN) != 1)
	{
		uint64_t prefix, sisis_version, process_type, process_version, sys_id, other_pid, ts;
		if (get_sisis_addr_components(prefix_str, &prefix, &sisis_version, &process_type, &process_version, &sys_id, &other_pid, &ts) == 0)
		{
			// Get process number and name to send
			int proc_num = 0;
			char * proc = "Unknown";
			switch (process_type)
			{
				case SISIS_PTYPE_DEMO1_SORT:
					proc = "Sort";
					proc_num = 1;
					break;
				case SISIS_PTYPE_DEMO1_JOIN:
					proc = "Join";
					proc_num = 2;
					break;
				case SISIS_PTYPE_DEMO1_VOTER:
					proc = "Voter";
					proc_num = 3;
					break;
			}
			
			// Send message
			char buf[512];
			sprintf(buf, "hostUp %llu\n", sys_id % 16);
			sprintf(buf, "procAdd %llu $%llu %s\n", sys_id % 16, proc_num, proc);
			send(sockfd, buf, strlen(buf), 0);
		}
	}
	
	// Free memory
	free(route);
}

int rib_monitor_remove_ipv6_route(struct route_ipv6 * route)
{
	char prefix_str[INET6_ADDRSTRLEN];
	if (inet_ntop(AF_INET6, &(route->p->prefix.s6_addr), prefix_str, INET6_ADDRSTRLEN) != 1)
	{
		uint64_t prefix, sisis_version, process_type, process_version, sys_id, other_pid, ts;
		if (get_sisis_addr_components(prefix_str, &prefix, &sisis_version, &process_type, &process_version, &sys_id, &other_pid, &ts) == 0)
		{
			// Get process number and name to send
			int proc_num = 0;
			char * proc = "Unknown";
			switch (process_type)
			{
				case SISIS_PTYPE_DEMO1_SORT:
					proc = "Sort";
					proc_num = 1;
					break;
				case SISIS_PTYPE_DEMO1_JOIN:
					proc = "Join";
					proc_num = 2;
					break;
				case SISIS_PTYPE_DEMO1_VOTER:
					proc = "Voter";
					proc_num = 3;
					break;
			}
			
			// Send message
			char buf[512];
			sprintf(buf, "hostUp %llu\n", sys_id % 16);
			sprintf(buf, "procAdd %llu $%llu %s\n", sys_id % 16, proc_num, proc);
			send(sockfd, buf, strlen(buf), 0);
		}
	}
	
	// Free memory
	free(route);
}
#endif /* HAVE_IPV6 */

int main (int argc, char ** argv)
{
	// Open socket to visualization process
	// Check if the IP address and port are set
	if (argc != 3)
	{
		printf("Usage: %s <visualization_host> <visualization_port>\n", argv[0]);
		exit(1);
	}
	
	// Set up socket address info
	struct addrinfo hints, *server_info;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET4;	// IPv4
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(argv[1], argv[2], &hints, &server_info) != 0)
	{
		printf("Failed to set up server information.\n");
		exit(1);
	}
	
	// Create socket
	if ((sockfd = socket(server_info->ai_family, server_info->ai_socktype, server_info->ai_protocol)) == -1)
	{
		ts_printf("Failed to open socket: %s\n", strerror(errno));
		exit(1);
	}
	
	// Connect to socket
	if (connect(sockfd, server_info->ai_addr,server_info->ai_addrlen) == -1)
	{
		ts_printf("Failed to connect to server: %s\n", strerror(errno));
		close(sockfd);
		exit(1);
	}
	
	// Monitor rib changes
	struct subscribe_to_rib_changes_info info;
	info.rib_add_ipv4_route = NULL;
	info.rib_remove_ipv4_route = NULL;
	info.rib_add_ipv6_route = rib_monitor_add_ipv6_route;
	info.rib_remove_ipv6_route = rib_monitor_remove_ipv6_route;
	subscribe_to_rib_changes(&info);
	
	// Do nothing
	while (1)
		sleep(600);
	
	// Close socket
	close(sockfd);
}
