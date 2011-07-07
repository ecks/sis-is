#ifndef _ZEBRA_SHIM_NETWORK_H
#define _ZEBRA_SHIM_NETWORK_H

extern struct in6_addr allspfrouters6;
extern struct in6_addr alldrouters6;

extern int shim_sock_init(void);

extern void shim_join_allspfrouters (int ifindex);
extern void shim_leave_allspfrouters (u_int ifindex);
extern void shim_join_alldrouters (u_int ifindex);
extern void shim_leave_alldrouters (u_int ifindex);
extern int shim_sendmsg (struct in6_addr *src, struct in6_addr *dst, unsigned int *ifindex, struct iovec *message);
extern int shim_recvmsg (struct in6_addr * src, struct in6_addr * dst, unsigned int * ifindex, struct iovec * message, unsigned int fd, struct stream * buf, unsigned int len);

#endif
