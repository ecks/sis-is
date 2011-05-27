/*
 * SIS-IS Demo program.
 * Stephen Sigwart
 * University of Delaware
 */

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>

#include <time.h>

#include "demo.h"
#include "voter.h"
#include "redundancy.h"
#include "table.h"

#include "../tests/sisis_api.h"
#include "../tests/sisis_process_types.h"

#define VERSION 1

// Thread to validate processed output
pthread_t validate_thread;

// Setup list of tables
table_group_t merge_table_group;
table_group_item_t * cur_merge_table_item;

// Expected table
demo_merge_table_entry expected_table[MAX_TABLE_SIZE];
int expected_table_size = 0;
struct timeval expected_table_received;
int expected_table_checked = 1;
pthread_mutex_t expected_table_mutex = PTHREAD_MUTEX_INITIALIZER;

int main (int argc, char ** argv)
{
	// Create thread to validate produced output
	pthread_create(&validate_thread, NULL, validator, NULL);
	
	// Setup list of tables
	merge_table_group.first = NULL;
	
	// Start main loop
	redundancy_main((uint64_t)SISIS_PTYPE_DEMO1_VOTER, (uint64_t)VERSION, VOTER_PORT, (uint64_t)SISIS_PTYPE_DEMO1_JOIN, process_input, vote_and_process, flush_inputs, REDUNDANCY_MAIN_FLAG_SKIP_REDUNDANCY, argc, argv);
}

/** Gets real answer from shim and validates results. */
void * validator(void * param)
{
	// Wait until sisis address is set up
	char tmp_addr[INET6_ADDRSTRLEN];
	get_sisis_addr(tmp_addr);
	
	// Make socket
	char port_str[16];
	sprintf(port_str, "%u", VOTER_ANSWER_PORT);
	int fd = make_socket(port_str);
	
	// Receive buffer
	int buflen;
	char buf[RECV_BUFFER_SIZE];
	
	// Timing info
	struct timeval tv_now, tv_diff;
	
	// Receive message
	while (1)
	{
		if ((buflen = recvfrom(fd, buf, RECV_BUFFER_SIZE, 0, NULL, NULL)) != -1)
		{
			// Deserialize
			int bytes_used;
			pthread_mutex_lock(&expected_table_mutex);
			
			// Check if the last table was checked against current result.
			int miss = 0;
			if (!expected_table_checked)
			{
				miss = 1;
				
				// Get current time
				gettimeofday(&tv_now, NULL);
				
				// Determine time that passed
				timersub(&tv_now, &expected_table_received, &tv_diff);
			}
			
			// Store new excepted information
			gettimeofday(&expected_table_received, NULL);
			expected_table_size = deserialize_join_table(expected_table, MAX_TABLE_SIZE, buf, buflen, &bytes_used);
			expected_table_checked = 0;
			pthread_mutex_unlock(&expected_table_mutex);
			
			// Output missed result
			if (miss)
			{
				// Create timestamp
				char ts[32];
				sprintf(ts, "%llu.%06llu", (uint64_t)tv_diff.tv_sec, (uint64_t)tv_diff.tv_usec);
				
				// Print message
				printf("Result NOT received in %s sec.\n", ts);
			}
		}
	}
	
	return NULL;
}

/** Process input from a single process. */
void process_input(char * buf, int buflen)
{
	// Allocate memory
	if (merge_table_group.first == NULL)
	{
		// Table
		cur_merge_table_item = malloc(sizeof(*cur_merge_table_item));
		merge_table_group.first = cur_merge_table_item;
	}
	else
	{
		// Table 1
		cur_merge_table_item->next = malloc(sizeof(*cur_merge_table_item->next));
		cur_merge_table_item = cur_merge_table_item->next;
	}
	
	// Check memory
	if (cur_merge_table_item == NULL)
	{ printf("Out of memory.\n"); exit(0); }
	cur_merge_table_item->table = malloc(sizeof(demo_merge_table_entry)*MAX_TABLE_SIZE);
	cur_merge_table_item->next = NULL;
	
	// Check memory
	if (cur_merge_table_item->table == NULL)
	{ printf("Out of memory.\n"); exit(0); }
	
	// Deserialize
	int bytes_used;
	cur_merge_table_item->table_size = deserialize_join_table(cur_merge_table_item->table, MAX_TABLE_SIZE, buf, buflen, &bytes_used);
}

/** Vote on input and process */
void vote_and_process()
{
	// Get number of inputs used
	int num_inputs_used = get_table_group_size(&merge_table_group);
	
	// Vote
	table_group_item_t * merge_table_item = merge_table_vote(&merge_table_group);
	if (!merge_table_item)
		printf("Failed to vote on tables.\n");
	else
	{
		// Print
		if (merge_table_item->table_size == -1)
			printf("Join error.\n");
		else
		{
			demo_merge_table_entry * join_table = (demo_merge_table_entry *)merge_table_item->table;
			
			// Get current time
			struct timeval tv_now, tv_diff;
			gettimeofday(&tv_now, NULL);
			
			int i;
			/*
			printf("Joined Rows: %d\n", merge_table_item->table_size);
			for (i = 0; i < merge_table_item->table_size; i++)
				printf("User Id: %d\tName: %s\tGender: %c\n", join_table[i].user_id, join_table[i].name, join_table[i].gender);
			*/
			pthread_mutex_lock(&expected_table_mutex);
			// Determine time that passed
			timersub(&tv_now, &expected_table_received, &tv_diff);
			// Compare against expected table
			short correct = 1;
			if (expected_table_size != merge_table_item->table_size)
				correct = 0;
			else
			{
				for (i = 0; correct && i < expected_table_size; i++)
					if (join_table[i].user_id != expected_table[i].user_id || strcmp(join_table[i].name, expected_table[i].name) != 0 || join_table[i].gender != expected_table[i].gender)
						correct = 0;
			}
			expected_table_checked = 1;
			pthread_mutex_unlock(&expected_table_mutex);
			
			// Create timestamp
			char ts[32];
			sprintf(ts, "%llu.%06llu", (uint64_t)tv_diff.tv_sec, (uint64_t)tv_diff.tv_usec);
			
			// Was this correct
			if (correct)
				printf("Correct result in %s sec.  Used %d inputs.\n", ts, num_inputs_used);
			else
				printf("WRONG result in %s sec.  Used %d inputs.\n", ts, num_inputs_used);
			
			printf("********************************** Voted Table *********************************\n");
			for (i = 0; i < merge_table_item->table_size; i++)
				printf("User Id: %d\tName: %s\tGender: %c\n", join_table[i].user_id, join_table[i].name, join_table[i].gender);
			
			// Check if any of the input tables disagreed with the voter on table
			int num_diff = 0;
			table_group_item_t * item = merge_table_group.first;
			while (item != NULL)
			{
				printf("********************************** Voted Table *********************************\n");
				demo_merge_table_entry * t1 = (demo_merge_table_entry *)(item->table);
				for (i = 0; i < item->table_size; i++)
					printf("User Id: %d\tName: %s\tGender: %c\n", t1[i].user_id, t1[i].name, t1[i].gender);
				
				if (merge_table_distance((demo_merge_table_entry *)(item->table), item->table_size, (demo_merge_table_entry *)merge_table_item->table, merge_table_item->table_size))
					num_diff++;
				
				// Get next item
				item = item->next;
			}
			if (num_diff)
				printf("\t%d inputs disagreed with voted table.\n", num_diff);
		}
	}
	
	// Clear tables
	table_group_free(&merge_table_group);
}

/** Flush inputs */
void flush_inputs()
{
	printf("Flushing inputs.\n");
	
	// Clear tables
	table_group_free(&merge_table_group);
}
