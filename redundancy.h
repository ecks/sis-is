#ifndef REDUNDANCY_H
#define REDUNDANCY_H

#include <sys/types.h>

#include "../tests/sisis_api.h"

#define MACHINE_MONITOR_REQUEST_TIMEOUT 2000000 // in usec
#define RECHECK_PROCS_ALARM_DELAY 500000 // in usec

// Socket
extern int sockfd;

/** Count number of processes of a given type */
int get_process_type_count(uint64_t process_type);

/** Get list of processes of a given type.  Caller should call FREE_LINKED_LIST on result after. */
struct list * get_processes_by_type(uint64_t process_type);

/** Checks if there is an appropriate number of join processes running in the system. */
void check_redundancy();

/** Main loop for redundant processes */
void redundancy_main (uint64_t process_type, uint64_t process_type_version, int port, uint64_t input_process_type, void (*process_input)(char *, int), void (*vote_and_process)(), int argc, char ** argv);

int rib_monitor_add_ipv6_route(struct route_ipv6 * route);
int rib_monitor_remove_ipv6_route(struct route_ipv6 * route);

// Information for deciding most desirable host to start
typedef struct
{
	uint64_t priority; // Low # = High Priority
	struct in6_addr * remote_spawn_addr;
} desirable_host_t;

/** Compare desirable hosts. */
int compare_desirable_hosts(const void * a_ptr, const void * b_ptr);

/** Creates a new socket. */
int make_socket(char * port);

#endif
