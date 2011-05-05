#ifndef SORT_H
#define SORT_H

#include <sys/types.h>

#include "table.h"
#include "../tests/sisis_api.h"

/** Count number of processes of a given type */
int get_process_type_count(uint64_t process_type);

/** Count number of sort processes */
int get_sort_process_count();

/** Join tables and send result to voter processes. */
void process_tables(demo_table1_entry * table1, int rows1, demo_table2_entry * table2, int rows2);

/** Checks if there is an appropriate number of join processes running in the system. */
void check_redundancy();

int rib_monitor_add_ipv6_route(struct route_ipv6 * route);
int rib_monitor_remove_ipv6_route(struct route_ipv6 * route);

#endif