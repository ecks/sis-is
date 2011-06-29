#include "zebra.h"

#include "thread.h"
#include "memory.h"
#include "stream.h"
#include "log.h"
#include "sockopt.h"
#include "linklist.h"
#include "stream.h"

#include "shim/shim_packet.h"
#include "shim/shim_network.h"
#include "shim/shim_sisis.h"
#include "shim/shimd.h"

static struct shim_master shim_master;

struct shim_master *sm;

void
shim_init (uint64_t host_num)
{
  struct shim * ns = shim_new (host_num);
//  listnode_add(sm->listen_sockets, ns); // is this necessary ?
}

struct shim *
shim_new (uint64_t host_num)
{
  struct shim * new = XCALLOC(MTYPE_SHIM, sizeof (struct shim));

  // socket to the outside
  if ((new->fd = shim_sock_init()) < 0)
  {
    zlog_err("shim_new: fatal error: shim_sock_init was unable to open "
             "a socket");
    exit(1);
  }
  new->maxsndbuflen = getsockopt_so_sendbuf (new->fd);
  if ((new->obuf = stream_new(OSPF_MAX_PACKET_SIZE+1)) == NULL)
  {
    zlog_err("shim_new: fatal error: stream_new(%u) failed allocating obuf",
             OSPF_MAX_PACKET_SIZE+1);
    exit(1);
  }
  new->t_read = thread_add_read (master, shim_read, new, new->fd);

  // internal socket to sisis
  if ((new->sis_fd = shim_sisis_init(host_num)) < 0)
  {
    zlog_err("shim_new: fatal error: shim_sisis_init was unable to "
             "open a socket");
    exit(1);
  }
  if ((new->ibuf = stream_new(OSPF_MAX_PACKET_SIZE+1)) == NULL)
  {
    zlog_err("shim_new: fatal error: stream_new(%u) failed allocating ibuf", 
             OSPF_MAX_PACKET_SIZE+1);
    exit(1);
  }
  return new;
}

void
shim_terminate ()
{
  struct shim * shim;
  struct listnode * node, * nnode;

  for(ALL_LIST_ELEMENTS(sm->listen_sockets, node, nnode, shim))
  {
    close(shim->fd);
    stream_free(shim->ibuf);
    XFREE (MTYPE_SHIM, shim);
  }

  list_delete(sm->listen_sockets);
 
  if(master)
    thread_master_free(master);
 
  exit(0);
}

void
shim_master_init()
{
  memset (&shim_master, 0, sizeof (struct shim_master));

  sm = &shim_master;
  sm->master = thread_master_create ();
  sm->listen_sockets = list_new();
  sm->start_time = quagga_time (NULL);
}
