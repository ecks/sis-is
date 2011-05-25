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

int main (int argc, char ** argv)
{
	// Get kernel routes
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