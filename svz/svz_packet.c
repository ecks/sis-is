#include "zebra.h"

#include "memory.h"
#include "thread.h"
#include "if.h"
#include "log.h"
#include "sockopt.h"
#include "stream.h"
#include "buffer.h"
#include "svz/svzd.h"
#include "svz/svz_interface.h"
#include "svz/svz_network.h"

#include "rospf6d/ospf6_message.h"
#include "rospf6d/ospf6_proto.h"
#include "rospf6d/ospf6_lsa.h"
#include "svz/svz_tunnel.h"
#include "svz/svz_packet.h"
#include "svz/svz_sisis.h"

const char *ospf6_message_type_str[] =
  { "Unknown", "Hello", "DbDesc", "LSReq", "LSUpdate", "LSAck" };

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


static void
shim_header_print (struct ospf6_header *oh)
{
  char router_id[16], area_id[16];
  inet_ntop (AF_INET, &oh->router_id, router_id, sizeof (router_id));
  inet_ntop (AF_INET, &oh->area_id, area_id, sizeof (area_id));

  zlog_debug ("    OSPFv%d Type:%d Len:%hu Router-ID:%s",
             oh->version, oh->type, ntohs (oh->length), router_id);
  zlog_debug ("    Area-ID:%s Cksum:%hx Instance-ID:%d",
             area_id, ntohs (oh->checksum), oh->instance_id);
}

void
shim_hello_print (struct ospf6_header *oh)
{
  struct ospf6_hello *hello;
  char options[16];
  char drouter[16], bdrouter[16], neighbor[16];
  char *p;

  shim_header_print (oh);
  assert (oh->type == OSPF6_MESSAGE_TYPE_HELLO);

  hello = (struct ospf6_hello *)
    ((caddr_t) oh + sizeof (struct ospf6_header));

  inet_ntop (AF_INET, &hello->drouter, drouter, sizeof (drouter));
  inet_ntop (AF_INET, &hello->bdrouter, bdrouter, sizeof (bdrouter));
//  ospf6_options_printbuf (hello->options, options, sizeof (options)); 
  char * opt = "N/A";
  memcpy(options, opt, sizeof("N/A"));
  zlog_debug ("    I/F-Id:%ld Priority:%d Option:%s",
             (u_long) ntohl (hello->interface_id), hello->priority, options);
  zlog_debug ("    HelloInterval:%hu DeadInterval:%hu",
             ntohs (hello->hello_interval), ntohs (hello->dead_interval));
  zlog_debug ("    DR:%s BDR:%s", drouter, bdrouter);

  for (p = (char *) ((caddr_t) hello + sizeof (struct ospf6_hello));
       p + sizeof (u_int32_t) <= OSPF6_MESSAGE_END (oh);
       p += sizeof (u_int32_t))
    {
      inet_ntop (AF_INET, (void *) p, neighbor, sizeof (neighbor));
      zlog_debug ("    Neighbor: %s", neighbor);
    }

  if (p != OSPF6_MESSAGE_END (oh))
    zlog_debug ("Trailing garbage exists");
}

const char *
ospf6_lstype_name (u_int16_t type)
{
  static char buf[8];
  struct ospf6_lsa_handler *handler;

//  handler = ospf6_get_lsa_handler (type);
//  if (handler && handler != &unknown_handler)
//    return handler->name;

  snprintf (buf, sizeof (buf), "0x%04hx", ntohs (type));
  return buf;
}

void
shim_lsa_header_print_raw (struct ospf6_lsa_header *header)
{
  char id[16], adv_router[16];
  inet_ntop (AF_INET, &header->id, id, sizeof (id));
  inet_ntop (AF_INET, &header->adv_router, adv_router,
             sizeof (adv_router));
  zlog_debug ("    [%s Id:%s Adv:%s]",
	      ospf6_lstype_name (header->type), id, adv_router);
  zlog_debug ("    Age: %4hu SeqNum: %#08lx Cksum: %04hx Len: %d",
	      ntohs (header->age), (u_long) ntohl (header->seqnum),
	      ntohs (header->checksum), ntohs (header->length));
}

void
shim_dbdesc_print (struct ospf6_header *oh)
{
  struct ospf6_dbdesc *dbdesc;
  char options[16];
  char *p;

  shim_header_print (oh);
  assert (oh->type == OSPF6_MESSAGE_TYPE_DBDESC);

  dbdesc = (struct ospf6_dbdesc *)
    ((caddr_t) oh + sizeof (struct ospf6_header));

//  ospf6_options_printbuf (dbdesc->options, options, sizeof (options));

  zlog_debug ("    MBZ: %#x Option: %s IfMTU: %hu",
             dbdesc->reserved1, options, ntohs (dbdesc->ifmtu));
  zlog_debug ("    MBZ: %#x Bits: %s%s%s SeqNum: %#lx",
             dbdesc->reserved2,
             (CHECK_FLAG (dbdesc->bits, OSPF6_DBDESC_IBIT) ? "I" : "-"),
             (CHECK_FLAG (dbdesc->bits, OSPF6_DBDESC_MBIT) ? "M" : "-"),
             (CHECK_FLAG (dbdesc->bits, OSPF6_DBDESC_MSBIT) ? "m" : "s"),
             (u_long) ntohl (dbdesc->seqnum));

  for (p = (char *) ((caddr_t) dbdesc + sizeof (struct ospf6_dbdesc));
       p + sizeof (struct ospf6_lsa_header) <= OSPF6_MESSAGE_END (oh);
       p += sizeof (struct ospf6_lsa_header))
    shim_lsa_header_print_raw ((struct ospf6_lsa_header *) p);

  if (p != OSPF6_MESSAGE_END (oh))
    zlog_debug ("Trailing garbage exists");
}

void
shim_lsreq_print (struct ospf6_header *oh) 
{
  char id[16], adv_router[16];
  char *p;

  shim_header_print (oh);
  assert (oh->type == OSPF6_MESSAGE_TYPE_LSREQ);

  for (p = (char *) ((caddr_t) oh + sizeof (struct ospf6_header));
       p + sizeof (struct ospf6_lsreq_entry) <= OSPF6_MESSAGE_END (oh);
       p += sizeof (struct ospf6_lsreq_entry))
  {    
      struct ospf6_lsreq_entry *e = (struct ospf6_lsreq_entry *) p;
      inet_ntop (AF_INET, &e->adv_router, adv_router, sizeof (adv_router));      
      inet_ntop (AF_INET, &e->id, id, sizeof (id));      
      zlog_debug ("    [%s Id:%s Adv:%s]",
                 ospf6_lstype_name (e->type), id, adv_router);    
  }
    
  if (p != OSPF6_MESSAGE_END (oh))
    zlog_debug ("Trailing garbage exists");
}

void
shim_lsupdate_print (struct ospf6_header *oh)
{
  struct ospf6_lsupdate *lsupdate;  u_long num;
  char *p;

  shim_header_print (oh);
  assert (oh->type == OSPF6_MESSAGE_TYPE_LSUPDATE);

  lsupdate = (struct ospf6_lsupdate *)
    ((caddr_t) oh + sizeof (struct ospf6_header));
  num = ntohl (lsupdate->lsa_number);
  zlog_debug ("    Number of LSA: %ld", num);  for (p = (char *) ((caddr_t) lsupdate + sizeof (struct ospf6_lsupdate));       p < OSPF6_MESSAGE_END (oh) &&
       p + OSPF6_LSA_SIZE (p) <= OSPF6_MESSAGE_END (oh);       p += OSPF6_LSA_SIZE (p))    {      shim_lsa_header_print_raw ((struct ospf6_lsa_header *) p);
      if (OSPF6_LSA_SIZE (p) < sizeof (struct ospf6_lsa_header))
        {
          zlog_debug ("    Malformed LSA length, quit printing");
          break;
        }
    }

  if (p != OSPF6_MESSAGE_END (oh))
    {
      char buf[32];

      int num = 0;
      memset (buf, 0, sizeof (buf));

      zlog_debug ("    Trailing garbage exists");
      while (p < OSPF6_MESSAGE_END (oh))
        {
          snprintf (buf, sizeof (buf), "%s %2x", buf, *p++);
          num++;
          if (num == 8)
            {
              zlog_debug ("    %s", buf);
              memset (buf, 0, sizeof (buf));
              num = 0;
            }
        }
      if (num)
        zlog_debug ("    %s", buf);
    }
}

void
shim_lsack_print (struct ospf6_header *oh) 
{
  char *p;

  shim_header_print (oh);
  assert (oh->type == OSPF6_MESSAGE_TYPE_LSACK);

  for (p = (char *) ((caddr_t) oh + sizeof (struct ospf6_header));
       p + sizeof (struct ospf6_lsa_header) <= OSPF6_MESSAGE_END (oh);
       p += sizeof (struct ospf6_lsa_header))
    shim_lsa_header_print_raw ((struct ospf6_lsa_header *) p);

  if (p != OSPF6_MESSAGE_END (oh))
    zlog_debug ("Trailing garbage exists");
}

int 
shim_receive (struct tclient * tclient)
{
  return shim_sisis_write(tclient->ibuf, wb);
}

/*
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
*/
  /* first of all get interface pointer. */
/*  tclient = THREAD_ARG (thread);

  shim->t_read = thread_add_read (master, shim_receive, shim, shim->fd);
*/
//  stream_reset(shim->ibuf);
//  if (!(ibuf = shim_recv_packet (shim->fd, &ifp, shim->ibuf)))
//    return -1;

//  iph = (struct ip *) STREAM_DATA (ibuf);

//  stream_forward_getp (ibuf, iph->ip_hl * 4);
//  oh = (struct ospf_header *) STREAM_PNT (ibuf);
/*
  memset (&src, 0, sizeof(src));
  memset (&dst, 0, sizeof(dst));
  ifindex = 0;

  obuf = stream_new (SV_HEADER_SIZE + iobuflen);
  sv_create_header (obuf, SV_MESSAGE);

  iovector[0].iov_base = (obuf->data + obuf->endp);
  iovector[0].iov_len = iobuflen;
  iovector[1].iov_base = NULL;
  iovector[1].iov_len = 0;

  obuf = stream_new (SV_HEADER_SIZE + iobuflen);
  sv_create_header (obuf, SV_MESSAGE);
*/
//  len = shim_recvmsg (&src, &dst, &ifindex, iovector, shim->fd, obuf, iobuflen);
/*  if (len > iobuflen)
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

  size_t endp_save = stream_get_endp (obuf);
  obuf->endp = 4; 
  stream_put (obuf, &src, sizeof (struct in6_addr));
  stream_put (obuf, &dst, sizeof (struct in6_addr));
  obuf->endp = endp_save;

  stream_putl_at (obuf, 36, ifindex);

  si = shim_interface_lookup_by_ifindex (ifindex);
  if (si == NULL)
  {    
    zlog_debug ("Message received on disabled interface");
    return 0;
  } 

  zlog_notice("Received packet on int %d", ifindex);  

  struct stream * obuf_d = stream_dup (obuf);
  stream_forward_getp (obuf_d, SV_HEADER_SIZE);
  oh = (struct ospf6_header *) recvbuf;
  stream_get(oh, obuf_d, len);
  switch (oh->type)
  {    
    case OSPF6_MESSAGE_TYPE_HELLO: */
//      zlog_debug("Received OSPF6 hello");
//      shim_hello_print (oh);
/*      break;
    case OSPF6_MESSAGE_TYPE_DBDESC: */
//      zlog_debug("Received DBDESC");
//      shim_dbdesc_print (oh);
/*      break;
    case OSPF6_MESSAGE_TYPE_LSREQ: */
//      ospf6_lsreq_print (oh);
/*      break;
    case OSPF6_MESSAGE_TYPE_LSUPDATE: */
//      ospf6_lsupdate_print (oh);
/*      break;
    case OSPF6_MESSAGE_TYPE_LSACK: */
//      ospf6_lsack_print (oh);
/*      break;
    default:
      zlog_debug ("Unknown message, type: %d", oh->type);
      break;
  }

  shim_sisis_write (obuf, wb); 

  return 0;
} */

/*int
shim_send(struct stream * s, struct shim_interface * si)
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
} */

void
shim_send(struct in6_addr * src, struct in6_addr * dst, 
	  struct shim_interface * si, struct stream * ibuf, uint16_t length)
{
  int len;
  char srcname[64], dstname[64];
  struct ospf6_header * oh;
  struct iovec iovector[2];

  /* initialize */
  iovector[0].iov_base = (ibuf->data + ibuf->getp);
  iovector[0].iov_len = length;
  iovector[1].iov_base = NULL;
  iovector[1].iov_len = 0;

  oh = (ibuf->data + ibuf->getp);

  switch (oh->type)
  {    
      case OSPF6_MESSAGE_TYPE_HELLO:
        zlog_debug("Sending Hello");
        shim_hello_print(oh);
        break;
      case OSPF6_MESSAGE_TYPE_DBDESC:
        zlog_debug("Sending DBDESC");
        shim_dbdesc_print(oh);
        break;
      case OSPF6_MESSAGE_TYPE_LSREQ:
        zlog_debug("Sending LSREQ");
        shim_lsreq_print (oh);
        break;
      case OSPF6_MESSAGE_TYPE_LSUPDATE:
        zlog_debug("Sending LSUPDATE");
        shim_lsupdate_print (oh);
        break;
      case OSPF6_MESSAGE_TYPE_LSACK:
        zlog_debug("Sending LSACK");
        shim_lsack_print (oh);
        break;
      default:
        zlog_debug ("Unknown message");
        assert (0); 
        break;
  } 
    /* fill OSPF header */
//  oh->version = OSPFV3_VERSION;                     // need to fill in for later
  /* message type must be set before */
  /* message length must be set before */
//  oh->router_id = si->area->ospf6->router_id;
//  oh->area_id = si->area->area_id;
  /* checksum is calculated by kernel */
//  oh->instance_id = si->instance_id;                // need to fill in for later
//  oh->reserved = 0;                                 // need to fill in for later

  /* Log */
/*  if (IS_OSPF6_DEBUG_MESSAGE (oh->type, SEND))
  {    
    inet_ntop (AF_INET6, dst, dstname, sizeof (dstname));
    if (src)
      inet_ntop (AF_INET6, src, srcname, sizeof (srcname));
    else 
      memset (srcname, 0, sizeof (srcname));
    zlog_debug ("%s send on %s",
               OSPF6_MESSAGE_TYPE_NAME (oh->type), si->interface->name);
    zlog_debug ("    src: %s", srcname);
    zlog_debug ("    dst: %s", dstname);

    switch (oh->type)
    {    
      case OSPF6_MESSAGE_TYPE_HELLO:
        shim_hello_print (oh);
        break;
      case OSPF6_MESSAGE_TYPE_DBDESC:
//        ospf6_dbdesc_print (oh);
        break;
      case OSPF6_MESSAGE_TYPE_LSREQ:
//        ospf6_lsreq_print (oh);
         break;
      case OSPF6_MESSAGE_TYPE_LSUPDATE:
//        ospf6_lsupdate_print (oh);
        break;
      case OSPF6_MESSAGE_TYPE_LSACK:
//          ospf6_lsack_print (oh);
          break;
      default:
        zlog_debug ("Unknown message");
        assert (0); 
        break;
    } */
//  }    
    /* send message */
//  len = shim_sendmsg (src, dst, &si->interface->ifindex, iovector, shim->fd, ibuf, length);
  if (len != length)
    zlog_err ("Could not send entire message");
}
