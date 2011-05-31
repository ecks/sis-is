#ifndef SORT_H
#define SORT_H

#include <stdlib.h>
#include "table.h"

/** Process input from a single process. */
void process_input(char * buf, int buflen);

/** Vote on input and process */
void vote_and_process();

/** Sort tables and send results to join processes. */
void process_tables(demo_table1_entry * table1, int rows1, demo_table2_entry * table2, int rows2);

/** Bubble sort */
void bubble_sort(void * base, size_t num, size_t size, int (*comparator) (const void *, const void *));

#endif