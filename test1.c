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

//#define BUFFER_OUTPUT


#define VERSION 2

int sockfd = -1, con = -1;
uint64_t ptype, host_num, pid;
uint64_t timestamp;

#ifdef BUFFER_OUTPUT
struct output_buf
{
	char * buf;
	struct output_buf * next;
};
struct output_buf * output_buf_head = NULL;
struct output_buf * output_buf_tail = NULL;
#endif

void ts_printf(const char * format, ... )
{
	// Get & print time
	struct timespec time;
  clock_gettime(CLOCK_REALTIME, &time);
#ifdef BUFFER_OUTPUT
	// Add to list
	struct output_buf * tmp = malloc(sizeof(struct output_buf));
	memset(tmp, 0, sizeof(*tmp));
	if (output_buf_head == NULL)
		output_buf_head = output_buf_tail = tmp;
	else
	{
		output_buf_tail->next = tmp;
		output_buf_tail = tmp;
	}
	
	// Timestamp
	asprintf(&tmp->buf, "[%ld.%09ld] ", time.tv_sec, time.tv_nsec);
	
	// Add to list
	tmp = malloc(sizeof(struct output_buf));
	memset(tmp, 0, sizeof(*tmp));
	if (output_buf_head == NULL)
		output_buf_head = output_buf_tail = tmp;
	else
	{
		output_buf_tail->next = tmp;
		output_buf_tail = tmp;
	}
	
	// printf
	va_list args;
	va_start(args, format);
	vasprintf(&tmp->buf, format, args);
	va_end(args);
#else // !defined BUFFER_OUTPUT
	printf("[%ld.%09ld] ", time.tv_sec, time.tv_nsec);
	
	// printf
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
	
	// Flush output
	fflush(stdout);
#endif // BUFFER_OUTPUT
}

void close_listener()
{
	if (sockfd != -1)
	{
		ts_printf("Closing listening socket...\n");
		close(sockfd);
		
		// Unregister
		ts_printf("Unregistering SIS-IS address...\n");
		sisis_unregister(NULL, (uint64_t)ptype, (uint64_t)VERSION, (uint64_t)host_num, (uint64_t)pid, (uint64_t)timestamp);
		
		sockfd = -1;
	}
}

void terminate(int signal)
{
	ts_printf("Terminating...\n");
	close_listener();
	if (con != -1)
	{
		ts_printf("Closing remove connection socket...\n");
		close(con);
	}
	
#ifdef BUFFER_OUTPUT
	struct output_buf * tmp = output_buf_head, * tmp2;
	while (tmp != NULL)
	{
		printf("%s", tmp->buf);
		tmp2 = tmp->next;
		free(tmp);
		tmp = tmp2;
	}
#endif // BUFFER_OUTPUT
	
	exit(0);
}

int rib_monitor_add_ipv4_route(struct route_ipv4 * route)
{
	char prefix_str[INET_ADDRSTRLEN];
	if (inet_ntop(AF_INET, &(route->p->prefix.s_addr), prefix_str, INET_ADDRSTRLEN) != 1)
		ts_printf("Added route: %s/%d [%u/%u]\n", prefix_str, route->p->prefixlen, route->distance, route->metric);
	
	// Free memory
	free(route);
}

int rib_monitor_remove_ipv4_route(struct route_ipv4 * route)
{
	char prefix_str[INET_ADDRSTRLEN];
	if (inet_ntop(AF_INET, &(route->p->prefix.s_addr), prefix_str, INET_ADDRSTRLEN) != 1)
		ts_printf("Removed route: %s/%d [%u/%u]\n", prefix_str, route->p->prefixlen, route->distance, route->metric);
	
	// Free memory
	free(route);
}

#ifdef HAVE_IPV6
int rib_monitor_add_ipv6_route(struct route_ipv6 * route)
{
	char prefix_str[INET6_ADDRSTRLEN];
	if (inet_ntop(AF_INET6, &(route->p->prefix.s6_addr), prefix_str, INET6_ADDRSTRLEN) != 1)
		ts_printf("Added route: %s/%d [%u/%u]\n", prefix_str, route->p->prefixlen, route->distance, route->metric);
	
	// Free memory
	free(route);
}

int rib_monitor_remove_ipv6_route(struct route_ipv6 * route)
{
	char prefix_str[INET6_ADDRSTRLEN];
	if (inet_ntop(AF_INET6, &(route->p->prefix.s6_addr), prefix_str, INET6_ADDRSTRLEN) != 1)
		ts_printf("Removed route: %s/%d [%u/%u]\n", prefix_str, route->p->prefixlen, route->distance, route->metric);
	
	// Free memory
	free(route);
}
#endif /* HAVE_IPV6 */

int main (int argc, char ** argv)
{
	// Get start time
	timestamp = time(NULL);
	
	// Set up signal handling
	signal(SIGABRT, terminate);
	signal(SIGTERM, terminate);
	signal(SIGINT, terminate);
	
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
	
	// Monitor rib changes
	if (argc == 2 && strcmp(argv[1], "--rib-mon-test") == 0)
	{
		struct subscribe_to_rib_changes_info info;
		info.rib_add_ipv4_route = rib_monitor_add_ipv4_route;
		info.rib_remove_ipv4_route = rib_monitor_remove_ipv4_route;
		info.rib_add_ipv6_route = rib_monitor_add_ipv6_route;
		info.rib_remove_ipv6_route = rib_monitor_remove_ipv6_route;
		subscribe_to_rib_changes(&info);
		
		// Do nothing
		sleep(10);
		
		unsubscribe_to_rib_changes(&info);
		
		sleep(60);
		exit(0);
	}
	
	struct addrinfo hints, *addr;
	
	// Check if the IP address and port are set
	if (argc != 4)
	{
		printf("Usage: %s <host_num> <process_type> <port>\n", argv[0]);
		exit(0);
	}
	
	// Get process type and host number
	sscanf (argv[1], "%llu", &host_num);
	sscanf (argv[2], "%llu", &ptype);
	char sisis_addr[INET6_ADDRSTRLEN+1];
	
	// Get pid
	pid = getpid();
	
	// Register address
	ts_printf("Registering SIS-IS address.\n");
	if (sisis_register(sisis_addr, (uint64_t)ptype, (uint64_t)VERSION, host_num, pid, timestamp) != 0)
	{
		ts_printf("Failed to register SIS-IS address.\n");
		exit(3);
	}
	
	/*
	// Test to see that we can register multiple addresses
	char sisis_addr_tmp[INET6_ADDRSTRLEN+1];
	int k;
	for (k = 100; k < 104; k++)
		if (sisis_register(sisis_addr_tmp, (uint64_t)(ptype+k), (uint64_t)VERSION, host_num, pid, timestamp) != 0)
			ts_printf("Failed to register SIS-IS address.\n");
	*/
	
	// Status
	ts_printf("Opening socket at %s on port %s.\n", sisis_addr, argv[3]);
	
	// Set up socket address info
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET6;	// IPv6
	hints.ai_socktype = SOCK_STREAM;
	getaddrinfo(sisis_addr, argv[3], &hints, &addr);
	
	// Create socket
	if ((sockfd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol)) == -1)
	{
		ts_printf("Failed to open socket: %s\n", strerror(errno));
		exit(4);
	}
	
	// Bind to port
	if (bind(sockfd, addr->ai_addr, addr->ai_addrlen) == -1)
	{
		ts_printf("Failed to bind socket to port: %s\n", strerror(errno));
		close_listener();
		exit(5);
	}
	
	// Status message
	inet_ntop(AF_INET6, &((struct sockaddr_in6 *)(addr->ai_addr))->sin6_addr, sisis_addr, INET6_ADDRSTRLEN);
	ts_printf("Socket opened at %s on port %u.\n", sisis_addr, ntohs(((struct sockaddr_in *)(addr->ai_addr))->sin_port));
	
	// Listen on the socket
	if (listen(sockfd, 5) == -1)
	{
		ts_printf("Failed to listen on socket: %s\n", strerror(errno));
		close_listener();
		exit(6);
	}
	
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
				ts_printf("Failed to send message.\n");
			
			// Trim
			while (buf[len-1] == '\r' || buf[len-1] == '\n')
				buf[--len] = '\0';
			
			ts_printf("Received \"%s\".\n", buf);
			
			// Exit if needed
			if (strcmp(buf, "exit") == 0)
				break;
		}
		else
			ts_printf("Failed to receive message.\n");
		
		// Close connection
		close(con);
		con = -1;
	}
	
	// Close socket
	close_listener();
}
