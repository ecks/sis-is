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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "remote_spawn.h"

#include "../tests/sisis_api.h"
#include "../tests/sisis_process_types.h"

#define PROCS_DAT_FILE "procs.dat"

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
		sisis_unregister(NULL, (uint64_t)SISIS_PTYPE_REMOTE_SPAWN, (uint64_t)VERSION, (uint64_t)host_num, (uint64_t)pid, (uint64_t)timestamp);
		
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

// SIGCHLD handler
void sigchld_handler(signo)
{
	int status;
	wait(&status);
}

/** Spawns a process */
int spawn_process(char * path, char ** argv)
{
	pid_t fork_pid;
	if ((fork_pid = fork()) == 0)
	{
		// TODO: How do I check for errors?
		
		// Change STDIN, STDOUT, and STDERR to /dev/null
		close(STDIN_FILENO);
		open("/dev/null", O_RDONLY);
		close(STDOUT_FILENO);
		open("/dev/null", O_WRONLY);
		close(STDERR_FILENO);
		open("/dev/null", O_WRONLY); 
		
		// Detach from parent
		setsid();
		
		// TODO: Remove full path later and use execvp
		execv(path, argv);
		
		// Exit
		exit(0);
	}
	else if (fork_pid > 0)
	{
		printf("Started\n");
		return REMOTE_SPAWN_RESP_OK;
	}
	else
	{
		printf("Failed[%d]\n", errno);
		return REMOTE_SPAWN_RESP_SPAWN_FAILED;
	}
}

int main (int argc, char ** argv)
{
	// Get start time
	timestamp = time(NULL);
	
	// Setup SIS-IS API
	setup_sisis_addr_format("sisis_format_v2.dat");
	
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
	if (sisis_register(sisis_addr, (uint64_t)SISIS_PTYPE_REMOTE_SPAWN, (uint64_t)VERSION, host_num, pid, timestamp) != 0)
	{
		printf("Failed to register SIS-IS address.\n");
		exit(1);
	}
	
	// Status
	printf("Opening socket at %s on port %i.\n", sisis_addr, REMOTE_SPAWN_PORT);
	
	// Set up socket address info
	struct addrinfo hints, *addr;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET6;	// IPv6
	hints.ai_socktype = SOCK_DGRAM;
	char port_str[8];
	sprintf(port_str, "%u", REMOTE_SPAWN_PORT);
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
	signal(SIGCHLD, sigchld_handler);
	
	// Wait for message
	struct sockaddr remote_addr;
	int len;
	char buf[256];
	socklen_t addr_size = sizeof remote_addr;
	while ((len = recvfrom(sockfd, buf, 255, 0, &remote_addr, &addr_size)) != -1)
	{
		int resp = REMOTE_SPAWN_RESP_INVALID_REQUEST;
		// TODO: Convert to binary later
		//if (strlen(buf) == 8)
		{
			buf[len] = '\0';
			int request, ptype;
			sscanf(buf, "%d %d", &request, &ptype);
			// TODO: Convert to binary later
			//int request = ntohl(*(int *)buf);
			//int ptype = ntohl(*(int*)(buf+4));
			
			printf("Message: %s\n", buf);
			printf("Request: %i\n", request);
			printf("Process Type: %i\n", ptype);
			
			// Read in processes
			FILE * procs_file = fopen(PROCS_DAT_FILE, "r");
			if (procs_file == NULL)
			{
				printf("Failed to open \"%s\".\n", PROCS_DAT_FILE);
				resp = REMOTE_SPAWN_RESP_SPAWN_FAILED;
			}
			else
			{
#define PROCS_DAT_PARSE_FOUND_PROCESS						0x00000001
#define PROCS_DAT_PARSE_LINE_FOUND_PTYPE				0x00000002
#define PROCS_DAT_PARSE_LINE_FOUND_PATH					0x00000004
#define PROCS_DAT_PARSE_LINE_FOUND_ARG0					0x00000008
#define PROCS_DAT_PARSE_LINE_ESCAPE_SEQ_STARTED	0x00000010
#define PROCS_DAT_PARSE_LINE_STRING_STARTED			0x00000020
#define PROCS_DAT_PARSE_LINE_DONE								0x00000040
#define PROCS_DAT_PARSE_LINE_ERROR							0x00000080
#define PROCS_DAT_PARSE_LINE_ALL								0x000000fe
				// Read in line from file
				char line[1024];
				int proc_dat_parse_flags = 0, linenum = 1;
				while (fgets(line, 1024, procs_file) && !(proc_dat_parse_flags & PROCS_DAT_PARSE_FOUND_PROCESS))
				{
					int i = 0, linelen = strlen(line);
					proc_dat_parse_flags &= ~PROCS_DAT_PARSE_LINE_ALL;
					unsigned int line_ptype = 0;
					char line_path[1024], line_arg0[128], tmp[32];
					line_path[0] = line_arg0[0] = tmp[0] = '\0';
					for (; i < linelen && !(proc_dat_parse_flags & PROCS_DAT_PARSE_LINE_ERROR); i++)
					{
						// What string are we working on
						char * str = tmp;
						int max_strlen = 32;
						// Put in inverse order
						if (proc_dat_parse_flags & PROCS_DAT_PARSE_LINE_FOUND_PATH)
						{
							str = line_arg0;
							max_strlen = 128;
						}
						else if (proc_dat_parse_flags & PROCS_DAT_PARSE_LINE_FOUND_PTYPE)
						{
							str = line_path;
							max_strlen = 1024;
						}
						
						// Ignore extra whitespace
						if (((proc_dat_parse_flags & PROCS_DAT_PARSE_LINE_DONE) || str[0] == '\0') && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r' || line[i] == '\n'))
						{ /* Do nothing. */ }
						// Parsing process type
						else if (!(proc_dat_parse_flags & PROCS_DAT_PARSE_LINE_FOUND_PTYPE))
						{
							// Check max string len
							if (strlen(str) + 1 == max_strlen)
								proc_dat_parse_flags |= PROCS_DAT_PARSE_LINE_ERROR;
							else if (line[i] >= '0' && line[i] <= '9')
								sprintf(str, "%s%c", str, line[i]);
							else if (line[i] == ' ' || line[i] == '\t')
							{
								sscanf(str, "%d", &line_ptype);
								str[0] = '\0';
								proc_dat_parse_flags |= PROCS_DAT_PARSE_LINE_FOUND_PTYPE;
							}
							else
								proc_dat_parse_flags |= PROCS_DAT_PARSE_LINE_ERROR;
						}
						// Parsing path and arg0
						else if (!(proc_dat_parse_flags & PROCS_DAT_PARSE_LINE_FOUND_PATH) || !(proc_dat_parse_flags & PROCS_DAT_PARSE_LINE_FOUND_ARG0))
						{
							// We need starting quote
							if (str[0] == '\0' && line[i] != '"' && !(proc_dat_parse_flags & PROCS_DAT_PARSE_LINE_STRING_STARTED))
								proc_dat_parse_flags |= PROCS_DAT_PARSE_LINE_ERROR;
							// Starting string
							else if (str[0] == '\0' && line[i] == '"')
								proc_dat_parse_flags |= PROCS_DAT_PARSE_LINE_STRING_STARTED;
							// Check max string len
							else if (strlen(str) + 1 == max_strlen)
								proc_dat_parse_flags |= PROCS_DAT_PARSE_LINE_ERROR;
							// Check for escape sequence
							else if (proc_dat_parse_flags & PROCS_DAT_PARSE_LINE_ESCAPE_SEQ_STARTED)
							{
								if (line[i] == '\\' || line[i] == '"')
									sprintf(str, "%s%c", str, line[i]);
								else
									proc_dat_parse_flags |= PROCS_DAT_PARSE_LINE_ERROR;
								
								proc_dat_parse_flags &= ~PROCS_DAT_PARSE_LINE_ESCAPE_SEQ_STARTED;
							}
							// Check for escape sequence starter
							else if (line[i] == '\\')
								proc_dat_parse_flags |= PROCS_DAT_PARSE_LINE_ESCAPE_SEQ_STARTED;
							else if (line[i] != '"')
								sprintf(str, "%s%c", str, line[i]);
							else
							{
								// What did we just finish
								if (!(proc_dat_parse_flags & PROCS_DAT_PARSE_LINE_FOUND_PATH))
									proc_dat_parse_flags |= PROCS_DAT_PARSE_LINE_FOUND_PATH;
								else
									proc_dat_parse_flags |= PROCS_DAT_PARSE_LINE_FOUND_ARG0 | PROCS_DAT_PARSE_LINE_DONE;
								
								// String terminated
								proc_dat_parse_flags &= ~PROCS_DAT_PARSE_LINE_STRING_STARTED;
							}
						}
						else
							proc_dat_parse_flags |= PROCS_DAT_PARSE_LINE_ERROR;
					}
					
					// Did we finish parsing correctly?
					if (!(proc_dat_parse_flags & PROCS_DAT_PARSE_LINE_FOUND_ARG0))
						proc_dat_parse_flags |= PROCS_DAT_PARSE_LINE_ERROR;
					
					// Check for error
					if (proc_dat_parse_flags & PROCS_DAT_PARSE_LINE_ERROR)
						printf("Error processing line %d.\n", linenum);
					else
					{
						printf("Line: %d %s %s\n", line_ptype, line_path, line_arg0);
						// Check if this is the right process
						if (ptype == line_ptype)
						{
							if (request == REMOTE_SPAWN_REQ_START)
							{
								char * spawn_argv[] = { line_arg0, argv[1], NULL };
								resp = spawn_process(line_path, spawn_argv);
							}
							else
								resp = REMOTE_SPAWN_RESP_NOT_IMPLEMENTED;
							
							// We found it
							proc_dat_parse_flags |= PROCS_DAT_PARSE_FOUND_PROCESS;
						}
					}
					
					// Next line
					linenum++;
				}
				
				// Close file
				fclose(procs_file);
				
				// Was the process found
				if (!(proc_dat_parse_flags & PROCS_DAT_PARSE_FOUND_PROCESS))
					resp = REMOTE_SPAWN_RESP_NOT_SPAWNABLE;
			}
		}
		
		// Send response
		char out[16];
		sprintf(out, "%d\n", resp);
		if (sendto(sockfd, &out, strlen(out), 0, &remote_addr, addr_size) == -1)
			printf("Failed to send message.\n");
		// TODO: Convert to binary later
		/*
		// Convert to network ordering
		resp = htonl(resp);
		
		// Send response
		if (sendto(sockfd, &resp, sizeof(resp), 0, &remote_addr, addr_size) == -1)
			printf("Failed to send message.\n");
		*/
	}
	
	// Close socket
	close_listener();
}
