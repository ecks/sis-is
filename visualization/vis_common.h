#include "../tests/sisis_api.h"

#ifndef _VIS_COMMON_H
#define _VIS_COMMON_H

#define PRE_LEN	 	4
#define TYPE_LEN 	4
#define HOST_LEN	4
#define PID_LEN		4

struct addr {
  char prefix_str[INET_ADDRSTRLEN];
  char pre[PRE_LEN];
  char type[TYPE_LEN];
  char host[HOST_LEN];
  char pid[PID_LEN];
};

struct addr * parse_ip_address(char prefix_str[]);
struct list * get_rib_routes();
struct list * extract_host_from_rib(struct list * rib_list, struct addr * addr_to_check);

#endif
