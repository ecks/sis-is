#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/socket.h>
#include <netinet/in.h>


#include <stdint.h>
#include <stdarg.h>

#define MIN(a,b) ((a) < (b) ? (a) : (b))

//#include <regex.h>

typedef struct {
	char * name;
	short bits;
	int flags;
#define SISIS_COMPONENT_FIXED					(1 << 0)
	uint64_t fixed_val;		// Fixed values only for if bits <= 64
} sisis_component_t;

/**
 * Construct SIS-IS address.
 *
 * sisis_addr String to store resulting SIS-IS/IP address in.
 * 
 * Returns zero on success.
 */
int sisis_create_addr(char * sisis_addr, sisis_component_t * components, int num_components, ...)
{
	va_list args;
	va_start(args, num_components);
	int comp = 0, bit = 0, consumed_bits = 0, comp_bits = components[comp].bits;
	unsigned short part = 0;
	uint64_t arg = (components[comp].flags & SISIS_COMPONENT_FIXED) ? components[comp].fixed_val : va_arg(args, uint64_t);
	for (; bit < 128; bit+=consumed_bits)
	{
		// Find next component with available bits
		while (comp_bits == 0 && comp + 1 < num_components)
		{
			comp++;
			comp_bits = components[comp].bits;
			if (components[comp].flags & SISIS_COMPONENT_FIXED)
				arg = components[comp].fixed_val;
			else
				arg = va_arg(args, uint64_t);
		}
		// Fill remainder with zeros if there are no more components
		if (comp_bits == 0)
		{
			consumed_bits = 16 - (bit % 16);
			part <<= consumed_bits;
		}
		// Otherwise, copy from arg
		else
		{
			consumed_bits = MIN(16 - (bit % 16), comp_bits);
			//printf("Consumed: %d\n", consumed_bits);
			//printf("Arg: %llu\n", arg);
			int i = 0;
			for (; i < consumed_bits; i++)
			{
				part <<= 1;
				comp_bits--;
				part |= (arg >> comp_bits) & 0x1;
			}
			//printf("%hu\n", part);
		}
		
		// Print now?
		if ((bit + consumed_bits) % 16 == 0)
		{
			sprintf(sisis_addr+(bit/16)*5, "%04hx%s", part, bit + consumed_bits == 128 ? "" : ":");
			part = 0;
		}
	}
	va_end(args);
	
	return 0;
}

/**
 * Split an SIS-IS address into components.
 *
 * sisis_addr SIS-IS/IP address
 */
void get_sisis_addr_components(char * sisis_addr, sisis_component_t * components, int num_components, ...)
{
	// Remove :: and ensure 4 numbers per 16 bits
	int idx, idx2, len = strlen(sisis_addr);
	char before[INET6_ADDRSTRLEN], after[INET6_ADDRSTRLEN], full[INET6_ADDRSTRLEN+1];
	full[0] = 0;
	// Count colons
	int colons = 0;
	for (idx = 0; idx < len; idx++)
		if (sisis_addr[idx] == ':')
			colons++;
	for (idx = 0, idx2 = 0; idx < len; idx++)
	{
		if (idx > 1 && sisis_addr[idx-1] == ':' && sisis_addr[idx] == ':')
		{
			for (colons--; colons < 7; colons++)
			{
				full[idx2++] = '0';
				full[idx2++] = ':';
			}
			if (idx + 1 == len)
				full[idx2++] = '0';
			full[idx2] = 0;
		}
		else
		{
			full[idx2] = sisis_addr[idx];
			idx2++;
			full[idx2] = 0;
		}
	}
	
	// Parse into args
	va_list args;
	va_start(args, num_components);
	int comp = 0, bit = 0, consumed_bits = 0, comp_bits = components[comp].bits;
	unsigned short part = 0;
	uint64_t * arg = va_arg(args, uint64_t *);
	memset(arg, 0, sizeof(*arg));
	char * full_ptr = full;
	for (; bit < 128; bit+=consumed_bits)
	{
		// Next part?
		if (bit % 16 == 0)
		{
			sscanf(full_ptr, "%4hx", &part);
			while (*full_ptr != '\0' && *full_ptr != ':')
				full_ptr++;
			if (*full_ptr == ':')
				full_ptr++;
			printf("%d - %04hx - %s\n", bit/16, part, full_ptr);
		}
		
		consumed_bits = 16;
		// Find next component with available bits
		while (comp_bits == 0 && comp + 1 < num_components)
		{
			comp++;
			comp_bits = components[comp].bits;
			arg = va_arg(args, uint64_t *);
			memset(arg, 0, sizeof(*arg));
		}
		// Make sure there are no more components
		if (comp_bits > 0)
		{
			consumed_bits = MIN(16 - (bit % 16), comp_bits);
			int i = 0;
			for (; i < consumed_bits; i++)
			{
				*arg <<= 1;
				comp_bits--;
				*arg |= (part >> (15-((bit+i)%16))) & 0x1;
			}
		}
	}
	va_end(args);
}

int main()
{
	// Components (at most 128)
	int num_components = 0;
	int total_bits = 0;
	sisis_component_t * components = malloc(sizeof(sisis_component_t)*128);
	memset(components, 0, sizeof(sisis_component_t)*128);
	
	// Open file
	FILE * file = fopen("sisis_format_v2.dat", "r");
	if (file)
	{
		// Read each line
		int line_num = 1;
		int line_size = 512;
		char * line = malloc(sizeof(char) * line_size);
		while (fgets(line, line_size, file))
		{
			// Read until we get the full line
			int len = strlen(line);
			while (len > 0 && line[len-1] != '\n')
			{
				line_size *= 2;
				line = realloc(line, line_size);
				// Set len to 0 to stop looping on EOF or error
				if (!fgets(line+len, line_size-len, file))
					len = 0;
				// Get new length
				else
					len = strlen(line);
			}
			
			/*
			regex_t regex1;
			regmatch_t match;
			if (regcomp(&regex1, "^\\s*([A-Za-z0-9_]+)\\s+([A-Za-z0-9_]+)", 0))
			{
				printf("Failed to compile regex.\n");
				exit(1);
			}
			*/
			
			// Parse line
			/*
			char * name = NULL, * type = NULL, * value = NULL;
			short bits;
			name = line;
			int i;
			for (i = 0, len = strlen(line); i < len; i++)
			{
				if (line[i] == ' ' || line[i] == '\t' || line[i] == '\r' || line[i] == '\n')
					line[i] = '\0';
			}
			int parts = sscanf(line, "%as", &name);
			printf("%s\n", name);
			*/
			
			int i, start = 0, field = 1;
#define SISIS_DAT_FIELD_NAME			1
#define SISIS_DAT_FIELD_BITS			2
#define SISIS_DAT_FIELD_FIXED			3
#define SISIS_DAT_FIELD_FIXED_VAL	4
			for (i = 0, len = strlen(line) + 1; i < len; i++)	// Intentionally go to '\0'
			{
				if (line[i] == ' ' || line[i] == '\t' || line[i] == '\r' || line[i] == '\n' || line[i] == '\0')
				{
					if (start == i)
						start++;
					else
					{
						// Copy to new buffer
						int buf_len = i - start + 1;	// Add 1 for '\0'
						char * buf = malloc(sizeof(char)*buf_len);
						buf[buf_len-1] = '\0';
						memcpy(buf, line+start, buf_len-1);
						start = i+1;
						
						/* Save to correct component */
						switch (field)
						{	
							// Name
							case SISIS_DAT_FIELD_NAME:
								components[num_components].name = buf;
								break;
							// # of bits
							case SISIS_DAT_FIELD_BITS:
								{
									int h = 0;
									for (; h < buf_len-1; h++)
									{
										if (buf[h] >= '0' && buf[h] <= '9')
											components[num_components].bits = components[num_components].bits*10+(buf[h]-'0');
										else
										{
											printf("Unexpected \"%c\" on line %d around \"%s\".\n", buf[h], line_num, buf);
											exit(1);
										}
									}
									free(buf);
									
									// Restrict components to 64 bits
									if (components[num_components].bits > 64)
									{
										printf("Components may be at most 64 bits.\n");
										exit(1);
									}
									
									// Check total number of bits
									total_bits += components[num_components].bits;
									if (total_bits > 128)
									{
										printf("Too many bits.\n");
										exit(1);
									}
								}
								break;
							// Fixed or default
							case SISIS_DAT_FIELD_FIXED:
								if (strcmp(buf, "fixed") == 0)
									components[num_components].flags |= SISIS_COMPONENT_FIXED;
								else
								{
									printf("Unexpected \"%s\" on line %d.  Expecting \"fixed\".\n", buf, line_num);
									exit(1);
								}
								free(buf);
								break;
							// Default value
							case SISIS_DAT_FIELD_FIXED_VAL:
								{
									short base = 10;
									int h = 0;
									for (; h < buf_len-1; h++)
									{
										if (buf[h] >= '0' && buf[h] <= '9')
											components[num_components].fixed_val = components[num_components].fixed_val*base+(buf[h]-'0');
										else if (base == 16 && buf[h] >= 'a' && buf[h] <= 'f')
											components[num_components].fixed_val = components[num_components].fixed_val*base+(buf[h]-'a'+10);
										else if (h == 1 && buf[0] == '0' && buf[1] <= 'x')
											base = 16;
										else
										{
											printf("Unexpected \"%c\" on line %d around \"%s\".\n", buf[h], line_num, buf);
											exit(1);
										}
									}
									free(buf);
								}
								break;
						}
						
						// Next field
						field++;
					}
				}
			}
			
			// Add to number of components
			num_components++;
			
			// Next line
			line_num++;
		}
		fclose(file);
		
		// Shrink memory for components
		components = realloc(components, sizeof(sisis_component_t)*num_components);
		int i = 0;
		for (; i< num_components; i++)
			printf("%s\t%hd\t%llx\n", components[i].name, components[i].bits, components[i].fixed_val);
		
		// Test address creation
		/*
		char sisis_addr[128];
		sisis_create_addr(sisis_addr, components, num_components, 0, 0, 0, 0, 0);
		printf("Address: %s\n", sisis_addr);
		sisis_create_addr(sisis_addr, components, num_components, 5, 0, 0, 0, 0);
		printf("Address: %s\n", sisis_addr);
		sisis_create_addr(sisis_addr, components, num_components, 5, 1, 1, 1356346, 573675472);
		printf("Address: %s\n", sisis_addr);
		sisis_create_addr(sisis_addr, components, num_components, 58375, 18, 546132, 1356346, 573675472);
		printf("Address: %s\n", sisis_addr);
		
		uint64_t prefix, sisis_version, process_type, process_version, sys_id, pid, timestamp;
		get_sisis_addr_components(sisis_addr, components, num_components, &prefix, &sisis_version, &process_type, &process_version, &sys_id, &pid, &timestamp);
		printf("%04x\t%u\t%u\t%u\t%u\t%u\t%u\n", (uint)prefix, (uint)sisis_version, (uint)process_type, (uint)process_version, (uint)sys_id, (uint)pid, (uint)timestamp);
		*/
		uint64_t prefix, sisis_version, process_type, process_version, sys_id, pid, timestamp;
		get_sisis_addr_components("fcff:1000:840:0:100:17c9:4d8a:3a71", components, num_components, &prefix, &sisis_version, &process_type, &process_version, &sys_id, &pid, &timestamp);
		printf("%04x\t%u\t%u\t%u\t%u\t%u\t%u\n", (uint)prefix, (uint)sisis_version, (uint)process_type, (uint)process_version, (uint)sys_id, (uint)pid, (uint)timestamp);
	}
	exit(0);
}