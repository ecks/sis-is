#ifndef SORT_H
#define SORT_H

#include "table.h"

#define JOIN_PORT 50000
#define VOTER_PORT 50000


#define RECV_BUFFER_SIZE 65536
#define SEND_BUFFER_SIZE 65536

#define GATHER_RESULTS_TIMEOUT_USEC		750000	// 750ms

// From time.h
#ifndef timersub
#define timersub(a, b, result)						      \
  do {									      \
    (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;			      \
    (result)->tv_usec = (a)->tv_usec - (b)->tv_usec;			      \
    if ((result)->tv_usec < 0) {					      \
      --(result)->tv_sec;						      \
      (result)->tv_usec += 1000000;					      \
    }									      \
  } while (0)
#endif

#define MAX_TABLE_SIZE 50

/** Count number of sort processes */
int get_sort_process_count();

/** Join tables and send result to voter processes. */
void process_tables(demo_table1_entry * table1, int rows1, demo_table2_entry * table2, int rows2);

#endif