/*
 * SIS-IS Demo program.
 * Stephen Sigwart
 * University of Delaware
 */

#ifndef TABLE_H
#define TABLE_H

#define TABLE1_NAME_LEN 64

/** Demo table 1 entry */
typedef struct {
	int user_id;
	char name[TABLE1_NAME_LEN];
} demo_table1_entry;

/** Demo table 2 entry */
typedef struct {
	int user_id;
	char gender;
} demo_table2_entry;

/** Merge table entry */
typedef struct {
	int user_id;
	char name[TABLE1_NAME_LEN];
	char gender;
} demo_merge_table_entry;

/** Serialize table 1.  Returns -1 if buffer is not long enough. */
int serialize_table1(demo_table1_entry * table, int size, char * buf, int bufsize);

/** Deserialize table 1.  Returns -1 if table is not big enough or other errors. */
int deserialize_table1(demo_table1_entry * table, int size, char * buf, int bufsize, int * bytes_used);

/** Serialize table 2.  Returns -1 if buffer is not long enough. */
int serialize_table2(demo_table2_entry * table, int size, char * buf, int bufsize);

/** Deserialize table 2.  Returns -1 if table is not big enough or other errors. */
int deserialize_table2(demo_table2_entry * table, int size, char * buf, int bufsize, int * bytes_used);

/** Serialize join table.  Returns -1 if buffer is not long enough. */
int serialize_join_table(demo_merge_table_entry * table, int size, char * buf, int bufsize);

/** Deserialize join table.  Returns -1 if table is not big enough or other errors. */
int deserialize_join_table(demo_merge_table_entry * table, int size, char * buf, int bufsize, int * bytes_used);

/** Compare user id of two table 1 entries */
int table1_user_id_comparator(const void * v_a, const void * v_b);

/** Sort table 1 by user_id */
void sort_table1_by_user_id(demo_table1_entry * table, int size);

/** Compare user id of two table 2 entries */
int table2_user_id_comparator(const void * v_a, const void * v_b);

/** Sort table 2 by user_id */
void sort_table2_by_user_id(demo_table2_entry * table, int size);

/** Merge join table 1 and 2.  Input tables should be pre-sorted.  Assumes user_id is a primary key. */
int merge_join(demo_table1_entry * table1, int size1, demo_table2_entry * table2, int size2, demo_merge_table_entry * table, int size);

typedef struct table_group_item {
	void * table;
	int table_size;
	struct table_group_item * next;
} table_group_item_t;

typedef struct {
	table_group_item_t * first;
} table_group_t;

/** Voter on a group of table 1s. */
void * table1_vote(table_group_t * tables);

/** Compute distance between 2 table 1s. */
int table1_distance(demo_table1_entry * table1, int size1, demo_table1_entry * table2, int size2);

#endif
