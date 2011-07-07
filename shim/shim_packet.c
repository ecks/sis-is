#include "zebra.h"

#include "memory.h"
#include "thread.h"
#include "if.h"
#include "log.h"
#include "sockopt.h"
#include "stream.h"
#include "buffer.h"
#include "sv.h"
#include "shim/shimd.h"
#include "shim/shim_interface.h"
#include "shim/shim_network.h"

#include "rospf6d/ospf6_message.h"
#include "rospf6d/ospf6_proto.h"
#include "shim/shim_packet.h"
#include "shim/shim_sisis.h"

static u_char * recvbuf = NULL;
static u_char * sendbuf = NULL;
static struct buffer * wb = NULL;

static unsigned int iobuflen = 0;
 	
int
shim_iobuf_size (unsigned int size)
{
  u_char * recvnew, * sendnew;
  
  if (size <= iobuflen)
    return iobuflen;

  recvnew = XMALLOC (MTYPE_OSPF6_MESSAGE, size);
  sendnew = XMALLOC (MTYPE_OSPF6_MESSAGE, size);
  
  if (recvnew == NULL || sendnew == NULL)
  {
    if (recvnew)
      XFREE (MTYPE_OSPF6_MESSAGE, recvnew);
    if (sendnew)
      XFREE (MTYPE_OSPF6_MESSAGE, sendnew);
    zlog_debug ("Could not allocate I/O buffer of size %d.", size);
    return iobuflen;
  }    

  if (recvbuf)
    XFREE (MTYPE_OSPF6_MESSAGE, recvbuf);
  if (sendbuf)
    XFREE (MTYPE_OSPF6_MESSAGE, sendbuf);
  recvbuf = recvnew;
  sendbuf = sendnew;

  if (wb)
    buffer_free (wb);
  wb = buffer_new(0);

  iobuflen = size;
  return iobuflen;
}

int 
shim_receive (struct thread * thread)
{
  struct stream * obuf;
  struct shim * shim;
  struct interface * ifp;
  struct shim_interface * si;
  unsigned int ifindex, len;
  struct in6_addr src, dst;
  struct iovec iovector[2];
  struct ospf6_header * oh;
  struct ip * iph;

  zlog_notice("Received packet!");  

  /* first of all get interface pointer. */
  shim = THREAD_ARG (thread);

  shim->t_read = thread_add_read (master, shim_receive, shim, shim->fd);

//  stream_reset(shim->ibuf);
//  if (!(ibuf = shim_recv_packet (shim->fd, &ifp, shim->ibuf)))
//    return -1;

//  iph = (struct ip *) STREAM_DATA (ibuf);

//  stream_forward_getp (ibuf, iph->ip_hl * 4);
//  oh = (struct ospf_header *) STREAM_PNT (ibuf);

  memset (&src, 0, sizeof(src));
  memset (&dst, 0, sizeof(dst));
  ifindex = 0;
//  memset (recvbuf, 0, iobuflen);
//  iovector[0].iov_base = recvbuf;
  iovector[0].iov_len = iobuflen;
//  iovector[1].iov_base = NULL;
//  iovector[1].iov_len = 0;

  obuf = stream_new (SV_HEADER_SIZE + iobuflen);
  sv_create_header (obuf, ROSPF6_MESSAGE_HELLO);

  printf("iobuflen: %d\n", iobuflen);
  len = shim_recvmsg (&src, &dst, &ifindex, iovector, shim->fd, obuf, iobuflen);
  if (len > iobuflen)
  {    
    zlog_err ("Excess message read");
    return 0;
  }    
  else if (len < sizeof (struct ospf6_header))
  {    
    zlog_err ("Deficient message read");
    return 0;
  }    

  stream_putw_at(obuf, 0, stream_get_endp(obuf));

  si = shim_interface_lookup_by_ifindex (ifindex);
  if (si == NULL)
  {    
    zlog_debug ("Message received on disabled interface");
    return 0;
  } 

  shim_sisis_write (obuf, wb); 
/*  oh = (struct ospf6_header *) STREAM_DATA(ibuf);

  switch (oh->type)
  {
    case OSPF6_MESSAGE_TYPE_HELLO:
      zlog_notice("hello");
//      shim_hello_recv (&src, &dst, si, oh, len);
      break;
    case OSPF6_MESSAGE_TYPE_DBDESC:
      zlog_notice("dbdesc\n");
      break;
    case OSPF6_MESSAGE_TYPE_LSREQ:
      zlog_notice("lsreq\n");
      break;
    case OSPF6_MESSAGE_TYPE_LSUPDATE:
      zlog_notice("lsupdate\n");
      break; 
    case OSPF6_MESSAGE_TYPE_LSACK:
      zlog_notice("lsack\n");
      break;
    default:
      zlog_notice("Unknown message\n");
      break;
  } */

  return 0;
}

int
shim_hello_send(struct stream * s, struct shim_interface * si)
{
  struct ospf6_header * oh;
  struct ospf6_hello * hello;
  u_char * p;

  memset (sendbuf, 0, iobuflen);
  oh = (struct ospf6_header *) sendbuf;
  hello = (struct ospf6_hello *)((caddr_t) oh + sizeof (struct ospf6_header));

  hello->interface_id = si->interface->ifindex;
  hello->priority = stream_getc(s);
  hello->options[0] = stream_getc(s);
  hello->options[1] = stream_getc(s);
  hello->options[2] = stream_getc(s);
  hello->hello_interval = stream_getw(s);
  hello->dead_interval = stream_getw(s);
  hello->drouter = stream_getl(s);
  hello->bdrouter = stream_getl(s);

  p = (u_char *)((caddr_t) hello + sizeof (struct ospf6_hello));

  while (STREAM_READABLE(s) >= sizeof(u_int32_t))
  {
    u_int32_t router_id = (u_int32_t)stream_getl(s);
    memcpy (p, &router_id, sizeof(u_int32_t));
    if (STREAM_READABLE(s) >= sizeof(u_int32_t))
      p += sizeof (u_int32_t);
  }
 
  oh->type = OSPF6_MESSAGE_TYPE_HELLO;
  oh->length = htons (p - sendbuf); 
  shim_send (si->linklocal_addr, &allspfrouters6, si, oh);
  return 0;
}

void
shim_send(struct in6_addr * src, struct in6_addr * dst, 
	  struct shim_interface * si, struct ospf6_header * oh)
{
  int len;
  struct iovec iovector[2];
    /* initialize */
  iovector[0].iov_base = (caddr_t) oh;
  iovector[0].iov_len = ntohs (oh->length);
  iovector[1].iov_base = NULL;
  iovector[1].iov_len = 0;

    /* fill OSPF header */
  oh->version = OSPFV3_VERSION;
  /* message type must be set before */
  /* message length must be set before */
//  oh->router_id = si->area->ospf6->router_id;
//  oh->area_id = si->area->area_id;
  /* checksum is calculated by kernel */
  oh->instance_id = si->instance_id;
  oh->reserved = 0;

    /* send message */
  len = shim_sendmsg (src, dst, &si->interface->ifindex, iovector);
  if (len != ntohs (oh->length))
    zlog_err ("Could not send entire message");
}
