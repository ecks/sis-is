/*
 * SIS-IS Test program.
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
#include <sys/utsname.h>
#include <sys/statvfs.h>
#include <dirent.h>

#include "machine_monitor.h"

#include "../tests/sisis_api.h"
#include "../tests/sisis_process_types.h"

#define VERSION 1

int sockfd = -1, con = -1;
uint64_t ptype, host_num, pid;
uint64_t timestamp;

void close_listener()
{
	if (sockfd != -1)
	{
		printf("Closing listening socket...\n");
		close(sockfd);
		
		// Unregister
		sisis_unregister(NULL, (uint64_t)SISIS_PTYPE_MACHINE_MONITOR, (uint64_t)VERSION, host_num, pid, timestamp);
		
		sockfd = -1;
	}
}

void terminate(int signal)
{
	printf("Terminating...\n");
	close_listener();
	if (con != -1)
	{
		printf("Closing remove connection socket...\n");
		close(con);
	}
	exit(0);
}

/* Get number of CPUs. */
short get_cpu_count()
{
	short cnt = 0;
	
	FILE * proc_cpuinfo = fopen("/proc/cpuinfo", "r");
	if (proc_cpuinfo != NULL)
	{
		char line[256];
		while (fgets(line, 256, proc_cpuinfo))
			// Check if the line starts with processor
			if (memcmp(line,"processor",9) == 0)
				cnt++;
		
		// Close file
		fclose(proc_cpuinfo);
	}
	
	return cnt;
}

/* Past CPU usage observations.  Stored as percent * 10 so that we can keep
  precisions to 1/10 of a percent without using floats. */
// Keep 10 min worth
#define MAX_CPU_USAGE_SECONDLY_HISTORY_ITEMS 600
short cpu_usage_secondly_history[MAX_CPU_USAGE_SECONDLY_HISTORY_ITEMS];
short cpu_usage_secondly_history_head = 0;
short cpu_usage_secondly_history_items = 0;
// Keep 1 hrs worth
#define MAX_CPU_USAGE_30SECOND_HISTORY_ITEMS 120
short cpu_usage_30second_history[MAX_CPU_USAGE_30SECOND_HISTORY_ITEMS];
short cpu_usage_30second_history_head = 0;
short cpu_usage_30second_history_items = 0;
// Keep 24 hrs worth
#define MAX_CPU_USAGE_MINUTELY_HISTORY_ITEMS 1440
short cpu_usage_minutely_history[MAX_CPU_USAGE_MINUTELY_HISTORY_ITEMS];
short cpu_usage_minutely_history_head = 0;
short cpu_usage_minutely_history_items = 0;
// Keep 7 days worth
#define MAX_CPU_USAGE_30MINUTE_HISTORY_ITEMS 336
short cpu_usage_30minute_history[MAX_CPU_USAGE_30MINUTE_HISTORY_ITEMS];
short cpu_usage_30minute_history_head = 0;
short cpu_usage_30minute_history_items = 0;

// Mutex
pthread_mutex_t cpu_usage_mutex;


/* Get CPU usage as a percent. */
void * get_cpu_usage_thread(void * nil)
{
	// Use double to prevent overflow
	double startUserTotal, startNiceTotal, startSystTotal, startIdleTotal;
	double startUserTotal_30sec, startNiceTotal_30sec, startSystTotal_30sec, startIdleTotal_30sec;
	double startUserTotal_min, startNiceTotal_min, startSystTotal_min, startIdleTotal_min;
	double startUserTotal_30min, startNiceTotal_30min, startSystTotal_30min, startIdleTotal_30min;
	double endUserTotal, endNiceTotal, endSystTotal, endIdleTotal;
	
	// First reading
	FILE * proc_stat = fopen("/proc/stat", "r");
	if (proc_stat != NULL)
	{
		fscanf(proc_stat, "cpu %lf %lf %lf %lf", &startUserTotal, &startNiceTotal, &startSystTotal, &startIdleTotal);
		fclose(proc_stat);
		
		startUserTotal_30sec = startUserTotal_min = startUserTotal_30min = startUserTotal;
		startNiceTotal_30sec = startNiceTotal_min = startNiceTotal_30min = startNiceTotal;
		startSystTotal_30sec = startSystTotal_min = startSystTotal_30min = startSystTotal;
		startIdleTotal_30sec = startIdleTotal_min = startIdleTotal_30min = startIdleTotal;
	}
	
	// Counters
	short seconds_to_next_30second_reading = 30;
	short seconds_to_next_minutely_reading = 60;
	short seconds_to_next_30minute_reading = 1800;
	
	short usage;
	
	while (1)
	{
		// Sleep
		sleep(1);
		
		// Read again
		proc_stat = fopen("/proc/stat", "r");
		if (proc_stat != NULL)
		{
			fscanf(proc_stat, "cpu %lf %lf %lf %lf", &endUserTotal, &endNiceTotal, &endSystTotal, &endIdleTotal);
			fclose(proc_stat);
		}
		
		// Lock mutex
		pthread_mutex_lock(&cpu_usage_mutex);
		
		// Secondly history
		usage = (short)(endUserTotal+endNiceTotal+endSystTotal-startUserTotal-startNiceTotal-startSystTotal)/(endUserTotal+endNiceTotal+endSystTotal+endIdleTotal-startUserTotal-startNiceTotal-startSystTotal-startIdleTotal)*1000;
		startUserTotal = endUserTotal; startNiceTotal = endNiceTotal; startSystTotal = endSystTotal; startIdleTotal = endIdleTotal;
		cpu_usage_secondly_history[(cpu_usage_secondly_history_head+cpu_usage_secondly_history_items)%MAX_CPU_USAGE_SECONDLY_HISTORY_ITEMS] = usage;
		if (cpu_usage_secondly_history_items == MAX_CPU_USAGE_SECONDLY_HISTORY_ITEMS)
			cpu_usage_secondly_history_head = (cpu_usage_secondly_history_head+1)%MAX_CPU_USAGE_SECONDLY_HISTORY_ITEMS;
		else
			cpu_usage_secondly_history_items++;
		
		// 30 Second history
		seconds_to_next_30second_reading--;
		if (seconds_to_next_30second_reading == 0)
		{
			usage = (short)(endUserTotal+endNiceTotal+endSystTotal-startUserTotal_30sec-startNiceTotal_30sec-startSystTotal_30sec)/(endUserTotal+endNiceTotal+endSystTotal+endIdleTotal-startUserTotal_30sec-startNiceTotal_30sec-startSystTotal_30sec-startIdleTotal_30sec)*1000;
			startUserTotal_30sec = endUserTotal; startNiceTotal_30sec = endNiceTotal; startSystTotal_30sec = endSystTotal; startIdleTotal_30sec = endIdleTotal;
			seconds_to_next_30second_reading = 30;
			cpu_usage_30second_history[(cpu_usage_30second_history_head+cpu_usage_30second_history_items)%MAX_CPU_USAGE_30SECOND_HISTORY_ITEMS] = usage;
			if (cpu_usage_30second_history_items == MAX_CPU_USAGE_30SECOND_HISTORY_ITEMS)
				cpu_usage_30second_history_head = (cpu_usage_30second_history_head+1)%MAX_CPU_USAGE_30SECOND_HISTORY_ITEMS;
			else
				cpu_usage_30second_history_items++;
		}
		
		// Minutely history
		seconds_to_next_minutely_reading--;
		if (seconds_to_next_minutely_reading == 0)
		{
			usage = (short)(endUserTotal+endNiceTotal+endSystTotal-startUserTotal_min-startNiceTotal_min-startSystTotal_min)/(endUserTotal+endNiceTotal+endSystTotal+endIdleTotal-startUserTotal_min-startNiceTotal_min-startSystTotal_min-startIdleTotal_min)*1000;
			startUserTotal_min = endUserTotal; startNiceTotal_min = endNiceTotal; startSystTotal_min = endSystTotal; startIdleTotal_min = endIdleTotal;
			seconds_to_next_minutely_reading = 60;
			cpu_usage_minutely_history[(cpu_usage_minutely_history_head+cpu_usage_minutely_history_items)%MAX_CPU_USAGE_MINUTELY_HISTORY_ITEMS] = usage;
			if (cpu_usage_minutely_history_items == MAX_CPU_USAGE_MINUTELY_HISTORY_ITEMS)
				cpu_usage_minutely_history_head = (cpu_usage_minutely_history_head+1)%MAX_CPU_USAGE_MINUTELY_HISTORY_ITEMS;
			else
				cpu_usage_minutely_history_items++;
		}
		
		// 30 Minute history
		seconds_to_next_30minute_reading--;
		if (seconds_to_next_30minute_reading == 0)
		{
			usage = (short)(endUserTotal+endNiceTotal+endSystTotal-startUserTotal_30min-startNiceTotal_30min-startSystTotal_30min)/(endUserTotal+endNiceTotal+endSystTotal+endIdleTotal-startUserTotal_30min-startNiceTotal_30min-startSystTotal_30min-startIdleTotal_30min)*1000;
			startUserTotal_30min = endUserTotal; startNiceTotal_30min = endNiceTotal; startSystTotal_30min = endSystTotal; startIdleTotal_30min = endIdleTotal;
			seconds_to_next_30minute_reading = 1800;
			cpu_usage_30minute_history[(cpu_usage_30minute_history_head+cpu_usage_30minute_history_items)%MAX_CPU_USAGE_30MINUTE_HISTORY_ITEMS] = usage;
			if (cpu_usage_30minute_history_items == MAX_CPU_USAGE_30MINUTE_HISTORY_ITEMS)
				cpu_usage_30minute_history_head = (cpu_usage_30minute_history_head+1)%MAX_CPU_USAGE_30MINUTE_HISTORY_ITEMS;
			else
				cpu_usage_30minute_history_items++;
		}
		
		// Unlock mutex
		pthread_mutex_unlock(&cpu_usage_mutex);
	}
}

/* Get CPU usage as a percent. */
short get_cpu_usage()
{
	short usage;
	
	// Lock mutex
	pthread_mutex_lock(&cpu_usage_mutex);
	
	while (cpu_usage_secondly_history_items == 0)
	{
		// Unlock mutex
		pthread_mutex_unlock(&cpu_usage_mutex);
		
		// Wait
		sleep(1);
		
		// Lock mutex
		pthread_mutex_lock(&cpu_usage_mutex);
	}
	
	// Get usage
	usage = cpu_usage_secondly_history[(cpu_usage_secondly_history_head+cpu_usage_secondly_history_items-1)%MAX_CPU_USAGE_SECONDLY_HISTORY_ITEMS];
	
	// Unlock mutex
	pthread_mutex_unlock(&cpu_usage_mutex);
	
	return usage;
}

struct memory_stats
{
	short usage_percent;
	double free,total;	// Use double to prevent overflow
};



/* Past CPU usage observations.  Stored as percent * 10 so that we can keep
  precisions to 1/10 of a percent without using floats. */
// Keep 10 min worth
#define MAX_MEMORY_USAGE_SECONDLY_HISTORY_ITEMS 600
short memory_usage_secondly_history[MAX_MEMORY_USAGE_SECONDLY_HISTORY_ITEMS];
short memory_usage_secondly_history_head = 0;
short memory_usage_secondly_history_items = 0;
// Keep 1 hrs worth
#define MAX_MEMORY_USAGE_30SECOND_HISTORY_ITEMS 120
short memory_usage_30second_history[MAX_MEMORY_USAGE_30SECOND_HISTORY_ITEMS];
short memory_usage_30second_history_head = 0;
short memory_usage_30second_history_items = 0;
// Keep 24 hrs worth
#define MAX_MEMORY_USAGE_MINUTELY_HISTORY_ITEMS 1440
short memory_usage_minutely_history[MAX_MEMORY_USAGE_MINUTELY_HISTORY_ITEMS];
short memory_usage_minutely_history_head = 0;
short memory_usage_minutely_history_items = 0;
// Keep 7 days worth
#define MAX_MEMORY_USAGE_30MINUTE_HISTORY_ITEMS 336
short memory_usage_30minute_history[MAX_MEMORY_USAGE_30MINUTE_HISTORY_ITEMS];
short memory_usage_30minute_history_head = 0;
short memory_usage_30minute_history_items = 0;

// Mutex
pthread_mutex_t memory_usage_mutex;

/* Get memory usage as a percent. */
void * get_memory_usage_thread(void * nil)
{
	struct memory_stats stats;
	double usage_sum_30sec = 0;
	double usage_sum_min = 0;
	double usage_sum_30min = 0;
	
	// Counters
	short seconds_to_next_30second_reading = 30;
	short seconds_to_next_minutely_reading = 60;
	short seconds_to_next_30minute_reading = 1800;
	
	short usage;
	
	while (1)
	{
		// Get stats
		stats = get_memory_usage();
		
		// Lock mutex
		pthread_mutex_lock(&memory_usage_mutex);
		
		// Secondly history
		memory_usage_secondly_history[(memory_usage_secondly_history_head+memory_usage_secondly_history_items)%MAX_MEMORY_USAGE_SECONDLY_HISTORY_ITEMS] = stats.usage_percent;
		if (memory_usage_secondly_history_items == MAX_MEMORY_USAGE_SECONDLY_HISTORY_ITEMS)
			memory_usage_secondly_history_head = (memory_usage_secondly_history_head+1)%MAX_MEMORY_USAGE_SECONDLY_HISTORY_ITEMS;
		else
			memory_usage_secondly_history_items++;
		
		// 30 Second history
		usage_sum_30sec += stats.usage_percent;
		seconds_to_next_30second_reading--;
		if (seconds_to_next_30second_reading == 0)
		{
			usage = (short)usage_sum_30sec/30;
			usage_sum_30sec = 0;
			seconds_to_next_30second_reading = 30;
			memory_usage_30second_history[(memory_usage_30second_history_head+memory_usage_30second_history_items)%MAX_MEMORY_USAGE_30SECOND_HISTORY_ITEMS] = usage;
			if (memory_usage_30second_history_items == MAX_MEMORY_USAGE_30SECOND_HISTORY_ITEMS)
				memory_usage_30second_history_head = (memory_usage_30second_history_head+1)%MAX_MEMORY_USAGE_30SECOND_HISTORY_ITEMS;
			else
				memory_usage_30second_history_items++;
		}
		
		// Minutely history
		usage_sum_min += stats.usage_percent;
		seconds_to_next_minutely_reading--;
		if (seconds_to_next_minutely_reading == 0)
		{
			usage = (short)usage_sum_min/30;
			usage_sum_min = 0;
			seconds_to_next_minutely_reading = 30;
			memory_usage_minutely_history[(memory_usage_minutely_history_head+memory_usage_minutely_history_items)%MAX_MEMORY_USAGE_MINUTELY_HISTORY_ITEMS] = usage;
			if (memory_usage_minutely_history_items == MAX_MEMORY_USAGE_MINUTELY_HISTORY_ITEMS)
				memory_usage_minutely_history_head = (memory_usage_minutely_history_head+1)%MAX_MEMORY_USAGE_MINUTELY_HISTORY_ITEMS;
			else
				memory_usage_minutely_history_items++;
		}
		
		// 30 Minute history
		usage_sum_30min += stats.usage_percent;
		seconds_to_next_30minute_reading--;
		if (seconds_to_next_30minute_reading == 0)
		{
			usage = (short)usage_sum_30min/30;
			usage_sum_30min = 0;
			seconds_to_next_30minute_reading = 30;
			memory_usage_30minute_history[(memory_usage_30minute_history_head+memory_usage_30minute_history_items)%MAX_MEMORY_USAGE_30MINUTE_HISTORY_ITEMS] = usage;
			if (memory_usage_30minute_history_items == MAX_MEMORY_USAGE_30MINUTE_HISTORY_ITEMS)
				memory_usage_30minute_history_head = (memory_usage_30minute_history_head+1)%MAX_MEMORY_USAGE_30MINUTE_HISTORY_ITEMS;
			else
				memory_usage_30minute_history_items++;
		}
		
		// Unlock mutex
		pthread_mutex_unlock(&memory_usage_mutex);
		
		// Sleep
		sleep(1);
	}
}

/** Get memory usage. */
struct memory_stats get_memory_usage()
{
	struct memory_stats stats;
	memset(&stats, 0, sizeof(stats));
	
	FILE * proc_meminfo = fopen("/proc/meminfo", "r");
	if (proc_meminfo != NULL)
	{
		fscanf(proc_meminfo, "MemTotal:");
		fscanf(proc_meminfo, "%lf kB\n", &stats.total);
		fscanf(proc_meminfo, "MemFree:");
		fscanf(proc_meminfo, "%lf kB\n", &stats.free);
		fclose(proc_meminfo);
		
		stats.usage_percent = (short)((stats.total-stats.free)/stats.total*1000);
	}
	return stats;
}

/** Get total number of running processes. */
long get_num_processes()
{
	long procs = 0;
	
	DIR * dir = opendir("/proc");
	if (dir)
	{
		struct dirent * entry;
		while (entry = readdir(dir))
		{
			int i = 0;
			if (entry->d_name[0] >= '0' && entry->d_name[0] <= '9')
				procs++;
		}
		closedir(dir);
	}
	
	return procs;
}

int main (int argc, char ** argv)
{
	// Get start time
	timestamp = time(NULL);
	
	// Check number of args
	if (argc != 2)
	{
		printf("Usage: %s <host_num>\n", argv[0]);
		exit(1);
	}
	
	// Get host number
	sscanf (argv[1], "%llu", &host_num);
	char sisis_addr[INET6_ADDRSTRLEN+1];
	
	// Get pid
	pid = getpid();
	
	// Register address
	if (sisis_register(sisis_addr, (uint64_t)SISIS_PTYPE_MACHINE_MONITOR, (uint64_t)VERSION, host_num, pid, timestamp) != 0)
	{
		printf("Failed to register SIS-IS address.\n");
		exit(1);
	}
	
	// Status
	printf("Opening socket at %s on port %i.\n", sisis_addr, MACHINE_MONITOR_PORT);
	
	// Set up socket address info
	struct addrinfo hints, *addr;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET6;	// IPv6
	hints.ai_socktype = SOCK_DGRAM;
	char port_str[8];
	sprintf(port_str, "%u", MACHINE_MONITOR_PORT);
	getaddrinfo(sisis_addr, port_str, &hints, &addr);
	
	// Create socket
	if ((sockfd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol)) == -1)
	{
		printf("Failed to open socket.\n");
		exit(1);
	}
	
	// Bind to port
	if (bind(sockfd, addr->ai_addr, addr->ai_addrlen) == -1)
	{
		printf("Failed to bind socket to port.\n");
		close_listener();
		exit(2);
	}
	
	// Status message
	inet_ntop(AF_INET6, &((struct sockaddr_in6 *)(addr->ai_addr))->sin6_addr, sisis_addr, INET6_ADDRSTRLEN);
	printf("Socket opened at %s on port %u.\n", sisis_addr, ntohs(((struct sockaddr_in *)(addr->ai_addr))->sin_port));
	
	// Set up signal handling
	signal(SIGABRT, terminate);
	signal(SIGTERM, terminate);
	signal(SIGINT, terminate);
	
	// Start thread to record CPU usage
	pthread_mutex_init(&cpu_usage_mutex, NULL);
	pthread_t cpu_usage_thread;
	pthread_create(&cpu_usage_thread, NULL, get_cpu_usage_thread, NULL);
	
	// Start thread to record memory usage
	pthread_mutex_init(&memory_usage_mutex, NULL);
	pthread_t memory_usage_thread;
	pthread_create(&memory_usage_thread, NULL, get_memory_usage_thread, NULL);
	
	// Wait for message
	struct sockaddr_in6 remote_addr;
	int len;
	char buf[1024];
	socklen_t addr_size = sizeof remote_addr;
	while ((len = recvfrom(sockfd, buf, 1024, 0, (struct sockaddr *)&remote_addr, &addr_size)) != -1)
	{
		char send_buf[16384];
		
		// Memory
		struct memory_stats mem_stats = get_memory_usage();
		sprintf(send_buf, "MemoryUsage: %hd.%hd%%\n", mem_stats.usage_percent/10, mem_stats.usage_percent%10);
		sprintf(send_buf, "%sFreeMemory: %0.0lf\n", send_buf, mem_stats.free);
		sprintf(send_buf, "%sTotalMemory: %0.0lf\n", send_buf, mem_stats.total);
		
		// CPU
		short usage = get_cpu_usage();
		sprintf(send_buf, "%sCPU: %hd.%hd%%\n", send_buf, usage/10, usage%10);
		sprintf(send_buf, "%sCPUs: %hd\n", send_buf, get_cpu_count());
		
		// Number of processes
		sprintf(send_buf, "%sProcesses: %ld\n", send_buf, get_num_processes());
		
		// Get file system info
		struct statvfs statvfs_info;
		if (statvfs("/", &statvfs_info) == 0)
		{
			double avail = (double)statvfs_info.f_bavail*statvfs_info.f_frsize/1024;
			double total = (double)statvfs_info.f_blocks*statvfs_info.f_frsize/1024;
			char * units[4] = {"KB", "MB", "GB", "TB"};
			int unit_idx = 0;
			while (unit_idx < 3 && avail > 1024)
			{
				avail /= 1024;
				total /= 1024;
				unit_idx++;
			}
			sprintf(send_buf, "%sFreeDiskSpace: %0.02lf%s\nTotalDiskSpace: %0.02lf%s\n", send_buf, avail, units[unit_idx], total, units[unit_idx]);
		}
		
		// Get info from uname
		struct utsname uname_info;
		if (uname(&uname_info) == 0)
			sprintf(send_buf, "%sunameSysname: %s\nunameNodename: %s\nunameRelease: %s\nunameVersion: %s\nunameMachine: %s\n", send_buf, uname_info.sysname, uname_info.nodename, uname_info.release, uname_info.version, uname_info.machine);
		
		/* CPU Usage Vector */
		// Lock mutex
		pthread_mutex_lock(&cpu_usage_mutex);
		
		// Secondly history
		int i, tmp;
		sprintf(send_buf, "%ssecondlyCPUUsage: [", send_buf);
		for (i = cpu_usage_secondly_history_head, tmp = cpu_usage_secondly_history_items; tmp > 0; tmp--, i = (i+1)%MAX_CPU_USAGE_SECONDLY_HISTORY_ITEMS)
			sprintf(send_buf, "%s%s%hd.%hd", send_buf, ((i == cpu_usage_secondly_history_head) ? "" : ","), cpu_usage_secondly_history[i]/10, cpu_usage_secondly_history[i]%10);
		sprintf(send_buf, "%s]\n", send_buf);
		
		// 30 Second history
		sprintf(send_buf, "%s30secondCPUUsage: [", send_buf);
		for (i = cpu_usage_30second_history_head, tmp = cpu_usage_30second_history_items; tmp > 0; tmp--, i = (i+1)%MAX_CPU_USAGE_30SECOND_HISTORY_ITEMS)
			sprintf(send_buf, "%s%s%hd.%hd", send_buf, ((i == cpu_usage_30second_history_head) ? "" : ","), cpu_usage_30second_history[i]/10, cpu_usage_30second_history[i]%10);
		sprintf(send_buf, "%s]\n", send_buf);
		
		// Minutely history
		sprintf(send_buf, "%sminutelyCPUUsage: [", send_buf);
		for (i = cpu_usage_minutely_history_head, tmp = cpu_usage_minutely_history_items; tmp > 0; tmp--, i = (i+1)%MAX_CPU_USAGE_MINUTELY_HISTORY_ITEMS)
			sprintf(send_buf, "%s%s%hd.%hd", send_buf, ((i == cpu_usage_minutely_history_head) ? "" : ","), cpu_usage_minutely_history[i]/10, cpu_usage_minutely_history[i]%10);
		sprintf(send_buf, "%s]\n", send_buf);
		
		// 30 Minute history
		sprintf(send_buf, "%s30minuteCPUUsage: [", send_buf);
		for (i = cpu_usage_30minute_history_head, tmp = cpu_usage_30minute_history_items; tmp > 0; tmp--, i = (i+1)%MAX_CPU_USAGE_30MINUTE_HISTORY_ITEMS)
			sprintf(send_buf, "%s%s%hd.%hd", send_buf, ((i == cpu_usage_30minute_history_head) ? "" : ","), cpu_usage_30minute_history[i]/10, cpu_usage_30minute_history[i]%10);
		sprintf(send_buf, "%s]\n", send_buf);
		
		// Unlock mutex
		pthread_mutex_unlock(&cpu_usage_mutex);
		
		
		/* Memory Usage Vector */
		// Lock mutex
		pthread_mutex_lock(&memory_usage_mutex);
		
		// Secondly history
		int i, tmp;
		sprintf(send_buf, "%ssecondlyMemoryUsage: [", send_buf);
		for (i = memory_usage_secondly_history_head, tmp = memory_usage_secondly_history_items; tmp > 0; tmp--, i = (i+1)%MAX_MEMORY_USAGE_SECONDLY_HISTORY_ITEMS)
			sprintf(send_buf, "%s%s%hd.%hd", send_buf, ((i == memory_usage_secondly_history_head) ? "" : ","), memory_usage_secondly_history[i]/10, memory_usage_secondly_history[i]%10);
		sprintf(send_buf, "%s]\n", send_buf);
		
		// 30 Second history
		sprintf(send_buf, "%s30secondMemoryUsage: [", send_buf);
		for (i = memory_usage_30second_history_head, tmp = memory_usage_30second_history_items; tmp > 0; tmp--, i = (i+1)%MAX_MEMORY_USAGE_30SECOND_HISTORY_ITEMS)
			sprintf(send_buf, "%s%s%hd.%hd", send_buf, ((i == memory_usage_30second_history_head) ? "" : ","), memory_usage_30second_history[i]/10, memory_usage_30second_history[i]%10);
		sprintf(send_buf, "%s]\n", send_buf);
		
		// Minutely history
		sprintf(send_buf, "%sminutelyMemoryUsage: [", send_buf);
		for (i = memory_usage_minutely_history_head, tmp = memory_usage_minutely_history_items; tmp > 0; tmp--, i = (i+1)%MAX_MEMORY_USAGE_MINUTELY_HISTORY_ITEMS)
			sprintf(send_buf, "%s%s%hd.%hd", send_buf, ((i == memory_usage_minutely_history_head) ? "" : ","), memory_usage_minutely_history[i]/10, memory_usage_minutely_history[i]%10);
		sprintf(send_buf, "%s]\n", send_buf);
		
		// 30 Minute history
		sprintf(send_buf, "%s30minuteMemoryUsage: [", send_buf);
		for (i = memory_usage_30minute_history_head, tmp = memory_usage_30minute_history_items; tmp > 0; tmp--, i = (i+1)%MAX_MEMORY_USAGE_30MINUTE_HISTORY_ITEMS)
			sprintf(send_buf, "%s%s%hd.%hd", send_buf, ((i == memory_usage_30minute_history_head) ? "" : ","), memory_usage_30minute_history[i]/10, memory_usage_30minute_history[i]%10);
		sprintf(send_buf, "%s]\n", send_buf);
		
		// Unlock mutex
		pthread_mutex_unlock(&memory_usage_mutex);
		
		
		// Print sender address
		char sender[INET6_ADDRSTRLEN+1];
		inet_ntop(AF_INET6, &remote_addr.sin6_addr, sender, INET6_ADDRSTRLEN);
		printf("Sending data back to %s.\n", sender);
		
		// Send data back
		int len = strlen(send_buf);
		char * send_buf_cur = send_buf;
		while (len > 0)
		{
			// TODO: Deal with fragmented packet order
			/* For netcat:
			int send_len = (len > 1024) ? 1024 : len;	// Accommodate netcats 1024 byte limit (TODO: Remove this in the future)
			int sent = sendto(sockfd, send_buf_cur, send_len, 0, (struct sockaddr *)&remote_addr, addr_size);
			*/
			int sent = sendto(sockfd, send_buf_cur, len, 0, (struct sockaddr *)&remote_addr, addr_size);
			if (sent == -1)
			{
				perror("\tFailed to send message");
				break;
			}
			len -= sent;
			send_buf_cur += sent;
		}
	}
	
	// Close socket
	close_listener();
}
