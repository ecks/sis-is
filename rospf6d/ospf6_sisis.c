#include <zebra.h>

#include <pthread.h>
#include <thread.h>
#include <unistd.h>
#include <prefix.h>
#include <netinet/in.h>
#include <log.h>

#include <sisis_structs.h>
#include <sisis_api.h>
#include <sisis_process_types.h>
#include <sisis_addr_format.h>

#include "ospf6_sisis.h"
#include "ospf6d.h"

pthread_mutex_t num_processes_mutex = PTHREAD_MUTEX_INITIALIZER;
int num_processes = -1;

uint64_t timestamp;
uint64_t host_num, pid;

pthread_mutex_t sisis_addr_mutex = PTHREAD_MUTEX_INITIALIZER;
char sisis_addr[INET6_ADDRSTRLEN] = { '\0' };

struct in6_addr * get_sv_addr(void)
{
  char sv_addr[INET6_ADDRSTRLEN+1];
  sisis_create_addr(sv_addr, (uint64_t)SISIS_PTYPE_RIBCOMP_SV, (uint64_t)0, (uint64_t)0, (uint64_t)0, (uint64_t)0); 
  struct prefix_ipv6 sv_prefix = sisis_make_ipv6_prefix(sv_addr, 37);
  struct list_sis * sv_addrs = get_sisis_addrs_for_prefix(&sv_prefix);
  if(sv_addrs->size == 1)
  {
    struct listnode_sis * node = sv_addrs->head;
    return (struct in6_addr *)node->data;
  } 
  return NULL;
}

struct in6_addr * get_svz_addr(void)
{
  char svz_addr[INET6_ADDRSTRLEN+1];
  sisis_create_addr(svz_addr, (uint64_t)SISIS_PTYPE_RIBCOMP_SVZ, (uint64_t)0, (uint64_t)0, (uint64_t)0, (uint64_t)0); 
  struct prefix_ipv6 svz_prefix = sisis_make_ipv6_prefix(svz_addr, 37);
  struct list_sis * svz_addrs = get_sisis_addrs_for_prefix(&svz_prefix);
  if(svz_addrs->size == 1)
  {
    struct listnode_sis * node = svz_addrs->head;
    return (struct in6_addr *)node->data;
  } 
  return NULL;
}


struct in6_addr * 
get_rospf6_addr(void)
{
  char rospf6_addr[INET6_ADDRSTRLEN+1];
  sisis_create_addr(rospf6_addr, (uint64_t)SISIS_PTYPE_RIBCOMP_OSPF6, (uint64_t)0, (uint64_t)0, (uint64_t)0, (uint64_t)0); 
  struct prefix_ipv6 rospf6_prefix = sisis_make_ipv6_prefix(rospf6_addr, 37);
  struct list_sis * rospf6_addrs = get_sisis_addrs_for_prefix(&rospf6_prefix);
  if(rospf6_addrs->size == 1)
  {
    struct listnode_sis * node = rospf6_addrs->head;
    return (struct in6_addr *)node->data;
  } 
  return NULL;
}

int sisis_add_ipv6_route(struct route_ipv6 * route, void * data)
{
  uint64_t ptype = (uint64_t)SISIS_PTYPE_RIBCOMP_OSPF6;
  uint64_t ptype_version = (uint64_t)OSPF6_SISIS_VERSION;

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
          if (process_type == ptype && ptype_version == ptype_version)
          {
            pthread_mutex_lock(&num_processes_mutex);
              if(num_processes == -1)
                num_processes = get_process_type_version_count(ptype, ptype_version);
              else
                num_processes++;
            pthread_mutex_unlock(&num_processes_mutex);
          }
        }
      }
    }
  }

  return 0;
}

int sisis_remove_ipv6_route(struct route_ipv6 * route, void * data)
{
  uint64_t ptype = (uint64_t)SISIS_PTYPE_RIBCOMP_OSPF6;
  uint64_t ptype_version = (uint64_t)OSPF6_SISIS_VERSION;

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
          if (process_type == ptype && ptype_version == ptype_version)
          {
            pthread_mutex_lock(&num_processes_mutex);
              // this is where we need to have another process try to bring itself back up
              if(num_processes == -1)
                num_processes = get_process_type_version_count(ptype, ptype_version);
              else
                num_processes--;
            pthread_mutex_unlock(&num_processes_mutex);

            check_redundancy();
          }
        }
      }
    }
  }

  return 0;
}

void redundancy_main(uint64_t host_num)
{
  uint64_t ptype = (uint64_t)SISIS_PTYPE_RIBCOMP_OSPF6;
  uint64_t ptype_version = (uint64_t)OSPF6_SISIS_VERSION;
 
  // set global variables 
  struct timeval tv;
  gettimeofday(&tv, NULL);
  timestamp = (tv.tv_sec * 100 + (tv.tv_usec / 10000)) & 0x00000000ffffffffLLU;   // In 100ths of seconds  
  pid = getpid();
  host_num = host_num;

  // Register address
  pthread_mutex_lock(&sisis_addr_mutex);
  if (sisis_register(sisis_addr, ptype, ptype_version, host_num, pid, timestamp) != 0)
  {    
    zlog_notice("Failed to register SIS-IS address.\n");
    exit(1);
  }
  pthread_mutex_unlock(&sisis_addr_mutex);

  ospf6_sisis_changes_subscribe();
}

void ospf6_sisis_changes_subscribe()
{
  uint64_t ptype = (uint64_t)SISIS_PTYPE_RIBCOMP_OSPF6;
  uint64_t ptype_version = (uint64_t)OSPF6_SISIS_VERSION;

  num_processes = get_process_type_version_count(ptype, ptype_version);

  struct subscribe_to_rib_changes_info info;
  info.rib_add_ipv6_route = sisis_add_ipv6_route;
  info.rib_remove_ipv6_route = sisis_remove_ipv6_route;
  info.data = NULL;
  subscribe_to_rib_changes(&info); 
}

int num_of_processes()
{
  return num_processes;
}

void check_redundancy(void)
{
  uint64_t rc_ptype = (uint64_t)SISIS_PTYPE_RIBCOMP_OSPF6;
  uint64_t sp_ptype = (uint64_t)SISIS_PTYPE_REMOTE_SPAWN;
  uint64_t ptype_version = (uint64_t)OSPF6_SISIS_VERSION;
  int local_num_processes;

  zlog_debug("Inside check_redundancy...");
 
  // Get list of all processes
  struct list_sis * proc_addrs = get_processes_by_type_version(rc_ptype, ptype_version);
  struct listnode_sis * node;  
 
  pthread_mutex_lock(&num_processes_mutex);
  if (num_processes == -1)
    num_processes = get_process_type_version_count(rc_ptype, ptype_version);
  local_num_processes = num_processes;
  pthread_mutex_unlock(&num_processes_mutex);
 
  zlog_debug("Done getting number of processes...");

  if(local_num_processes < MIN_NUM_PROCESSES)
  {
    int do_startup = 1;
    if(proc_addrs)
    {
      LIST_FOREACH(proc_addrs, node)
      {
        struct in6_addr * remote_addr = (struct in6_addr *)node->data;
        
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
    
    if(do_startup)
    {
      int num_to_start = MIN_NUM_PROCESSES - local_num_processes;
      // Check if the spawn process is running
      char spawn_addr[INET6_ADDRSTRLEN+1];
      sisis_create_addr(spawn_addr, (uint64_t)sp_ptype, (uint64_t)1, (uint64_t)0, (uint64_t)0, (uint64_t)0);
      struct prefix_ipv6 spawn_prefix = sisis_make_ipv6_prefix(spawn_addr, 42);
      struct list_sis * spawn_addrs = get_sisis_addrs_for_prefix(&spawn_prefix);
      if (spawn_addrs != NULL && spawn_addrs->size)
      {
        // there should be only on spawn process so far
        if (spawn_addrs->size == 1)
        {
          // Make new socket
          int spawn_sock = make_socket(NULL);
          if (spawn_sock == -1)
          {
            zlog_notice("Failed to open spawn socket.\n");
          }
          else
          {
            do
            { 
              LIST_FOREACH(spawn_addrs, node)
              {
                struct in6_addr * remote_addr = (struct in6_addr *)node->data;
             
                struct sockaddr_in6 sockaddr;
                int sockaddr_size = sizeof(sockaddr);
                memset(&sockaddr, 0, sockaddr_size);
                sockaddr.sin6_family = AF_INET6;
                sockaddr.sin6_port = htons(REMOTE_SPAWN_PORT);
                sockaddr.sin6_addr = *remote_addr; 
 
                char req[32];
                sprintf(req, "%d %llu %llu", REMOTE_SPAWN_REQ_START, rc_ptype, ptype_version);
                if (sendto(spawn_sock, req, strlen(req), 0, (struct sockaddr *)&sockaddr, sockaddr_size) == -1)
                {
                  zlog_notice("Failed to send message.  Error: %i\n", errno);
                }
                else
                  num_to_start--;

                // Have we started enough?
                if (num_to_start == 0)
                  break;
              }
            }while (num_to_start > 0);

            close(spawn_sock);
          }
        }
      }
     
      if(spawn_addrs)
        FREE_LINKED_LIST(spawn_addrs); 
    }      
  }

}

/** Creates a new socket */
int make_socket(char * port)
{
  int fd;
            
  // Set up socket address info
  struct addrinfo hints, *addr;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET6;     // IPv6
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
