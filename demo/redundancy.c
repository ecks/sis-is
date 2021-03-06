/*
 * SIS-IS Demo program.
 * Stephen Sigwart
 * University of Delaware
 */

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>

#include <time.h>
#include <sys/time.h>
#include <pthread.h>

#include "demo.h"
#include "redundancy.h"

#include "../remote_spawn/remote_spawn.h"
#include "../tests/sisis_api.h"
#include "../tests/sisis_process_types.h"
#include "../tests/sisis_addr_format.h"

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

#define DEBUG
//#define DEBUG_FILE

FILE * debug_file = NULL;
FILE * printf_file = NULL;

#define VERSION 1
int sockfd = -1;
int stop_redundancy_socket = -1;
volatile short redundancy_flag = 1;
uint64_t ptype, ptype_version, host_num, pid;
uint64_t timestamp;
struct timeval timestamp_sisis_registered = { 0 };

// Time when the last set of inputs were actually processed (ie. there were enough processes)
struct timeval last_inputs_processes;

pthread_mutex_t sisis_addr_mutex = PTHREAD_MUTEX_INITIALIZER;
char sisis_addr[INET6_ADDRSTRLEN] = { '\0' };

// Current number of processes (-1 for invalid)
pthread_mutex_t num_processes_mutex = PTHREAD_MUTEX_INITIALIZER;
int num_processes = -1;

void close_listener()
{
	if (stop_redundancy_socket != -1)
		close(stop_redundancy_socket);
	if (sockfd != -1)
	{
#ifdef DEBUG
		fprintf(printf_file, "Closing listening socket...\n");
#endif
		close(sockfd);
		
		// Wait at least 1.1 seconds to prevent OSPF issues
		if (timestamp_sisis_registered.tv_sec == 0 && timestamp_sisis_registered.tv_usec == 0)
			gettimeofday(&timestamp_sisis_registered, NULL);
		struct timeval tv, tv2, tv3;
		gettimeofday(&tv, NULL);
		timersub(&tv, &timestamp_sisis_registered, &tv2);
		if (tv2.tv_sec < 1 || (tv2.tv_sec == 1 && tv2.tv_usec < 100000))
		{
			tv.tv_sec = 1;
			tv.tv_usec = 100000;
			timersub(&tv, &tv2, &tv3);
			struct timespec sleep_time, rem_sleep_time;
			sleep_time.tv_sec = tv3.tv_sec;
			sleep_time.tv_nsec = tv3.tv_usec * 1000;
	#ifdef DEBUG
			fprintf(printf_file, "Waiting %llu.%06llu seconds to prevent OSPF issue.\n", (uint64_t)sleep_time.tv_sec, (uint64_t)sleep_time.tv_nsec/1000);
	#endif
			// Sleep
			while (nanosleep(&sleep_time, &rem_sleep_time) == -1)
			{
				if (errno == EINTR)
				{
	#ifdef DEBUG
					fprintf(printf_file, "Sleep Interrupted... Trying again.\n");
	#endif
					memcpy(&sleep_time, &rem_sleep_time, sizeof rem_sleep_time);
				}
				else
					break;
			}
			// Busy wait as last resort
			gettimeofday(&tv, NULL);
			timersub(&tv, &timestamp_sisis_registered, &tv2);
			if (tv2.tv_sec < 1 || (tv2.tv_sec == 1 && tv2.tv_usec < 100000))
			{
	#ifdef DEBUG
				fprintf(printf_file, "Busy waiting...\n");
	#endif
				do
				{
					gettimeofday(&tv, NULL);
					timersub(&tv, &timestamp_sisis_registered, &tv2);
				} while (tv2.tv_sec < 1 || (tv2.tv_sec == 1 && tv2.tv_usec < 100000));
			}
			
			
			gettimeofday(&tv, NULL);
			timersub(&tv, &timestamp_sisis_registered, &tv2);
	#ifdef DEBUG
			fprintf(printf_file, "%llu.%06llu seconds since start... now actually terminating.\n", (uint64_t)tv2.tv_sec, (uint64_t)tv2.tv_usec);
	#endif
		}
		
		// Unregister
		sisis_unregister(NULL, ptype, ptype_version, host_num, pid, timestamp);
		
		sockfd = -1;
	}
}

void terminate(int signal)
{
	// Block SIGINT
	sigset_t set;
	sigemptyset(&set);
	sigaddset (&set, SIGINT);
	sigprocmask(SIG_BLOCK, &set, NULL);
	
#ifdef DEBUG
	fprintf(printf_file, "Terminating...\n");
	fflush(printf_file);
#endif
	
	close_listener();
	exit(0);
}

void recheck_redundance_alarm_handler(int signal)
{
#ifdef DEBUG
	fprintf(printf_file, "Timeout expired... Rechecking redundancy.\n");
	fflush(printf_file);
#endif
	check_redundancy();
}

/** Get SIS-IS Address */
void get_sisis_addr(char * buf)
{
	// TODO: This would be better with semaphores that actually block
	pthread_mutex_lock(&sisis_addr_mutex);
	while (*sisis_addr == '\0')
	{
		pthread_mutex_unlock(&sisis_addr_mutex);
		sched_yield();
		pthread_mutex_lock(&sisis_addr_mutex);
	}
	strcpy(buf, sisis_addr);
	pthread_mutex_unlock(&sisis_addr_mutex);
}

/** Main loop for redundant processes */
void redundancy_main(uint64_t process_type, uint64_t process_type_version, int port, uint64_t input_process_type, void (*process_input)(char *, int), void (*vote_and_process)(), void (*flush_inputs)(), int flags, int argc, char ** argv)
{
	// Get pid
	pid = getpid();
	
	// Open debug file
#ifdef DEBUG_FILE
	char fn[64];
	sprintf(fn, "redundancy_%llu.log", pid);
	debug_file = fopen(fn, "a");
	printf_file = (debug_file != NULL) ? debug_file : stdin;
#else
	printf_file = stdin;
#endif
	
	// Store process type
	ptype = process_type;
	ptype_version = process_type_version;
	
	// Get start time
	struct timeval tv;
	gettimeofday(&tv, NULL);
	timestamp = (tv.tv_sec * 100 + (tv.tv_usec / 10000)) & 0x00000000ffffffffLLU;	// In 100ths of seconds
	
	// Set up signal handling
	signal(SIGABRT, terminate);
	signal(SIGTERM, terminate);
	signal(SIGINT, terminate);
	
	// There are no last inputs processed
	memset(&last_inputs_processes, 0, sizeof last_inputs_processes);
	
	// Check number of args
	if (argc != 2)
	{
		fprintf(printf_file, "Usage: %s <host_num>\n", argv[0]);
		fflush(printf_file);
		exit(1);
	}
	
	// Get host number
	sscanf (argv[1], "%llu", &host_num);
	
	// Register address
	pthread_mutex_lock(&sisis_addr_mutex);
	if (sisis_register(sisis_addr, process_type, process_type_version, host_num, pid, timestamp) != 0)
	{
		fprintf(printf_file, "Failed to register SIS-IS address.\n");
		fflush(printf_file);
		exit(1);
	}
	pthread_mutex_unlock(&sisis_addr_mutex);
	gettimeofday(&timestamp_sisis_registered, NULL);
	
	// Status
	fprintf(printf_file, "Opening socket at %s on port %i.\n", sisis_addr, port);
	fflush(printf_file);
	
	// Set up socket address info
	struct addrinfo hints, *addr;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET6;	// IPv6
	hints.ai_socktype = SOCK_DGRAM;
	char port_str[8];
	sprintf(port_str, "%u", port);
	getaddrinfo(sisis_addr, port_str, &hints, &addr);
	
	// Create socket
	if ((sockfd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol)) == -1)
	{
		fprintf(printf_file, "Failed to open socket.\n");
		fflush(printf_file);
		exit(1);
	}
	
	// Bind to port
	if (bind(sockfd, addr->ai_addr, addr->ai_addrlen) == -1)
	{
		fprintf(printf_file, "Failed to bind socket to port.\n");
		fflush(printf_file);
		close_listener();
		exit(2);
	}
	
	// Are we checking redundancy?
	if (!(flags & REDUNDANCY_MAIN_FLAG_SKIP_REDUNDANCY))
	{
		// Open socket to stop redundancy
		sprintf(port_str, "%u", STOP_REDUNDANCY_PORT);
		stop_redundancy_socket = make_socket(port_str);
	}
	
	// Short sleep while address propagates
	usleep(50000);	// 50ms
	
	// Status message
	inet_ntop(AF_INET6, &((struct sockaddr_in6 *)(addr->ai_addr))->sin6_addr, sisis_addr, INET6_ADDRSTRLEN);
	fprintf(printf_file, "Socket opened at %s on port %u.\n", sisis_addr, ntohs(((struct sockaddr_in *)(addr->ai_addr))->sin_port));
	fflush(printf_file);
	
	// Info to subscribe to RIB changes
	struct subscribe_to_rib_changes_info info;
	
	// Are we checking redundancy?
	if (!(flags & REDUNDANCY_MAIN_FLAG_SKIP_REDUNDANCY))
	{
		// Set up signal handling for alarm
		signal(SIGALRM, recheck_redundance_alarm_handler);
		
		// Check redundancy
		check_redundancy();
		
		// Subscribe to RIB changes
		memset(&info, 0, sizeof info);
		info.rib_add_ipv6_route = rib_monitor_add_ipv6_route;
		info.rib_remove_ipv6_route = rib_monitor_remove_ipv6_route;
		subscribe_to_rib_changes(&info);
	}
	
	// Set of sockets for select call when waiting for other inputs
	fd_set socks;
	
	// Timeout information for select call
	struct timeval select_timeout;
	struct timeval start_time, cur_time, tmp1, tmp2;
	
	// Number of input processes
	int num_input_processes;
	int num_input = 0;
	
	// Wait for message
	struct sockaddr_in6 remote_addr;
	int buflen;
	char buf[RECV_BUFFER_SIZE];
	socklen_t addr_size = sizeof remote_addr;
	while (1)
	{
		// Set of sockets for main select call
		int main_socks_max_fd;
		fd_set main_socks;
		FD_ZERO(&main_socks);
		FD_SET(sockfd, &main_socks);
		
		// Are we checking redundancy?
		if (!(flags & REDUNDANCY_MAIN_FLAG_SKIP_REDUNDANCY))
		{
			FD_SET(stop_redundancy_socket, &main_socks);
			main_socks_max_fd = MAX(stop_redundancy_socket, sockfd)+1;
		}
		else
			main_socks_max_fd = sockfd+1;
		
		// Wait for message on either socket
		if (select(main_socks_max_fd, &main_socks, NULL, NULL, NULL) > 0)
		{
			// Stop redundancy socket
			if (FD_ISSET(stop_redundancy_socket, &main_socks))
			{
				if ((buflen = recvfrom(stop_redundancy_socket, buf, RECV_BUFFER_SIZE, 0, NULL, NULL)) != -1)
				{
					// Very primative security
					if (buflen == strlen(PASSWORD) && memcmp(buf, PASSWORD, buflen) == 0)
					{
#ifdef DEBUG
						fprintf(printf_file, "Stopping Redundancy.\n");
						fflush(printf_file);
#endif
						
						// Unsubscribe to RIB changes
						subscribe_to_rib_changes(&info);
						redundancy_flag = 0;
					}
				}
			}
			// Input socket
			else if (FD_ISSET(sockfd, &main_socks))
			{
				do
				{
					// Read from socket
					if ((buflen = recvfrom(sockfd, buf, RECV_BUFFER_SIZE, 0, (struct sockaddr *)&remote_addr, &addr_size)) != -1)
					{
#ifdef DEBUG
						gettimeofday(&cur_time, NULL);
						char addr[INET6_ADDRSTRLEN];
						if (inet_ntop(AF_INET6, &(remote_addr.sin6_addr), addr, INET6_ADDRSTRLEN) != NULL)
							fprintf(printf_file, "[%llu.%06llu] Input from %*s.\n", (uint64_t)cur_time.tv_sec, (uint64_t)cur_time.tv_usec, INET6_ADDRSTRLEN, addr);
						fflush(printf_file);
#endif
						// Setup input
						if (num_input == 0)
						{
							// Set socket select timeout
							select_timeout.tv_sec = GATHER_RESULTS_TIMEOUT_USEC / 1000000;
							select_timeout.tv_usec = GATHER_RESULTS_TIMEOUT_USEC % 1000000;
							
							// Get start time
							gettimeofday(&start_time, NULL);
						}
						else
						{
							// Determine new socket select timeout
							gettimeofday(&cur_time, NULL);
							timersub(&cur_time, &start_time, &tmp1);
							timersub(&select_timeout, &tmp1, &tmp2);
							select_timeout.tv_sec = tmp2.tv_sec;
							select_timeout.tv_usec = tmp2.tv_usec;
						}
						
						// Record input
						num_input++;
						
						// Process the input
						process_input(buf, buflen);
						
						// Check how many input processes there are
						if (!(flags & REDUNDANCY_MAIN_FLAG_SINGLE_INPUT))
						{
							num_input_processes = get_process_type_count(input_process_type);
			#ifdef DEBUG
							fprintf(printf_file, "# inputs: %d\n", num_input);
							fprintf(printf_file, "# input processes: %d\n", num_input_processes);
							fprintf(printf_file, "Waiting %ld.%06ld seconds for more results.\n", (long)(select_timeout.tv_sec), (long)(select_timeout.tv_usec));
							fflush(printf_file);
			#endif
						}
					}
					
					// Set of sockets for select call when waiting for other inputs
					FD_ZERO(&socks);
					FD_SET(sockfd, &socks);
				} while(!(flags & REDUNDANCY_MAIN_FLAG_SINGLE_INPUT) && num_input < num_input_processes && select(sockfd+1, &socks, NULL, NULL, &select_timeout) > 0);
				
				// Check that at least 1/2 of the processes sent inputs
				if (!(flags & REDUNDANCY_MAIN_FLAG_SINGLE_INPUT) && num_input <= num_input_processes/2)
				{
					// Check how late these are for the last set of inputs
					gettimeofday(&cur_time, NULL);
					timersub(&cur_time, &last_inputs_processes, &tmp1);
					//if ((tmp1.tv_sec * 1000000 + tmp1.tv_usec) < GATHER_RESULTS_TIMEOUT_USEC * 1.25)
					fprintf(printf_file, "Late by %llu.%06llu seconds.\n", (uint64_t)tmp1.tv_sec, (uint64_t)tmp1.tv_usec);
					fflush(printf_file);
					
					// Flush inputs
					flush_inputs();
		#ifdef DEBUG
					fprintf(printf_file, "Not enough inputs for a vote.\n");
					fflush(printf_file);
		#endif
				}
				else
				{
		#ifdef DEBUG
					fprintf(printf_file, "Voting...\n");
					fflush(printf_file);
		#endif
					// Record time
					gettimeofday(&last_inputs_processes, NULL);
					
					// Vote and process results
					vote_and_process();
				}
				
				// Reset
				num_input = 0;
			}
		}
	}
	
	// Close socket
	close_listener();
}

int rib_monitor_add_ipv6_route(struct route_ipv6 * route, void * data)
{
	// Make sure it is a host address
	if (route->p->prefixlen == 128)
	{
		char addr[INET6_ADDRSTRLEN];
		if (inet_ntop(AF_INET6, &(route->p->prefix.s6_addr), addr, INET6_ADDRSTRLEN) != NULL)
		{
			// Parse components
			uint64_t prefix, sisis_version, process_type, process_version, sys_id, pid, ts;
			if (get_sisis_addr_components(addr, &prefix, &sisis_version, &process_type, &process_version, &sys_id, &pid, &ts) == 0)
			{
				// Check that this is an SIS-IS address
				if (prefix == components[0].fixed_val && sisis_version == components[1].fixed_val)
				{
					// Check if this is the current process type
					if (process_type == ptype && process_version == ptype_version)
					{
						// Update current number of processes
						pthread_mutex_lock(&num_processes_mutex);
						if (num_processes == -1)
							num_processes = get_process_type_version_count(ptype, ptype_version);
						else
							num_processes++;
						pthread_mutex_unlock(&num_processes_mutex);
						
						// Set alarm to check redundancy in a little bit
						struct itimerval itv;
						// Check that there is a shorter timer already set
						getitimer(ITIMER_REAL, &itv);
						if ((itv.it_value.tv_sec == 0 && itv.it_value.tv_usec == 0) || itv.it_value.tv_sec * 1000000 + itv.it_value.tv_usec > RECHECK_PROCS_ALARM_DELAY)
						{
							// Set timer
							memset(&itv, 0, sizeof itv);
							itv.it_value.tv_sec = INITIAL_CHECK_PROCS_ALARM_DELAY / 1000000;
							itv.it_value.tv_usec = INITIAL_CHECK_PROCS_ALARM_DELAY % 1000000;
							setitimer(ITIMER_REAL, &itv, NULL);
						}
					}
				}
			}
		}
	}
	
	// Free memory
	free(route);
}

int rib_monitor_remove_ipv6_route(struct route_ipv6 * route, void * data)
{
	// Make sure it is a host address
	if (route->p->prefixlen == 128)
	{
		char addr[INET6_ADDRSTRLEN];
		if (inet_ntop(AF_INET6, &(route->p->prefix.s6_addr), addr, INET6_ADDRSTRLEN) != NULL)
		{
			// Parse components
			uint64_t prefix, sisis_version, process_type, process_version, sys_id, pid, ts;
			if (get_sisis_addr_components(addr, &prefix, &sisis_version, &process_type, &process_version, &sys_id, &pid, &ts) == 0)
			{
				// Check that this is an SIS-IS address
				if (prefix == components[0].fixed_val && sisis_version == components[1].fixed_val)
				{
					// Check if this is the current process type
					if (process_type == ptype && process_version == ptype_version)
					{
						// Update current number of processes
						pthread_mutex_lock(&num_processes_mutex);
						if (num_processes == -1)
							num_processes = get_process_type_version_count(ptype, ptype_version);
						else
							num_processes--;
						pthread_mutex_unlock(&num_processes_mutex);
						
						// Set alarm to check redundancy in a little bit
						struct itimerval itv;
						// Check that there is a shorter timer already set
						getitimer(ITIMER_REAL, &itv);
						if ((itv.it_value.tv_sec == 0 && itv.it_value.tv_usec == 0) || itv.it_value.tv_sec * 1000000 + itv.it_value.tv_usec > RECHECK_PROCS_ALARM_DELAY)
						{
							// Set timer
							memset(&itv, 0, sizeof itv);
							itv.it_value.tv_sec = INITIAL_CHECK_PROCS_ALARM_DELAY / 1000000;
							itv.it_value.tv_usec = INITIAL_CHECK_PROCS_ALARM_DELAY % 1000000;
							setitimer(ITIMER_REAL, &itv, NULL);
						}
					}
				}
			}
		}
	}
	
	// Free memory
	free(route);
}

/** Checks if there is an appropriate number of processes running in the system. */
void check_redundancy()
{
	// Make sure we are supposed to check
	if (!redundancy_flag)
		return;
	
	// Get total number of machines (by looking for machine monitors)
	int num_machines = get_process_type_count((uint64_t)SISIS_PTYPE_MACHINE_MONITOR);
	
	// Determine number of processes we should have
	int num_procs = MAX(num_machines*REDUNDANCY_PERCENTAGE/100, MIN_NUM_PROCESSES);
	
	// Get list of all processes
	struct list * proc_addrs = get_processes_by_type_version(ptype, ptype_version);
	struct listnode * node;
	
	// Check current number of processes
	pthread_mutex_lock(&num_processes_mutex);
	if (num_processes == -1)
		num_processes = get_process_type_version_count(ptype, ptype_version);
	int local_num_processes = num_processes;
	pthread_mutex_unlock(&num_processes_mutex);
	fprintf(printf_file, "Need %d processes... Have %d.\n", num_procs, local_num_processes);
	fflush(printf_file);
	
	// Too few
	if (local_num_processes < num_procs)
	{
		// TODO: Maybe only have the leader do this (or a leader and spare)
		// Only have youngest process start new processes
		int do_startup = 1;
		if (proc_addrs)
		{
			LIST_FOREACH(proc_addrs, node)
			{
				struct in6_addr * remote_addr = (struct in6_addr *)node->data;
				
				// Parse components
				char addr[INET6_ADDRSTRLEN];
				uint64_t prefix, sisis_version, process_type, process_version, sys_id, other_pid, ts;
				if (inet_ntop(AF_INET6, remote_addr, addr, INET6_ADDRSTRLEN) != NULL)
					if (get_sisis_addr_components(addr, &prefix, &sisis_version, &process_type, &process_version, &sys_id, &other_pid, &ts) == 0)
						if (ts < timestamp || (ts == timestamp && (sys_id < host_num || other_pid < pid))) // Use System ID and PID as tie breakers
						{
							do_startup = 0;
							break;
						}
			}
		}
		
		// Are we starting up processes?
		if (do_startup)
		{
			// Number of processes to start
			int num_start = num_procs - local_num_processes;
			
			// Get machine monitors
			char mm_addr[INET6_ADDRSTRLEN+1];
			sisis_create_addr(mm_addr, (uint64_t)SISIS_PTYPE_MACHINE_MONITOR, (uint64_t)1, (uint64_t)0, (uint64_t)0, (uint64_t)0);
			struct prefix_ipv6 mm_prefix = sisis_make_ipv6_prefix(mm_addr, 42);
			struct list * monitor_addrs = get_sisis_addrs_for_prefix(&mm_prefix);
			
			// Check if the spawn process is running
			char spawn_addr[INET6_ADDRSTRLEN+1];
			sisis_create_addr(spawn_addr, (uint64_t)SISIS_PTYPE_REMOTE_SPAWN, (uint64_t)1, (uint64_t)0, (uint64_t)0, (uint64_t)0);
			struct prefix_ipv6 spawn_prefix = sisis_make_ipv6_prefix(spawn_addr, 42);
			struct list * spawn_addrs = get_sisis_addrs_for_prefix(&spawn_prefix);
			if (spawn_addrs != NULL && spawn_addrs->size)
			{
				// Determine most desirable hosts
				desirable_host_t * desirable_hosts = malloc(sizeof(desirable_host_t) * spawn_addrs->size);
				if (desirable_hosts == NULL)
				{
					fprintf(printf_file, "Malloc failed...\n");
					fflush(printf_file);
					exit(1);
				}
				
				int i = 0;
				LIST_FOREACH(spawn_addrs, node)
				{
					struct in6_addr * remote_addr = (struct in6_addr *)node->data;
					desirable_hosts[i].remote_spawn_addr = remote_addr;
					
					// Get priority
					desirable_hosts[i].priority = UINT64_MAX;
					// Parse components
					char addr[INET6_ADDRSTRLEN];
					uint64_t prefix, sisis_version, process_type, process_version, sys_id, other_pid, ts;
					if (inet_ntop(AF_INET6, remote_addr, addr, INET6_ADDRSTRLEN) != NULL)
						if (get_sisis_addr_components(addr, &prefix, &sisis_version, &process_type, &process_version, &sys_id, &other_pid, &ts) == 0)
						{
							desirable_hosts[i].priority = (sys_id == host_num ? 10000 : 0);
							
							// Try to find machine monitor for this host
#ifdef DEBUG
							fprintf(printf_file, "Looking for machine monitor: ");
							fflush(printf_file);
#endif
							struct in6_addr * mm_remote_addr = NULL;
							if (monitor_addrs != NULL && monitor_addrs->size > 0)
							{
								struct listnode * mm_node;
								LIST_FOREACH(monitor_addrs, mm_node)
								{
									struct in6_addr * remote_addr2 = (struct in6_addr *)mm_node->data;
									
									// Get system id
									uint64_t mm_sys_id;
									if (inet_ntop(AF_INET6, remote_addr2, addr, INET6_ADDRSTRLEN) != NULL)
										if (get_sisis_addr_components(addr, NULL, NULL, NULL, NULL, &mm_sys_id, NULL, NULL) == 0)
											if (mm_sys_id == sys_id)
											{
												mm_remote_addr = remote_addr2;
												break;
											}
								}
							}
#ifdef DEBUG
							fprintf(printf_file, "%sFound\n", (mm_remote_addr == NULL) ? "Not " : "");
							fflush(printf_file);
#endif
							// Check if there is the same process on this host
							if (proc_addrs != NULL && proc_addrs->size > 0)
							{
								struct listnode * proc_node;
								LIST_FOREACH(proc_addrs, proc_node)
								{
									struct in6_addr * remote_addr2 = (struct in6_addr *)proc_node->data;
									
									// Get system id
									uint64_t proc_sys_id;
									if (inet_ntop(AF_INET6, remote_addr2, addr, INET6_ADDRSTRLEN) != NULL)
										if (get_sisis_addr_components(addr, NULL, NULL, NULL, NULL, &proc_sys_id, NULL, NULL) == 0)
											if (proc_sys_id == sys_id)
												desirable_hosts[i].priority += 1000;
								}
							}
							
							// If there is no machine monitor, it is les desirable
							if (mm_remote_addr == NULL)
								desirable_hosts[i].priority += 200;
							else
							{
								// Make new socket
								int tmp_sock = make_socket(NULL);
								if (tmp_sock == -1)
									desirable_hosts[i].priority += 200;	// Error... penalize
								else
								{
									// Set of sockets for select call
									fd_set socks;
									FD_ZERO(&socks);
									FD_SET(tmp_sock, &socks);
									
									// Timeout information for select call
									struct timeval select_timeout;
									select_timeout.tv_sec = MACHINE_MONITOR_REQUEST_TIMEOUT / 1000000;
									select_timeout.tv_usec = MACHINE_MONITOR_REQUEST_TIMEOUT % 1000000;
									
									// Set up socket info
									struct sockaddr_in6 sockaddr;
									int sockaddr_size = sizeof(sockaddr);
									memset(&sockaddr, 0, sockaddr_size);
									sockaddr.sin6_family = AF_INET6;
									sockaddr.sin6_port = htons(MACHINE_MONITOR_PORT);
									sockaddr.sin6_addr = *mm_remote_addr;
#ifdef DEBUG
									char tmp_addr_str[INET6_ADDRSTRLEN];
									inet_ntop(AF_INET6, mm_remote_addr, tmp_addr_str, INET6_ADDRSTRLEN);
									fprintf(printf_file, "Sending machine monitor request to %s.\n", tmp_addr_str);
									fflush(printf_file);
#endif
									// Get memory stats
									char * req = "data\n";
									if (sendto(tmp_sock, req, strlen(req), 0, (struct sockaddr *)&sockaddr, sockaddr_size) == -1)
									{
#ifdef DEBUG
										fprintf(printf_file, "\tFailed to send machine monitor request.\n");
										fflush(printf_file);
#endif
										desirable_hosts[i].priority += 200;	// Error... penalize
									}
									else
									{
#ifdef DEBUG
										fprintf(printf_file, "\tSent machine monitor request.  Waiting for response...\n");
										fflush(printf_file);
#endif
										struct sockaddr_in6 fromaddr;
										int fromaddr_size = sizeof(fromaddr);
										memset(&fromaddr, 0, fromaddr_size);
										char buf[65536];
										int len;
										
										// Wait for response
										if (select(tmp_sock+1, &socks, NULL, NULL, &select_timeout) <= 0)
										{
#ifdef DEBUG
											fprintf(printf_file, "\tMachine monitor request timed out.\n");
											fflush(printf_file);
#endif
											desirable_hosts[i].priority += 200;	// Error... penalize
										}
										else if ((len = recvfrom(tmp_sock, buf, 65536, 0, (struct sockaddr *)&fromaddr, &fromaddr_size)) < 1)
										{
#ifdef DEBUG
											fprintf(printf_file, "\tFailed to receive machine monitor response.\n");
											fflush(printf_file);
#endif
											desirable_hosts[i].priority += 200;	// Error... penalize
										}
										else if (sockaddr_size != fromaddr_size || memcmp(&sockaddr, &fromaddr, fromaddr_size) != 0)
										{
#ifdef DEBUG
											inet_ntop(AF_INET6, &((struct sockaddr_in6 *)&fromaddr)->sin6_addr, tmp_addr_str, INET6_ADDRSTRLEN);
											fprintf(printf_file, "\tFailed to receive machine monitor response.  Response from wrong host (%s).\n", tmp_addr_str);
											fflush(printf_file);
#endif
											desirable_hosts[i].priority += 200;	// Error... penalize
										}
										else
										{
											// Terminate if needed
											if (len == 65536)
												buf[len-1] = '\0';
											
											// Parse response
											char * match;
											
											// Get memory usage
											char * mem_usage_str = "MemoryUsage: ";
											if ((match = strstr(buf, mem_usage_str)) == NULL)
												desirable_hosts[i].priority += 100;	// Error... penalize
											else
											{
												// Get usage
												int usage;
												if (sscanf(match+strlen(mem_usage_str), "%d%%", &usage))
												{
#ifdef DEBUG
													fprintf(printf_file, "\tMemory Usage = %d%%\n", usage);
													fflush(printf_file);
#endif
													desirable_hosts[i].priority += usage;
												}
												else
													desirable_hosts[i].priority += 100;	// Error... penalize
											}
											
											// Get CPU usage
											char * cpu_usage_str = "CPU: ";
											if ((match = strstr(buf, cpu_usage_str)) == NULL)
												desirable_hosts[i].priority += 100;	// Error... penalize
											else
											{
												// Get usage
												int usage;
												if (sscanf(match+strlen(cpu_usage_str), "%d%%", &usage))
												{
#ifdef DEBUG
													fprintf(printf_file, "\tCPU Usage = %d%%\n", usage);
													fflush(printf_file);
#endif
													desirable_hosts[i].priority += usage;
												}
												else
													desirable_hosts[i].priority += 100;	// Error... penalize
											}
										}
									}
									
									// Close socket
									close(tmp_sock);
								}
							}
						}
					
					i++;
				}
				
				// Sort desirable hosts
#ifdef DEBUG
				fprintf(printf_file, "Sorting hosts according to desirability.\n");
				fflush(printf_file);
#endif
				qsort(desirable_hosts, spawn_addrs->size, sizeof(desirable_hosts[0]), compare_desirable_hosts);
				
				// Recheck whether we should duplicate
				if (!redundancy_flag)
					return;
				
				// Make new socket
				int spawn_sock = make_socket(NULL);
				if (spawn_sock == -1)
				{
					fprintf(printf_file, "Failed to open spawn socket.\n");
					fflush(printf_file);
				}
				else
				{
					do
					{
						int desirable_host_idx = 0;
						for (; desirable_host_idx < spawn_addrs->size; desirable_host_idx++)
						{
							struct in6_addr * remote_addr = desirable_hosts[desirable_host_idx].remote_spawn_addr;
							
							// Set up socket info
							struct sockaddr_in6 sockaddr;
							int sockaddr_size = sizeof(sockaddr);
							memset(&sockaddr, 0, sockaddr_size);
							sockaddr.sin6_family = AF_INET6;
							sockaddr.sin6_port = htons(REMOTE_SPAWN_PORT);
							sockaddr.sin6_addr = *remote_addr;
	#ifdef DEBUG
							// Debugging info
							char tmp_addr[INET6_ADDRSTRLEN];
							if (inet_ntop(AF_INET6, remote_addr, tmp_addr, INET6_ADDRSTRLEN) != NULL)
							{
								uint64_t tmp_sys_id;
								if (get_sisis_addr_components(tmp_addr, NULL, NULL, NULL, NULL, &tmp_sys_id, NULL, NULL) == 0)
									fprintf(printf_file, "Starting new process via %s on host #%llu.\n", tmp_addr, tmp_sys_id);
								else
									fprintf(printf_file, "Starting new process via %s.\n", tmp_addr);
							}
							else
								fprintf(printf_file, "Starting new process.\n");
							fflush(printf_file);
	#endif
							// Send request
							char req[32];
							sprintf(req, "%d %llu %llu", REMOTE_SPAWN_REQ_START, ptype, ptype_version);
							if (sendto(spawn_sock, req, strlen(req), 0, (struct sockaddr *)&sockaddr, sockaddr_size) == -1)
							{
								fprintf(printf_file, "Failed to send message.  Error: %i\n", errno);
								fflush(printf_file);
							}
							else
								num_start--;
							
							// Have we started enough?
							if (num_start == 0)
								break;
						}
					}while (num_start > 0);
					
					// Close spawn socket
					close(spawn_sock);
				}
				
				// Free desirable hosts
				free(desirable_hosts);
				
				// Set alarm to recheck redundancy in a little bit
				struct itimerval itv;
				// Check that there is a shorter timer already set
				getitimer(ITIMER_REAL, &itv);
				if ((itv.it_value.tv_sec == 0 && itv.it_value.tv_usec == 0) || itv.it_value.tv_sec * 1000000 + itv.it_value.tv_usec > RECHECK_PROCS_ALARM_DELAY)
				{
					// Set timer
					memset(&itv, 0, sizeof itv);
					itv.it_value.tv_sec = RECHECK_PROCS_ALARM_DELAY / 1000000;
					itv.it_value.tv_usec = RECHECK_PROCS_ALARM_DELAY % 1000000;
					setitimer(ITIMER_REAL, &itv, NULL);
				}
			}
			// Free memory
			if (spawn_addrs)
				FREE_LINKED_LIST(spawn_addrs);
			if (monitor_addrs)
				FREE_LINKED_LIST(monitor_addrs);
		}
	}
	// Too many
	else if (local_num_processes > num_procs)
	{
		// Exit if not one of first num_procs processes
		int younger_procs = 0;
		if (proc_addrs)
		{
			LIST_FOREACH(proc_addrs, node)
			{
				struct in6_addr * remote_addr = (struct in6_addr *)node->data;
				
				// Parse components
				char addr[INET6_ADDRSTRLEN];
				uint64_t prefix, sisis_version, process_type, process_version, sys_id, other_pid, ts;
				if (inet_ntop(AF_INET6, remote_addr, addr, INET6_ADDRSTRLEN) != NULL)
					if (get_sisis_addr_components(addr, &prefix, &sisis_version, &process_type, &process_version, &sys_id, &other_pid, &ts) == 0)
						// TODO: Maybe if there have the same timestamp, first kill duplicates on the same host
						// TODO: Create list, sort by timestamp, host_num, pid
						// Count # of addresses priors to this one in the list, ignoring extra processes on the same host.
						// If there are num_procs processes and this is the first process on the host: Stay alive
						if (ts < timestamp || (ts == timestamp && (sys_id < host_num || other_pid < pid))) // Use System ID and PID as tie breakers
							if (++younger_procs == num_procs)
							{
								// TODO: In first 1.1 seconds, give second chance to avoid OSPF issues
								struct timeval tv, tv2, tv3;
								gettimeofday(&tv, NULL);
								timersub(&tv, &timestamp_sisis_registered, &tv2);
								if (tv2.tv_sec < 1 || (tv2.tv_sec == 1 && tv2.tv_usec < 100000))
								{
									tv.tv_sec = 1;
									tv.tv_usec = 100000;
									timersub(&tv, &tv2, &tv3);
									struct timespec sleep_time, rem_sleep_time;
									sleep_time.tv_sec = tv3.tv_sec;
									sleep_time.tv_nsec = tv3.tv_usec * 1000;
									// Sleep
									while (nanosleep(&sleep_time, &rem_sleep_time) == -1)
									{
										if (errno == EINTR)
											memcpy(&sleep_time, &rem_sleep_time, sizeof rem_sleep_time);
										else
											break;
									}
									// Busy wait as last resort
									gettimeofday(&tv, NULL);
									timersub(&tv, &timestamp_sisis_registered, &tv2);
									if (tv2.tv_sec < 1 || (tv2.tv_sec == 1 && tv2.tv_usec < 100000))
									{
										do
										{
											gettimeofday(&tv, NULL);
											timersub(&tv, &timestamp_sisis_registered, &tv2);
										} while (tv2.tv_sec < 1 || (tv2.tv_sec == 1 && tv2.tv_usec < 100000));
									}
									
									// Recheck
									check_redundancy();
									
									break;
								}
								
#ifdef DEBUG
								fprintf(printf_file, "Terminating...\n");
								fflush(printf_file);
#endif
								close_listener();
								exit(0);
							}
			}
		}
	}
	
	// Free memory
	if (proc_addrs)
		FREE_LINKED_LIST(proc_addrs);
}

/** Creates a new socket. */
int make_socket(char * port)
{
	int fd;
	
	// Set up socket address info
	struct addrinfo hints, *addr;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET6;	// IPv6
	hints.ai_socktype = SOCK_DGRAM;
	getaddrinfo(sisis_addr, port, &hints, &addr);
	
	// Create socket
	if ((fd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol)) == -1)
		return -1;
	
	// Bind to port
	if (bind(fd, addr->ai_addr, addr->ai_addrlen) == -1)
	{
		close(fd);
		return -1;
	}
	
	return fd;
}

/** Compare desirable hosts. */
int compare_desirable_hosts(const void * a_ptr, const void * b_ptr)
{
	desirable_host_t * a = (desirable_host_t *)a_ptr;
	desirable_host_t * b = (desirable_host_t *)b_ptr;
	if (a->priority < b->priority)
		return -1;
	else if (a->priority > b->priority)
		return 1;
	return 0;
}