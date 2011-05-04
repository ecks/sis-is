#ifndef SORT_H
#define SORT_H

#include "table.h"

/** Sort tables and send results to join processes. */
void process_tables(demo_table1_entry * table1, int rows1, demo_table2_entry * table2, int rows2);

#endif