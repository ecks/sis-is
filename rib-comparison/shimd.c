#include "zebra.h"

#include "thread.h"
#include "memory.h"
#include "stream.h"
#include "log.h"
#include "sockopt.h"

#include "rib-comparison/shim_packet.h"
#include "rib-comparison/shim_network.h"
#include "rib-comparison/shimd.h"

static struct shim_master shim_master;

struct shim_master *sm;

static struct shim *
shim_new ()
{
  struct shim * new = XCALLOC(MTYPE_SHIM, sizeof (struct shim));

  if ((new->fd = shim_sock_init()) < 0)
  {
    zlog_err("shim_new: fatal error: shim_sock_init was unable to open "
             "a socket");
    exit(1);
  }
  new->maxsndbuflen = getsockopt_so_sendbuf (new->fd);
  if ((new->ibuf = stream_new(OSPF_MAX_PACKET_SIZE+1)) == NULL)
  {
    zlog_err("shim_new: fatal error: stream_new(%u) failed allocating ibuf",
             OSPF_MAX_PACKET_SIZE+1);
    exit(1);
  }
  new->t_read = thread_add_read (master, shim_read, new, new->fd);

  return new;
}

void
shim_terminate ()
{
  
}

void
shim_master_init()
{
  memset (&shim_master, 0, sizeof (struct shim_master));

  sm = &shim_master;
  sm->master = thread_master_create ();
  sm->start_time = quagga_time (NULL);
}
