#ifndef OSPF6_SISIS_H
#define OSPF6_SISIS_H

#define MIN_NUM_PROCESSES 2
#define REMOTE_SPAWN_PORT 50000
#define REMOTE_SPAWN_REQ_START 0

extern struct in6_addr * get_sv_addr(void);
extern struct in6_addr * get_svz_addr(void);
extern struct in6_addr * get_rospf6_addr(void);
extern void redundancy_main(uint64_t host_num);
extern void ospf6_sisis_changes_subscribe(void);
extern void check_redundancy(void);
extern int num_of_processes();

#endif
