#ifndef SV_INTERFACE_H
#define SV_INTERFACE_H

struct shim_interface 
{
  struct interface * interface;
  struct in6_addr * linklocal_addr;
  u_char instance_id;
  u_int32_t ifmtu;
};
 
extern struct shim_interface * shim_interface_lookup_by_ifindex (int ifindex);
extern struct shim_interface * shim_interface_create (struct interface * ifp);
extern int shim_interface_up (struct thread * thread);
extern void shim_interface_if_add (struct interface * ifp);
extern void shim_interface_if_del (struct interface * ifp);
extern void shim_interface_state_update (struct interface * ifp);

#endif
