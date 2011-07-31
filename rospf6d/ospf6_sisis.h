#ifndef OSPF6_SISIS_H
#define OSPF6_SISIS_H

extern struct in6_addr * get_sv_addr();
extern struct in6_addr * get_svz_addr();
extern void rospf6_sisis_register (uint64_t host_num);

#endif
