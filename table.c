/*
 * SIS-IS Demo program.
 * Stephen Sigwart
 * University of Delaware
 */

#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <math.h>

#include "table.h"

#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define ABS(a) ((a) < 0 ? (0 - (a)) : (a))


/** Serialize table 1.  Returns -1 if buffer is not long enough. */
int serialize_table1(demo_table1_entry * table, int size, char * buf, int bufsize)
{
	int len = 0;
	int i;
	
	// Serialize size of array
	if (len + sizeof(size) > bufsize)
		return -1;
	*(int*)(buf) = htonl(size);
	len += sizeof(size);
	
	// Serialize each row
	for (i = 0; i < size; i++)
	{
		// Add length
		int len2 = sizeof(table[i].user_id) + TABLE1_NAME_LEN;
		if (len + len2 > bufsize)
			return -1;
		
		// Copy user id
		*(int*)(buf+len) = htonl(table[i].user_id);
		len += sizeof(table[i].user_id);
		
		// Copy name
		memcpy(buf+len, table[i].name, TABLE1_NAME_LEN);
		len += TABLE1_NAME_LEN;
	}
	return len;
}

/** Deserialize table 1.  Returns -1 if table is not big enough or other errors. */
int deserialize_table1(demo_table1_entry * table, int size, char * buf, int bufsize, int * bytes_used)
{
	int rows = 0;
	int i, pos = 0;
	
	// Get size of array
	if (pos + sizeof(rows) > bufsize)
		return -1;
	rows = ntohl(*(int*)(buf));
	pos += sizeof(rows);
	if (rows > size)
		return -1;
	
	// Get each row
	for (i = 0; i < rows; i++)
	{
		// Check length
		int len2 = sizeof(table[i].user_id) + TABLE1_NAME_LEN;
		if (pos + len2 > bufsize)
			return -1;
		
		// Copy user id
		table[i].user_id = ntohl(*(int*)(buf+pos));
		pos += sizeof(table[i].user_id);
		
		// Copy name
		memcpy(table[i].name, buf+pos, TABLE1_NAME_LEN);
		pos += TABLE1_NAME_LEN;
	}
	
	if (bytes_used != NULL)
		*bytes_used = pos;
	
	return rows;
}

/** Serialize table 2.  Returns -1 if buffer is not long enough. */
int serialize_table2(demo_table2_entry * table, int size, char * buf, int bufsize)
{
	int len = 0;
	int i;
	
	// Serialize size of array
	if (len + sizeof(size) > bufsize)
		return -1;
	*(int*)(buf) = htonl(size);
	len += sizeof(size);
	
	// Serialize each row
	for (i = 0; i < size; i++)
	{
		// Add length
		int len2 = sizeof(table[i].user_id) + 1;
		if (len + len2 > bufsize)
			return -1;
		
		// Copy user id
		*(int*)(buf+len) = htonl(table[i].user_id);
		len += sizeof(table[i].user_id);
		
		// Copy gender
		buf[len] = table[i].gender;
		len++;
	}
	return len;
}

/** Deserialize table 2.  Returns -1 if table is not big enough or other errors. */
int deserialize_table2(demo_table2_entry * table, int size, char * buf, int bufsize, int * bytes_used)
{
	int rows = 0;
	int i, pos = 0;
	
	// Get size of array
	if (pos + sizeof(rows) > bufsize)
		return -1;
	rows = ntohl(*(int*)(buf));
	pos += sizeof(rows);
	if (rows > size)
		return -1;
	
	// Get each row
	for (i = 0; i < rows; i++)
	{
		// Check length
		int len2 = sizeof(table[i].user_id) + 1;
		if (pos + len2 > bufsize)
			return -1;
		
		// Copy user id
		table[i].user_id = ntohl(*(int*)(buf+pos));
		pos += sizeof(table[i].user_id);
		
		// Copy gender
		table[i].gender = buf[pos];
		pos++;
	}
	
	if (bytes_used != NULL)
		*bytes_used = pos;
	
	return rows;
}

/** Serialize join table.  Returns -1 if buffer is not long enough. */
int serialize_join_table(demo_merge_table_entry * table, int size, char * buf, int bufsize)
{
	int len = 0;
	int i;
	
	// Serialize size of array
	if (len + sizeof(size) > bufsize)
		return -1;
	*(int*)(buf) = htonl(size);
	len += sizeof(size);
	
	// Serialize each row
	for (i = 0; i < size; i++)
	{
		// Add length
		int len2 = sizeof(table[i].user_id) + TABLE1_NAME_LEN + 1;
		if (len + len2 > bufsize)
			return -1;
		
		// Copy user id
		*(int*)(buf+len) = htonl(table[i].user_id);
		len += sizeof(table[i].user_id);
		
		// Copy name
		memcpy(buf+len, table[i].name, TABLE1_NAME_LEN);
		len += TABLE1_NAME_LEN;
		
		// Copy gender
		buf[len] = table[i].gender;
		len++;
	}
	return len;
}

/** Deserialize join table.  Returns -1 if table is not big enough or other errors. */
int deserialize_join_table(demo_merge_table_entry * table, int size, char * buf, int bufsize, int * bytes_used)
{
	int rows = 0;
	int i, pos = 0;
	
	// Get size of array
	if (pos + sizeof(rows) > bufsize)
		return -1;
	rows = ntohl(*(int*)(buf));
	pos += sizeof(rows);
	if (rows > size)
		return -1;
	
	// Get each row
	for (i = 0; i < rows; i++)
	{
		// Check length
		int len2 = sizeof(table[i].user_id) + TABLE1_NAME_LEN + 1;
		if (pos + len2 > bufsize)
			return -1;
		
		// Copy user id
		table[i].user_id = ntohl(*(int*)(buf+pos));
		pos += sizeof(table[i].user_id);
		
		// Copy name
		memcpy(table[i].name, buf+pos, TABLE1_NAME_LEN);
		pos += TABLE1_NAME_LEN;
		
		// Copy gender
		table[i].gender = buf[pos];
		pos++;
	}
	
	if (bytes_used != NULL)
		*bytes_used = pos;
	
	return rows;
}

/** Compare user id of two table 1 entries */
int table1_user_id_comparator(const void * v_a, const void * v_b)
{
	demo_table1_entry * a = (demo_table1_entry *)v_a;
	demo_table1_entry * b = (demo_table1_entry *)v_b;
	if (a->user_id < b->user_id)
		return -1;
	else if (a->user_id > b->user_id)
		return 1;
	return 0;
}

/** Sort table 1 by user_id */
void sort_table1_by_user_id(demo_table1_entry * table, int size)
{
	qsort(table, size, sizeof(demo_table1_entry), table1_user_id_comparator);
}

/** Compare user id of two table 2 entries */
int table2_user_id_comparator(const void * v_a, const void * v_b)
{
	demo_table2_entry * a = (demo_table2_entry *)v_a;
	demo_table2_entry * b = (demo_table2_entry *)v_b;
	if (a->user_id < b->user_id)
		return -1;
	else if (a->user_id > b->user_id)
		return 1;
	return 0;
}

/** Sort table 2 by user_id */
void sort_table2_by_user_id(demo_table2_entry * table, int size)
{
	qsort(table, size, sizeof(demo_table2_entry), table2_user_id_comparator);
}

/** Merge join table 1 and 2.  Input tables should be pre-sorted.  Assumes user_id is a primary key. */
int merge_join(demo_table1_entry * table1, int size1, demo_table2_entry * table2, int size2, demo_merge_table_entry * table, int size)
{
	int rows = 0;
	
	int idx1 = 0, idx2 = 0;
	for (; idx1 < size1 && idx2 < size2; idx1++)
	{
		// Find matching user id
		for (; idx2 < size2 && table1[idx1].user_id > table2[idx2].user_id; idx2++);
		
		if (idx2 < size2 && table1[idx1].user_id == table2[idx2].user_id)
		{
			if (rows >= size)
				return -1;
			table[rows].user_id = table1[idx1].user_id;
			memcpy(table[rows].name, table1[idx1].name, TABLE1_NAME_LEN);
			table[rows].gender = table2[idx2].gender;
			
			rows++;
		}
	}
	
	return rows;
}

/** Voter on a group of table 1s. */
table_group_item_t * table1_vote(table_group_t * tables)
{
	table_group_item_t * winner = NULL;
	
	// Compare each table against all others
	int dist, min_dist;
	table_group_item_t * item = tables->first;
	table_group_item_t * item2;
	while (item != NULL)
	{
		dist = 0;
		item2 = tables->first;
		while (item2 != NULL)
		{
			// Don't compare against itself
			if (item2 != item)
			{
				demo_table1_entry * t1 = (demo_table1_entry *)(item->table);
				demo_table1_entry * t2 = (demo_table1_entry *)(item2->table);
				dist += table1_distance(t1, item->table_size, t2, item2->table_size);
			}
			
			// Get next item
			item2 = item2->next;
		}
		
		// Check if this is the lowest distance
		if (winner == NULL || dist < min_dist)
		{
			winner = item;
			min_dist = dist;
		}
		
		// Get next item
		item = item->next;
	}
	
	return winner;
}

/** Compute distance between 2 table 1s. */
int table1_distance(demo_table1_entry * table1, int size1, demo_table1_entry * table2, int size2)
{
	int dist = 0;
	
	// Check sizes
	dist += ABS(size1 - size2) * 3;	// 3 is an arbitrary weight
	
	// Check each entry
	int i;
	for (i = 0; i < MIN(size1, size2); i++)
	{
		if (table1[i].user_id != table2[i].user_id)
			dist += 1;
		if (strcmp(table1[i].name, table2[i].name))
			dist += 1;
	}
	
	return dist;
}

/** Voter on a group of table 2s. */
table_group_item_t * table2_vote(table_group_t * tables)
{
	table_group_item_t * winner = NULL;
	
	// Compare each table against all others
	int dist, min_dist;
	table_group_item_t * item = tables->first;
	table_group_item_t * item2;
	while (item != NULL)
	{
		dist = 0;
		item2 = tables->first;
		while (item2 != NULL)
		{
			// Don't compare against itself
			if (item2 != item)
			{
				demo_table2_entry * t1 = (demo_table2_entry *)(item->table);
				demo_table2_entry * t2 = (demo_table2_entry *)(item2->table);
				dist += table2_distance(t1, item->table_size, t2, item2->table_size);
			}
			
			// Get next item
			item2 = item2->next;
		}
		
		// Check if this is the lowest distance
		if (winner == NULL || dist < min_dist)
		{
			winner = item;
			min_dist = dist;
		}
		
		// Get next item
		item = item->next;
	}
	
	return winner;
}

/** Compute distance between 2 table 2s. */
int table2_distance(demo_table2_entry * table1, int size1, demo_table2_entry * table2, int size2)
{
	int dist = 0;
	
	// Check sizes
	dist += ABS(size1 - size2) * 3;	// 3 is an arbitrary weight
	
	// Check each entry
	int i;
	for (i = 0; i < MIN(size1, size2); i++)
	{
		if (table1[i].user_id != table2[i].user_id)
			dist += 1;
		if (table1[i].gender != table2[i].gender)
			dist += 1;
	}
	
	return dist;
}

/** Voter on a group of join tables. */
table_group_item_t * merge_table_vote(table_group_t * tables)
{
	table_group_item_t * winner = NULL;
	
	// Compare each table against all others
	int dist, min_dist;
	table_group_item_t * item = tables->first;
	table_group_item_t * item2;
	while (item != NULL)
	{
		dist = 0;
		item2 = tables->first;
		while (item2 != NULL)
		{
			// Don't compare against itself
			if (item2 != item)
			{
				demo_merge_table_entry * t1 = (demo_merge_table_entry *)(item->table);
				demo_merge_table_entry * t2 = (demo_merge_table_entry *)(item2->table);
				dist += merge_table_distance(t1, item->table_size, t2, item2->table_size);
			}
			
			// Get next item
			item2 = item2->next;
		}
		
		// Check if this is the lowest distance
		if (winner == NULL || dist < min_dist)
		{
			winner = item;
			min_dist = dist;
		}
		
		// Get next item
		item = item->next;
	}
	
	return winner;
}

/** Compute distance between 2 join tables. */
int merge_table_distance(demo_merge_table_entry * table1, int size1, demo_merge_table_entry * table2, int size2)
{
	int dist = 0;
	
	// Check sizes
	dist += ABS(size1 - size2) * 3;	// 3 is an arbitrary weight
	
	// Check each entry
	int i;
	for (i = 0; i < MIN(size1, size2); i++)
	{
		if (table1[i].user_id != table2[i].user_id)
			dist += 1;
		if (strcmp(table1[i].name, table2[i].name))
			dist += 1;
		if (table1[i].gender != table2[i].gender)
			dist += 1;
	}
	
	return dist;
}

/** Free a group of tables and data inside */
int table_group_free(table_group_t * tables)
{
	table_group_item_t * item = tables->first, * item2;
	tables->first = NULL;
	while (item != NULL)
	{
		// Get next item
		item2 = item->next;
		
		// Free memory
		free(item->table);
		free(item);
		
		// Set next item
		item = item2;
	}
}
