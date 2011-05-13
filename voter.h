#ifndef VOTER_H
#define VOTER_H

/** Gets real answer from shim and validates results. */
void * validator(void * param);

/** Process input from a single process. */
void process_input(char * buf, int buflen);

/** Vote on input and process */
void vote_and_process();

#endif