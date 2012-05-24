/*
 * SIS-IS Test program.
 * Stephen Sigwart
 * University of Delaware
 */

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>

#include <pthread.h>

#include <stdarg.h>
#include <time.h>
#include "../tests/sisis_api.h"
#include "../tests/sisis_process_types.h"
#include "../tests/sisis_addr_format.h"

#define MACHINE_MONITOR_PORT 50000

//#define DEMO_UPGRADE

int sockfd = -1;

// Number of processes per host
int num_proc_pre_host[16];

/** Process visualization information */
typedef struct {
	char * desc;
	int proc_num;
} process_visualization_info_t;

/** Get process visualization information */
process_visualization_info_t get_process_info(int process_type, int process_version)
{
	process_visualization_info_t info;
	info.desc = "Unknown";
	info.proc_num = 0;
	switch (process_type)
	{
		case SISIS_PTYPE_REMOTE_SPAWN:
			info.desc = "RemoteSpawn";
			info.proc_num = 1;
			break;
		case SISIS_PTYPE_MACHINE_MONITOR:
			info.desc = "MachineMonitor";
			info.proc_num = 2;
			break;
#ifdef DEMO_UPGRADE
		case SISIS_PTYPE_DEMO1_SHIM:
			info.desc = "Shim";
			info.proc_num = 3;
			break;
		case SISIS_PTYPE_DEMO1_SORT:
			if (process_version == 2)
			{
				info.desc = "Sort_v2";
				info.proc_num = 5;
			}
			else
			{
				info.desc = "Sort";
				info.proc_num = 4;
			}
			break;
		case SISIS_PTYPE_DEMO1_JOIN:
			info.desc = "Join";
			info.proc_num = 6;
			break;
		case SISIS_PTYPE_DEMO1_VOTER:
			info.desc = "Voter";
			info.proc_num = 7;
			break;
#else
		case SISIS_PTYPE_DEMO1_SHIM:
			info.desc = "Shim";
			info.proc_num = 3;
			break;
		case SISIS_PTYPE_DEMO1_SORT:
			info.desc = "Sort";
			info.proc_num = 4;
			break;
		case SISIS_PTYPE_DEMO1_JOIN:
			info.desc = "Join";
			info.proc_num = 5;
			break;
		case SISIS_PTYPE_DEMO1_VOTER:
			info.desc = "Voter";
			info.proc_num = 6;
			break;
#endif
	}
	return info;
}

/** Data for update_hostname function */
typedef struct {
	pthread_t thread;
	uint64_t sys_id;
} update_hostname_data_t;

/** Thread to update hostname */
void * update_hostname(void * data_v)
{
	update_hostname_data_t * data = (update_hostname_data_t *)data_v;
	
	// Get hostname
	char hostname[64];
	
	// Get machine monitors
	char mm_addr[INET6_ADDRSTRLEN+1];
	sisis_create_addr(mm_addr, (uint64_t)SISIS_PTYPE_MACHINE_MONITOR, (uint64_t)1, data->sys_id, (uint64_t)0, (uint64_t)0);
	struct prefix_ipv6 mm_prefix = sisis_make_ipv6_prefix(mm_addr, 74);
	struct list * monitor_addrs = get_sisis_addrs_for_prefix(&mm_prefix);
	
	// Find machine monitor
	struct in6_addr * mm_remote_addr = NULL;
	if (monitor_addrs != NULL && monitor_addrs->size > 0)
	{
		mm_remote_addr = (struct in6_addr *)monitor_addrs->head->data;
		/*
		char tmp_addr[INET6_ADDRSTRLEN];
			if (inet_ntop(AF_INET6, mm_remote_addr, tmp_addr, INET6_ADDRSTRLEN) != NULL)
				printf("Sending message to machine monitor at %s.\n", tmp_addr);
		*/
		// Make new socket
		int tmp_sock = socket(AF_INET6, SOCK_DGRAM, 0);
		if (tmp_sock != -1)
		{
			short remaining_attempts = 4;
			while (remaining_attempts > 0)
			{
				// Set of sockets for select call
				fd_set socks;
				FD_ZERO(&socks);
				FD_SET(tmp_sock, &socks);
				
				// Timeout information for select call
				struct timeval select_timeout;
				select_timeout.tv_sec = 2;
				select_timeout.tv_usec = 500000;	// 500ms
				
				// Set up socket info
				struct sockaddr_in6 sockaddr;
				int sockaddr_size = sizeof(sockaddr);
				memset(&sockaddr, 0, sockaddr_size);
				sockaddr.sin6_family = AF_INET6;
				sockaddr.sin6_port = htons(MACHINE_MONITOR_PORT);
				sockaddr.sin6_addr = *mm_remote_addr;
				
				// Get stats
				char * req = "data\n";
				if (sendto(tmp_sock, req, strlen(req), 0, (struct sockaddr *)&sockaddr, sockaddr_size) != -1)
				{
					struct sockaddr_in6 fromaddr;
					int fromaddr_size = sizeof(fromaddr);
					memset(&fromaddr, 0, fromaddr_size);
					char buf[65536];
					int len;
					
					// Wait for response
					if (select(tmp_sock+1, &socks, NULL, NULL, &select_timeout) <= 0)
					{}
					else if ((len = recvfrom(tmp_sock, buf, 65536, 0, (struct sockaddr *)&fromaddr, &fromaddr_size)) < 1)
					{}
					else if (sockaddr_size != fromaddr_size || memcmp(&sockaddr, &fromaddr, fromaddr_size) != 0)
					{}
					else
					{
						// Terminate if needed
						if (len == 65536)
							buf[len-1] = '\0';
						
						// Parse response
						char * match;
						
						// Get hostname
						char * hostname_str = "Hostname: ";
						if ((match = strstr(buf, hostname_str)) != NULL)
							sscanf(match+strlen(hostname_str), "%s", hostname);
						
						// Set hostname
						sprintf(buf, "hostname %llu %s\n", data->sys_id % 16, hostname);
						send(sockfd, buf, strlen(buf), 0);
						
						// No more attempts needed
						remaining_attempts = 0;
					}
					
					remaining_attempts--;
				}
			}
			
			// Close socket
			close(tmp_sock);
		}
	}
	if (monitor_addrs != NULL)
		FREE_LINKED_LIST(monitor_addrs);
	
	// Free data
	free(data_v);
}

#ifdef HAVE_IPV6
int rib_monitor_add_ipv6_route(struct route_ipv6 * route, void * data)
{
	char prefix_str[INET6_ADDRSTRLEN];
	if (inet_ntop(AF_INET6, &(route->p->prefix.s6_addr), prefix_str, INET6_ADDRSTRLEN) != NULL)
	{
		uint64_t prefix, sisis_version, process_type, process_version, sys_id, other_pid, ts;
		if (get_sisis_addr_components(prefix_str, &prefix, &sisis_version, &process_type, &process_version, &sys_id, &other_pid, &ts) == 0)
		{
			// Check that this is an SIS-IS address
			if (prefix == components[0].fixed_val && sisis_version == components[1].fixed_val)
			{
				// Get process number and name to send
				process_visualization_info_t proc_info = get_process_info((int)process_type, (int)process_version);
				
				// Send message
				char buf[512];
				if (num_proc_pre_host[sys_id%16] == 0 || process_type == (uint64_t)SISIS_PTYPE_MACHINE_MONITOR)
				{
					// Set temporary hostname if the host need to be set to up
					if (num_proc_pre_host[sys_id%16] == 0)
					{
						char hostname[64];
						sprintf(hostname, "Host #%llu", sys_id%16);
						
						// Set host to up
						sprintf(buf, "hostUp %llu %s\n", sys_id % 16, hostname);
						send(sockfd, buf, strlen(buf), 0);
					}
					
					// Get hostname asynchronously
					update_hostname_data_t * data = malloc(sizeof(update_hostname_data_t));
					if (data != NULL)
					{
						data->sys_id = sys_id;
						pthread_create(&data->thread, NULL, update_hostname, (void*)data);
					}
				}
				
				num_proc_pre_host[sys_id%16]++;
				sprintf(buf, "procAdd %llu %i %s\n", sys_id % 16, proc_info.proc_num, proc_info.desc);
				send(sockfd, buf, strlen(buf), 0);
			}
		}
	}
	
	// Free memory
	free(route);
}

int rib_monitor_remove_ipv6_route(struct route_ipv6 * route, void * data)
{
	char prefix_str[INET6_ADDRSTRLEN];
	if (inet_ntop(AF_INET6, &(route->p->prefix.s6_addr), prefix_str, INET6_ADDRSTRLEN) != NULL)
	{
		uint64_t prefix, sisis_version, process_type, process_version, sys_id, other_pid, ts;
		if (get_sisis_addr_components(prefix_str, &prefix, &sisis_version, &process_type, &process_version, &sys_id, &other_pid, &ts) == 0)
		{
			// Check that this is an SIS-IS address
			if (prefix == components[0].fixed_val && sisis_version == components[1].fixed_val)
			{
				// Get process number and name to send
				process_visualization_info_t proc_info = get_process_info((int)process_type, (int)process_version);
				
				// Send message
				char buf[512];
				if (--num_proc_pre_host[sys_id%16] == 0)
				{
					sprintf(buf, "hostDown %llu\n", sys_id % 16);
					send(sockfd, buf, strlen(buf), 0);
				}
				sprintf(buf, "procDel %llu %i %s\n", sys_id % 16, proc_info.proc_num, proc_info.desc);
				send(sockfd, buf, strlen(buf), 0);
			}
		}
	}
	
	// Free memory
	free(route);
}
#endif /* HAVE_IPV6 */

int main (int argc, char ** argv)
{
	int i;
	for (i = 0; i < 16; i++)
		num_proc_pre_host[i] = 0;
	
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
	hints.ai_family = AF_INET;	// IPv4
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(argv[1], argv[2], &hints, &server_info) != 0)
	{
		printf("Failed to set up server information.\n");
		exit(1);
	}
	
	// Create socket
	if ((sockfd = socket(server_info->ai_family, server_info->ai_socktype, server_info->ai_protocol)) == -1)
	{
		printf("Failed to open socket: %s\n", strerror(errno));
		exit(1);
	}
	
	// Connect to socket
	if (connect(sockfd, server_info->ai_addr,server_info->ai_addrlen) == -1)
	{
		printf("Failed to connect to server: %s\n", strerror(errno));
		close(sockfd);
		exit(1);
	}
	
	// Get initial RIB dump
	sisis_dump_kernel_routes();
	struct listnode * node;
#ifdef HAVE_IPV6
	LIST_FOREACH(ipv6_rib_routes, node)
	{
		struct route_ipv6 * route = (struct route_ipv6 *)node->data;
		rib_monitor_add_ipv6_route(route, NULL);
	}
#endif /* HAVE_IPV6 */
	
	// Monitor rib changes
	struct subscribe_to_rib_changes_info info;
	info.rib_add_ipv4_route = NULL;
	info.rib_remove_ipv4_route = NULL;
	info.rib_add_ipv6_route = rib_monitor_add_ipv6_route;
	info.rib_remove_ipv6_route = rib_monitor_remove_ipv6_route;
	info.data = NULL;
	subscribe_to_rib_changes(&info);
	
	// Do nothing
	while (1)
		sleep(600);
	
	// Close socket
	close(sockfd);
}
