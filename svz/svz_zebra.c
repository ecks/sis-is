#include <zebra.h>

#include "prefix.h"
#include "zclient.h"

#include "svz/svz_zebra.h"

struct zclient * zclient = NULL;

static int
shim_zebra_if_add (int command, struct zclient * zclient, zebra_size_t length)
{
  struct interface * ifp;
  
  ifp = zebra_interface_add_read (zclient->ibuf);
  shim_interface_if_add (ifp);

  return 0;
}

static int
shim_zebra_if_del (int command, struct zclient * zclient, zebra_size_t length)
{
  struct interface * ifp;

  if (!(ifp = zebra_interface_state_read (zclient->ibuf)))
    return 0;

  if (if_is_up (ifp))
    zlog_warn ("Zebra: got delete of %s, but interface is still up", ifp->name);

  shim_interface_if_del (ifp);

  ifp->ifindex = IFINDEX_INTERNAL;
  return 0;
}

static int
shim_zebra_if_state_update (int command, struct zclient * zclient, zebra_size_t length)
{
  struct interface * ifp;

  shim_interface_state_update (ifp);

  return 0;
}

void
shim_zebra_init ()
{
  zclient = zclient_new ();
  zclient_init (zclient, ZEBRA_ROUTE_BGP);
  zclient->router_id_update = NULL;
  zclient->interface_add = shim_zebra_if_add;
  zclient->interface_delete = shim_zebra_if_del;
  zclient->interface_up = shim_zebra_if_state_update;
  zclient->interface_up = shim_zebra_if_state_update;
  zclient->interface_address_add = NULL;
  zclient->interface_address_delete = NULL;
  zclient->ipv4_route_add = NULL;
  zclient->ipv4_route_delete = NULL;
  zclient->interface_up = NULL;
  zclient->interface_down = NULL;
#ifdef HAVE_IPV6
  zclient->ipv6_route_add = NULL;
  zclient->ipv6_route_delete = NULL;
#endif /* HAVE_IPV6 */
}
