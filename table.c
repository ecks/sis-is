/*
 * SIS-IS Demo program.
 * Stephen Sigwart
 * University of Delaware
 */

#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>

#include "table.h"

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