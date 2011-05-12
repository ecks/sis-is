#ifndef JOIN_H
#define JOIN_H

/** Process input from a single process. */
void process_input(char * buf, int buflen);

/** Vote on input and process */
void vote_and_process();

/** Join tables and send result to voter processes. */
void process_tables(demo_table1_entry * table1, int rows1, demo_table2_entry * table2, int rows2);

#endif
