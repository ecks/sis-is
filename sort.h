#ifndef SORT_H
#define SORT_H

#include "table.h"

#define SORT_PORT 50000
#define JOIN_PORT 50000

#define RECV_BUFFER_SIZE 65536
#define SEND_BUFFER_SIZE 65536
#define MAX_TABLE_SIZE 50

/** Sort tables and send results to join processes. */
void process_tables(demo_table1_entry * table1, int rows1, demo_table2_entry * table2, int rows2);

#endif