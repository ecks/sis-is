/*
 * SIS-IS Demo program.
 * Stephen Sigwart
 * University of Delaware
 */

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "demo.h"

#include "../tests/sisis_api.h"
#include "../tests/sisis_process_types.h"
#include "../tests/sisis_addr_format.h"

/** Get list of processes of a given type.  Caller should call FREE_LINKED_LIST on result after. */
struct list * get_processes_by_type(uint64_t process_type)
{
	char addr[INET6_ADDRSTRLEN+1];
	sisis_create_addr(addr, process_type, (uint64_t)0, (uint64_t)0, (uint64_t)0, (uint64_t)0);
	struct prefix_ipv6 prefix = sisis_make_ipv6_prefix(addr, 37);
	return get_sisis_addrs_for_prefix(&prefix);
}

/** Count number of processes of a given type */
int get_process_type_count(uint64_t process_type)
{
	int cnt = 0;
	
	char addr[INET6_ADDRSTRLEN+1];
	sisis_create_addr(addr, process_type, (uint64_t)0, (uint64_t)0, (uint64_t)0, (uint64_t)0);
	struct prefix_ipv6 prefix = sisis_make_ipv6_prefix(addr, 37);
	struct list * addrs = get_sisis_addrs_for_prefix(&prefix);
	if (addrs != NULL)
	{
		cnt = addrs->size;
		
		// Free memory
		FREE_LINKED_LIST(addrs);
	}
	
	return cnt;
}