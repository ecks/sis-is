#include <zebra.h>

#include <thread.h>
#include <unistd.h>
#include <prefix.h>

#include <sisis_structs.h>
#include <sisis_api.h>
#include <sisis_process_types.h>

#include "rospf6d/ospf6_sisis.h"

struct in6_addr * 
get_sv_addr()
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

struct in6_addr * 
get_svz_addr()
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

void
rospf6_sisis_register (uint64_t host_num)
{
  uint64_t ptype, ptype_version, pid, timestamp;
  char sisis_addr[INET6_ADDRSTRLEN];

  // Store process type
  ptype = (uint64_t)SISIS_PTYPE_RIBCOMP_OSPF6;
  ptype_version = (uint64_t)VERSION;

  // Get pid
  pid = getpid();        // Get start time
  struct timeval tv; 
  gettimeofday(&tv, NULL);
  timestamp = (tv.tv_sec * 100 + (tv.tv_usec / 10000)) & 0x00000000ffffffffLLU;   // In 100ths of seconds

  // Register SIS-IS address
  if (sisis_register(sisis_addr, ptype, ptype_version, host_num, pid, timestamp) != 0)
  {
    zlog_notice("Failed to register SIS-IS address.");
    exit(1);    
  }
}
