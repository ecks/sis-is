#include "zebra.h"

#include "command.h"
#include "if.h"
#include "memory.h"
#include "vty.h"

#include "sv/svd.h"
#include "sv/sv_interface.h"

DEFUN (router_svz,
       router_svz_cmd,
       "router svz",
       ROUTER_STR
       "svz\n"
      )
{
  vty->node = SHIM_NODE;
  return CMD_SUCCESS;
}

DEFUN (svz_interface,
       svz_interface_cmd,
       "interface IFNAME",
       "Enable svz on interface\n"
       IFNAME_STR
      )
{
  struct interface *ifp;
  struct shim_interface * si;

  ifp = if_get_by_name (argv[0]);
  si = (struct shim_interface *)ifp->info;
  if (si == NULL)
    si = shim_interface_create (ifp);
 
  thread_add_event (master, shim_interface_up, si, 0); 
  return CMD_SUCCESS;
} 

static int
config_write_shim (struct vty * vty)
{
  return CMD_SUCCESS;
}

/* OSPF6 node structure. */
static struct cmd_node shim_node =
{
  SHIM_NODE,
  "%s(config-shim)# ",
  1 /* VTYSH */
};

void
shim_top_init (void)
{
  install_node (&shim_node, config_write_shim);
  install_element (CONFIG_NODE, &router_svz_cmd);

  install_default (SHIM_NODE);
  install_element (SHIM_NODE, &svz_interface_cmd);
}
