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

#include <time.h>
#include "sisis_api.h"

int sockfd = -1, con = -1;
int ptype, host_num, pid;

void close_listener()
{
	if (sockfd != -1)
	{
		printf("Closing listening socket...\n");
		close(sockfd);
		
		// Unregister
		sisis_unregister(ptype, host_num, pid);
		
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

int rib_monitor_add_ipv4_route(struct route_ipv4 * route)
{
	// Get time
	struct timespec time;
  clock_gettime(CLOCK_REALTIME, &time);
	
	char prefix_str[INET_ADDRSTRLEN];
	if (inet_ntop(AF_INET, &(route->p->prefix.s_addr), prefix_str, INET_ADDRSTRLEN) != 1)
		printf("[%ld.%09ld] Added route: %s/%d [%u/%u]\n", time.tv_sec, tim.tv_nsec, prefix_str, route->p->prefixlen, route->distance, route->metric);
	
	// Free memory
	free(route);
}

int rib_monitor_remove_ipv4_route(struct route_ipv4 * route)
{
	// Get time
	struct timespec time;
  clock_gettime(CLOCK_REALTIME, &time);
	
	char prefix_str[INET_ADDRSTRLEN];
	if (inet_ntop(AF_INET, &(route->p->prefix.s_addr), prefix_str, INET_ADDRSTRLEN) != 1)
		printf("[%ld.%09ld] Removed route: %s/%d [%u/%u]\n", time.tv_sec, tim.tv_nsec, prefix_str, route->p->prefixlen, route->distance, route->metric);
	
	// Free memory
	free(route);
}

#ifdef HAVE_IPV6
int rib_monitor_add_ipv6_route(struct route_ipv6 * route)
{
	// Get time
	struct timespec time;
  clock_gettime(CLOCK_REALTIME, &time);
	
	char prefix_str[INET6_ADDRSTRLEN];
	if (inet_ntop(AF_INET6, &(route->p->prefix.s6_addr), prefix_str, INET6_ADDRSTRLEN) != 1)
		printf("[%ld.%09ld] Added route: %s/%d [%u/%u]\n", time.tv_sec, tim.tv_nsec, prefix_str, route->p->prefixlen, route->distance, route->metric);
	
	// Free memory
	free(route);
}

int rib_monitor_remove_ipv6_route(struct route_ipv6 * route)
{
	// Get time
	struct timespec time;
  clock_gettime(CLOCK_REALTIME, &time);
	
	char prefix_str[INET6_ADDRSTRLEN];
	if (inet_ntop(AF_INET6, &(route->p->prefix.s6_addr), prefix_str, INET6_ADDRSTRLEN) != 1)
		printf("[%ld.%09ld] Removed route: %s/%d [%u/%u]\n", time.tv_sec, tim.tv_nsec, prefix_str, route->p->prefixlen, route->distance, route->metric);
	
	// Free memory
	free(route);
}
#endif /* HAVE_IPV6 */

int main (int argc, char ** argv)
{
	// Get kernel routes
	if (argc == 2 && strcmp(argv[1], "--rib-dump") == 0)
	{
		sisis_dump_kernel_routes();
		struct listnode * node;
		printf("----------------------------------- IPv4 -----------------------------------\n");
		LIST_FOREACH(ipv4_rib_routes, node)
		{
			struct route_ipv4 * route = (struct route_ipv4 *)node->data;
			
			// Set up prefix
			char prefix_str[INET_ADDRSTRLEN];
			if (inet_ntop(AF_INET, &(route->p->prefix.s_addr), prefix_str, INET_ADDRSTRLEN) != 1)
				printf("%s/%d [%u/%u]\n", prefix_str, route->p->prefixlen, route->distance, route->metric);
		}

#ifdef HAVE_IPV6
		printf("\n----------------------------------- IPv6 -----------------------------------\n");
		LIST_FOREACH(ipv6_rib_routes, node)
		{
			struct route_ipv6 * route = (struct route_ipv6 *)node->data;
			
			// Set up prefix
			char prefix_str[INET6_ADDRSTRLEN];
			if (inet_ntop(AF_INET6, &(route->p->prefix.s6_addr), prefix_str, INET6_ADDRSTRLEN) != 1)
				printf("%s/%d [%u/%u]\n", prefix_str, route->p->prefixlen, route->distance, route->metric);
		}
#endif /* HAVE_IPV6 */

		exit(0);
	}
	
	// Monitor rib changes
	if (argc == 2 && strcmp(argv[1], "--rib-monitor") == 0)
	{
		struct subscribe_to_rib_changes_info info;
		info.rib_add_ipv4_route = rib_monitor_add_ipv4_route;
		info.rib_remove_ipv4_route = rib_monitor_remove_ipv4_route;
		info.rib_add_ipv6_route = rib_monitor_add_ipv6_route;
		info.rib_remove_ipv6_route = rib_monitor_remove_ipv6_route;
		subscribe_to_rib_changes(&info);
		
		// Do nothing
		while (1)
			sleep(600);
	}
	
	struct addrinfo hints, *addr;
	
	// Check if the IP address and port are set
	if (argc != 4)
	{
		printf("Usage: %s <host_num> <process_type> <port>\n", argv[0]);
		exit(1);
	}
	
	// Get process type and host number
	sscanf (argv[1], "%d", &host_num);
	sscanf (argv[2], "%d", &ptype);
	char sisis_addr[INET6_ADDRSTRLEN+1];
	
	// Get pid
	pid = getpid();
	
	// Get time
	struct timespec time;
  
	// Register address
	clock_gettime(CLOCK_REALTIME, &time);
	printf("[%ld.%09ld] Registering SIS-IS address.\n", time.tv_sec, tim.tv_nsec;
	if (sisis_register(ptype, host_num, (uint64_t)pid, sisis_addr) != 0)
	{
		printf("Failed to register SIS-IS address.\n");
		exit(1);
	}
	
	// Status
	printf("Opening socket at %s on port %s.\n", sisis_addr, argv[3]);
	
	// Set up socket address info
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET6;	// IPv6
	hints.ai_socktype = SOCK_STREAM;
	getaddrinfo(sisis_addr, argv[3], &hints, &addr);
	
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
	
	// Listen on the socket
	if (listen(sockfd, 5) == -1)
	{
		printf("Failed to listen on socket.\n");
		close_listener();
		exit(3);
	}
	
	// Set up signal handling
	signal(SIGABRT, terminate);
	signal(SIGTERM, terminate);
	signal(SIGINT, terminate);
	
	// Wait for connection
	struct sockaddr_storage remote_addr;
	int len;
	socklen_t addr_size = sizeof remote_addr;
	while ((con = accept(sockfd, (struct sockaddr *)&remote_addr, &addr_size)) != -1)
	{
		char buf[1024];
		if ((len = recv(con, buf, 1023, 0)) > 0)
		{
			buf[len] = '\0';
			
			// Send data back
			if (send(con, buf, len, 0) == -1)
				printf("Failed to send message.\n");
			
			// Trim
			while (buf[len-1] == '\r' || buf[len-1] == '\n')
				buf[--len] = '\0';
			
			printf("Received \"%s\".\n", buf);
			
			// Exit if needed
			if (strcmp(buf, "exit") == 0)
				break;
		}
		else
			printf("Failed to receive message.\n");
		
		// Close connection
		close(con);
		con = -1;
	}
	
	// Close socket
	close_listener();
}
