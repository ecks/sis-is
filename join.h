#ifndef SORT_H
#define SORT_H

#include "table.h"

/** Count number of sort processes */
int get_sort_process_count();

/** Join tables and send result to voter processes. */
void process_tables(demo_table1_entry * table1, int rows1, demo_table2_entry * table2, int rows2);

#endif