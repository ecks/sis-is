#include <zebra.h>

#include <thread.h>
#include <unistd.h>
#include <prefix.h>

#include <sisis_structs.h>
#include <sisis_api.h>
#include <sisis_process_types.h>

#include "ospf6_sisis.h"

struct in6_addr * 
get_shim_addr()
{
  char shim_addr[INET6_ADDRSTRLEN+1];
  sisis_create_addr(shim_addr, (uint64_t)SISIS_PTYPE_RIBCOMP_SHIM, (uint64_t)0, (uint64_t)0, (uint64_t)0, (uint64_t)0); 
  struct prefix_ipv6 shim_prefix = sisis_make_ipv6_prefix(shim_addr, 37);
  struct list_sis * shim_addrs = get_sisis_addrs_for_prefix(&shim_prefix);
  if(shim_addrs->size == 1)
  {
    struct listnode_sis * node = shim_addrs->head;
    return (struct in6_addr *)node->data;
  } 
  return NULL;
}
