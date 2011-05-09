#ifndef SORT_H
#define SORT_H

#include <sys/types.h>

#include "table.h"
#include "../tests/sisis_api.h"

#define MACHINE_MONITOR_REQUEST_TIMEOUT 500000 // usec

/** Count number of processes of a given type */
int get_process_type_count(uint64_t process_type);

/** Get list of processes of a given type.  Caller should call FREE_LINKED_LIST on result after. */
struct list * get_processes_by_type(uint64_t process_type);

/** Count number of sort processes */
int get_sort_process_count();

/** Join tables and send result to voter processes. */
void process_tables(demo_table1_entry * table1, int rows1, demo_table2_entry * table2, int rows2);

/** Checks if there is an appropriate number of join processes running in the system. */
void check_redundancy();

int rib_monitor_add_ipv6_route(struct route_ipv6 * route);
int rib_monitor_remove_ipv6_route(struct route_ipv6 * route);

// Information for deciding most desirable host to start
typedef struct
{
	uint64_t priority; // Low # = High Priority
	struct in6_addr * remote_spawn_addr;
} desirable_host_t;

/** Creates a new socket. */
int make_socket(char * port);

#endif
