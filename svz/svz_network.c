#include "zebra.h"

#include "sockunion.h"
#include "log.h"
#include "sockopt.h"
#include "privs.h"
#include "prefix.h"
#include "stream.h"

#include "ospfd/ospfd.h"
#include "ospf6d/ospf6_proto.h"
extern struct zebra_privs_t svd_privs;

#include "svz/svz_network.h"
#include "svz/svz_tunnel.h"

struct tclient * tclient = NULL;

int
svz_net_init()
{
  tclient = svz_tunnel_new();
  svz_tunnel_init(tclient);
}
  
int
svz_net_message_send(struct stream * buf)
{
  stream_reset(tclient->obuf);

  tclient->obuf = stream_dup(buf);

  return svz_tunnel_send_message(tclient); 
}

svz_net_message_read(struct stream * buf)
{
  shim_sisis_write(buf);
}
