#include "../tests/sisis_api.h"

#ifndef _VIS_MAIN_H
#define _VIS_MAIN_H

#define PRE_LEN	 	3
#define TYPE_LEN 	3
#define HOST_LEN	3
#define PID_LEN		3

struct addr {
  char prefix_str[INET_ADDRSTRLEN];
  char pre[PRE_LEN];
  char type[TYPE_LEN];
  char host[HOST_LEN];
  char pid[PID_LEN];
};

#endif
