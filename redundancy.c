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

#define VERSION 1
int sockfd = -1;
int stop_redundancy_socket = -1;
volatile short redundancy_flag = 1;
uint64_t ptype, host_num, pid;
uint64_t timestamp;
struct timeval timestamp_precise;

pthread_mutex_t sisis_addr_mutex = PTHREAD_MUTEX_INITIALIZER;
char sisis_addr[INET6_ADDRSTRLEN] = { '\0' };

// Current number of processes (-1 for invalid)
int num_processes = -1;

void close_listener()
{
	if (stop_redundancy_socket != -1)
		close(stop_redundancy_socket);
	if (sockfd != -1)
	{
#ifdef DEBUG
		printf("Closing listening socket...\n");
#endif
		close(sockfd);
		
		// Unregister
		sisis_unregister(NULL, ptype, (uint64_t)VERSION, host_num, pid, timestamp);
		
		sockfd = -1;
	}
}

void terminate(int signal)
{
#ifdef DEBUG
	printf("Terminating...\n");
#endif
	close_listener();
	exit(0);
}

void recheck_redundance_alarm_handler(int signal)
{
#ifdef DEBUG
	printf("Timeout expired... Rechecking redundancy.\n");
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
	// Store process type
	ptype = process_type;
	
	// Get start time
	timestamp = time(NULL);
	gettimeofday(&timestamp_precise, NULL);	// More precise
	
	// Check number of args
	if (argc != 2)
	{
		printf("Usage: %s <host_num>\n", argv[0]);
		exit(1);
	}
	
	// Get host number
	sscanf (argv[1], "%llu", &host_num);
	
	// Get pid
	pid = getpid();
	
	// Register address
	pthread_mutex_lock(&sisis_addr_mutex);
	if (sisis_register(sisis_addr, process_type, process_type_version, host_num, pid, timestamp) != 0)
	{
		printf("Failed to register SIS-IS address.\n");
		exit(1);
	}
	pthread_mutex_unlock(&sisis_addr_mutex);
	
	// Status
	printf("Opening socket at %s on port %i.\n", sisis_addr, port);
	
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
	printf("Socket opened at %s on port %u.\n", sisis_addr, ntohs(((struct sockaddr_in *)(addr->ai_addr))->sin_port));
	
	// Set up signal handling
	signal(SIGABRT, terminate);
	signal(SIGTERM, terminate);
	signal(SIGINT, terminate);
	
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
	FD_ZERO(&socks);
	FD_SET(sockfd, &socks);
	
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
						printf("Stopping Redundancy.\n");
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
						char addr[INET6_ADDRSTRLEN];
						if (inet_ntop(AF_INET6, &(remote_addr.sin6_addr), addr, INET6_ADDRSTRLEN) != NULL)
							printf("Input from %*s.\n", INET6_ADDRSTRLEN, addr);
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
							printf("# inputs: %d\n", num_input);
							printf("# input processes: %d\n", num_input_processes);
							printf("Waiting %ld.%06ld seconds for more results.\n", (long)(select_timeout.tv_sec), (long)(select_timeout.tv_usec));
			#endif
						}
					}
				} while(!(flags & REDUNDANCY_MAIN_FLAG_SINGLE_INPUT) && num_input < num_input_processes && select(sockfd+1, &socks, NULL, NULL, &select_timeout) > 0);
				
				// Check that at least 1/2 of the processes sent inputs
				if (!(flags & REDUNDANCY_MAIN_FLAG_SINGLE_INPUT) && num_input <= num_input_processes/2)
				{
					// Flush inputs
					flush_inputs();
		#ifdef DEBUG
					printf("Not enough inputs for a vote.\n");
		#endif
				}
				else
				{
		#ifdef DEBUG
					printf("Voting...\n");
		#endif
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

int rib_monitor_add_ipv6_route(struct route_ipv6 * route)
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
					if (process_type == ptype)
					{
						// Update current number of processes
						if (num_processes == -1)
							num_processes = get_process_type_count(ptype);
						else
							num_processes++;
						
						// Check redundancy
						check_redundancy();
					}
				}
			}
		}
	}
	
	// Free memory
	free(route);
}

int rib_monitor_remove_ipv6_route(struct route_ipv6 * route)
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
					if (process_type == ptype)
					{
						// Update current number of processes
						if (num_processes == -1)
							num_processes = get_process_type_count(ptype);
						else
							num_processes--;
						
						// Check redundancy
						check_redundancy();
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
	int num_procs = MAX(num_machines*REDUNDANCY_PERCENTAGE/100, 3);
	
	// Get list of all processes
	struct list * proc_addrs = get_processes_by_type(ptype);
	struct listnode * node;
	
	// Check current number of processes
	if (num_processes == -1)
		num_processes = get_process_type_count(ptype);
	printf("Need %d processes... Have %d.\n", num_procs, num_processes);
	// Too few
	if (num_processes < num_procs)
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
			int num_start = num_procs - num_processes;
			
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
					printf("Malloc failed...\n");
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
							printf("Looking for machine monitor: ");
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
							printf("%sFound\n", (mm_remote_addr == NULL) ? "Not " : "");
#endif
							
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
									printf("Sending machine monitor request to %s.\n", tmp_addr_str);
#endif
									// Get memory stats
									char * req = "data\n";
									if (sendto(tmp_sock, req, strlen(req), 0, (struct sockaddr *)&sockaddr, sockaddr_size) == -1)
									{
#ifdef DEBUG
										printf("\tFailed to send machine monitor request.\n");
#endif
										desirable_hosts[i].priority += 200;	// Error... penalize
									}
									else
									{
#ifdef DEBUG
										printf("\tSent machine monitor request.  Waiting for response...\n");
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
											printf("\tMachine monitor request timed out.\n");
#endif
											desirable_hosts[i].priority += 200;	// Error... penalize
										}
										else if ((len = recvfrom(tmp_sock, buf, 65536, 0, (struct sockaddr *)&fromaddr, &fromaddr_size)) < 1)
										{
#ifdef DEBUG
											printf("\tFailed to receive machine monitor response.\n");
#endif
											desirable_hosts[i].priority += 200;	// Error... penalize
										}
										else if (sockaddr_size != fromaddr_size || memcmp(&sockaddr, &fromaddr, fromaddr_size) != 0)
										{
#ifdef DEBUG
											inet_ntop(AF_INET6, &((struct sockaddr_in6 *)&fromaddr)->sin6_addr, tmp_addr_str, INET6_ADDRSTRLEN);
											printf("\tFailed to receive machine monitor response.  Response from wrong host (%s).\n", tmp_addr_str);
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
													printf("\tMemory Usage = %d%%\n", usage);
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
													printf("\tCPU Usage = %d%%\n", usage);
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
				printf("Sorting hosts according to desirability.\n");
#endif
				qsort(desirable_hosts, spawn_addrs->size, sizeof(desirable_hosts[0]), compare_desirable_hosts);
				
				// Recheck whether we should duplicate
				if (!redundancy_flag)
					return;
				
				// Make new socket
				int spawn_sock = make_socket(NULL);
				if (spawn_sock == -1)
					printf("Failed to open spawn socket.\n");
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
								printf("Starting new process via %s.\n", tmp_addr);
							else
								printf("Starting new process.\n");
	#endif
							// Send request
							char req[32];
							sprintf(req, "%d %llu", REMOTE_SPAWN_REQ_START, ptype);
							if (sendto(spawn_sock, req, strlen(req), 0, (struct sockaddr *)&sockaddr, sockaddr_size) == -1)
								printf("Failed to send message.  Error: %i\n", errno);
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
				ualarm(RECHECK_PROCS_ALARM_DELAY, 0);
			}
			// Free memory
			if (spawn_addrs)
				FREE_LINKED_LIST(spawn_addrs);
			if (monitor_addrs)
				FREE_LINKED_LIST(monitor_addrs);
		}
	}
	// Too many
	else if (num_processes > num_procs)
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
						if (ts < timestamp || (ts == timestamp && (sys_id < host_num || other_pid < pid))) // Use System ID and PID as tie breakers
							if (++younger_procs == num_procs)
							{
								// TODO: In first second, give second chance to avoid OSPF issues
								struct timeval tv;
								gettimeofday(&tv, NULL);
								if ((tv.tv_sec * 10 + tv.tv_usec/100000) - (timestamp_precise.tv_sec * 10 + timestamp_precise.tv_usec/100000) < 10)
								{
									sleep(1);
									
									// Recheck
									check_redundancy();
									
									break;
								}
								
#ifdef DEBUG
								printf("Terminating...\n");
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