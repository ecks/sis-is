#include "zebra.h"

#include "thread.h"
#include "memory.h"
#include "log.h"
#include "if.h"
#include "stream.h"

#include "rospf6d/ospf6_message.h"
#include "shim/shimd.h"
#include "shim/shim_interface.h"
#include "shim/shim_packet.h"

struct shim_interface *
shim_interface_lookup_by_ifindex (int ifindex)
{
  struct shim_interface * si; 
  struct interface *ifp;

  ifp = if_lookup_by_index (ifindex);
  if (ifp == NULL)
    return (struct shim_interface *) NULL;

  si = (struct shim_interface *) ifp->info;
  return si;
}

struct shim_interface *
shim_interface_create (struct interface * ifp)
{
  struct shim_interface * si;
  unsigned int iobuflen;

  si = (struct shim_interface *)
    XCALLOC (MTYPE_SHIM_IF, sizeof (struct shim_interface));
  
  if (!si)
  {
    zlog_err ("Can't malloc ospf6_interface for ifindex %d", ifp->ifindex);
    return (struct shim_interface *) NULL;
  } 

  si->linklocal_addr = (struct in6_addr *)NULL;
  si->instance_id = 0;
  si->ifmtu = ifp->mtu6;
  iobuflen = shim_iobuf_size (ifp->mtu6);
  if (si->ifmtu > iobuflen)
  {
    si->ifmtu = iobuflen;
  }

  si->interface = ifp;
  ifp->info = si;

  return si;
}

void
shim_interface_if_add (struct interface * ifp)
{
  struct shim_interface * si;
  unsigned int iobuflen;

  si = (struct shim_interface *) ifp->info;
  if (si == NULL)
    return;

  /* Try to adjust I/O buffer size with IfMtu */
  if (si->ifmtu == 0)
    si->ifmtu = ifp->mtu6;
  iobuflen = shim_iobuf_size (ifp->mtu6);
  if (si->ifmtu > iobuflen)
  {    
      si->ifmtu = iobuflen;
  }

  printf("interface add: %s index %d mtu %d\n", ifp->name, ifp->ifindex, ifp->mtu6);
  thread_add_event (master, shim_interface_up, si, 0);  
}

void 
shim_interface_if_del (struct interface * ifp)
{

}

void
shim_interface_state_update (struct interface * ifp)
{

}

int 
shim_interface_up (struct thread * thread)
{
  struct shim_interface * si;
 
  si = (struct shim_interface *) THREAD_ARG (thread);
 
//  assert (si & si->interface);

//  if (IS_OSPF6_DEBUG_INTERFACE)
//    zlog_debug ("Interface Event %s: [InterfaceUp]",
//		si->interface->name);

  if (! if_is_up (si->interface))
  {
    printf("interface %s is down\n", si->interface->name);
    return 0;
  }

  /* Join AllSPFRouters */
//  shim_join_allspfrouters(si->interface->index);

  return 0;
}

int 
shim_interface_down (struct thread * thread)
{
  struct shim_interface * si;
  
  si = (struct shim_interface *) THREAD_ARG (thread);
//  assert (si & si->interface);
 
//  shim_leave_allspfrouters (si->interface->index);

  return 0;
}
