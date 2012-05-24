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
#include "sisis_addr_format.h"
#include "sisis_process_types.h"

#define MAX_HOSTS 100
#define MAX_PROCESSES 500
#define MAX_PROCESS_TYPES 10
/** Host information */
typedef struct {
	uint64_t sys_id;
	uint32_t num_processes;
} host_info_t;
/** Process information */
typedef struct {
	uint64_t process_type;
	uint64_t process_version;
	uint32_t num_processes;
} process_info_t;

/** Compare hosts. */
int compare_hosts(const void * a_ptr, const void * b_ptr)
{
	host_info_t * a = (host_info_t *)a_ptr;
	host_info_t * b = (host_info_t *)b_ptr;
	if (a->sys_id < b->sys_id)
		return -1;
	else if (a->sys_id > b->sys_id)
		return 1;
	return 0;
}

/** Compare processes. */
int compare_procs(const void * a_ptr, const void * b_ptr)
{
	process_info_t * a = (process_info_t *)a_ptr;
	process_info_t * b = (process_info_t *)b_ptr;
	if (a->process_type < b->process_type)
		return -1;
	else if (a->process_type > b->process_type)
		return 1;
	else if (a->process_version < b->process_version)
		return 1;
	else if (a->process_version > b->process_version)
		return 1;
	return 0;
}

int main (int argc, char ** argv)
{
	int i;
	
	// Process types
	char * process_types[MAX_PROCESS_TYPES];
	memset(process_types, 0, sizeof(char *) * MAX_PROCESS_TYPES);
	process_types[SISIS_PTYPE_MACHINE_MONITOR] = "Machine Monitor";
	process_types[SISIS_PTYPE_REMOTE_SPAWN] = "Remote Spawn";
	process_types[SISIS_PTYPE_LEADER_ELECTOR] = "Leader Elector";
	process_types[SISIS_PTYPE_DEMO1_SORT] = "Sort";
	process_types[SISIS_PTYPE_DEMO1_JOIN] = "Join";
	process_types[SISIS_PTYPE_DEMO1_VOTER] = "Voter";
	
	
	// List of hosts and processes
	int num_hosts = 0, num_procs = 0;
	host_info_t hosts[MAX_HOSTS];
	process_info_t procs[MAX_PROCESSES];
	
	// Get kernel routes
	sisis_dump_kernel_routes();
	struct listnode * node;
#ifdef HAVE_IPV6
	LIST_FOREACH(ipv6_rib_routes, node)
	{
		struct route_ipv6 * route = (struct route_ipv6 *)node->data;
		
		// Set up prefix
		char prefix_str[INET6_ADDRSTRLEN];
		if (inet_ntop(AF_INET6, &(route->p->prefix.s6_addr), prefix_str, INET6_ADDRSTRLEN) != 1)
		{
			// Parse components
			uint64_t prefix, sisis_version, process_type, process_version, sys_id, pid, ts;
			if (get_sisis_addr_components(prefix_str, &prefix, &sisis_version, &process_type, &process_version, &sys_id, &pid, &ts) == 0)
			{
				// Check that this is an SIS-IS address
				if (prefix == components[0].fixed_val && sisis_version == components[1].fixed_val)
				{
					// Find host
					host_info_t * host = NULL;
					for (i = 0; i < num_hosts && host == NULL; i++)
						if (hosts[i].sys_id == sys_id)
							host = &hosts[i];
					// Add host if needed
					if (host == NULL && num_hosts + 1 < MAX_HOSTS)
					{
						host = &hosts[num_hosts];
						num_hosts++;
						host->sys_id = sys_id;
						host->num_processes = 0;
					}
					// Check if the host is not null
					if (host != NULL)
						host->num_processes++;
					
					// Find process type/version
					process_info_t * proc = NULL;
					for (i = 0; i < num_procs && proc == NULL; i++)
						if (procs[i].process_type == process_type && procs[i].process_version == process_version)
							proc = &procs[i];
					// Add process if needed
					if (proc == NULL && num_procs + 1 < MAX_PROCESSES)
					{
						proc = &procs[num_procs];
						num_procs++;
						proc->process_type = process_type;
						proc->process_version = process_version;
						proc->num_processes = 0;
					}
					// Check if the process is not null
					if (proc != NULL)
						proc->num_processes++;
					
					//printf("%llu\t%llu\t%llu\t%llu\t%llu\n", process_type, process_version, sys_id, pid, ts);
				}
			}
		}
	}
#endif /* HAVE_IPV6 */
	
	// Sort hosts and processes
	qsort(hosts, num_hosts, sizeof(hosts[0]), compare_hosts);
	qsort(procs, num_procs, sizeof(procs[0]), compare_procs);

	// Print hosts
	printf("==================================== Hosts =====================================\n");
	printf("Host\t# Procs\n");
	for (i = 0; i < num_hosts; i++)
		printf("%llu\t%u\n", hosts[i].sys_id, hosts[i].num_processes);
	
	// Print processes
	printf("================================== Processes ===================================\n");
	printf("Process\t# Procs\n");
	for (i = 0; i < num_procs; i++)
	{
		if (procs[i].process_type < MAX_PROCESS_TYPES && process_types[procs[i].process_type] != NULL)
			printf("%llu(%s)v%llu\t%u\n", procs[i].process_type, process_types[procs[i].process_type], procs[i].process_version, procs[i].num_processes);
		else
			printf("%lluv%llu\t%u\n", procs[i].process_type, procs[i].process_version, procs[i].num_processes);
	}

	exit(0);
}