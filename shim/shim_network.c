#include "zebra.h"

#include "sockunion.h"
#include "log.h"
#include "sockopt.h"
#include "privs.h"
#include "prefix.h"
#include "stream.h"

#include "ospfd/ospfd.h"
#include "ospf6d/ospf6_proto.h"
extern struct zebra_privs_t shimd_privs;

#include "shim/shimd.h"
#include "shim/shim_network.h"

struct in6_addr allspfrouters6;
struct in6_addr alldrouters6;

int
shim_sock_init(void)
{
  int shim_sock;

  if ( shimd_privs.change (ZPRIVS_RAISE) )
    zlog_err ("shim_sock_init: could not raise privs, %s",
               safe_strerror (errno) );
    
  shim_sock = socket (AF_INET6, SOCK_RAW, IPPROTO_OSPFIGP);
  if (shim_sock < 0)
    {
      int save_errno = errno;
      if ( shimd_privs.change (ZPRIVS_LOWER) )
        zlog_err ("shim_sock_init: could not lower privs, %s",
                   safe_strerror (errno) );
      zlog_err ("shim_sock_init: socket: %s", safe_strerror (save_errno));
      exit(1);
    }
    
  if (shimd_privs.change (ZPRIVS_LOWER))
    {
      zlog_err ("shim_sock_init: could not lower privs, %s",
               safe_strerror (errno) );
    }

  sockopt_reuseaddr (shim_sock);

  u_int off = 0;
  if (setsockopt (shim_sock, IPPROTO_IPV6, IPV6_MULTICAST_LOOP,
                  &off, sizeof (u_int)) < 0)
    zlog_warn ("Network: reset IPV6_MULTICAST_LOOP failed: %s",
               safe_strerror (errno));

  if (setsockopt_ipv6_pktinfo (shim_sock, 1) < 0)
     zlog_warn ("Can't set pktinfo option for fd %d", shim_sock);

  int offset = 12;
#ifndef DISABLE_IPV6_CHECKSUM
  if (setsockopt (shim_sock, IPPROTO_IPV6, IPV6_CHECKSUM,
                  &offset, sizeof (offset)) < 0)
    zlog_warn ("Network: set IPV6_CHECKSUM failed: %s", safe_strerror (errno));
#else
  zlog_warn ("Network: Don't set IPV6_CHECKSUM");
#endif /* DISABLE_IPV6_CHECKSUM */

  inet_pton (AF_INET6, ALLSPFROUTERS6, &allspfrouters6);
  inet_pton (AF_INET6, ALLDROUTERS6, &alldrouters6);

  return shim_sock;
}

void
shim_join_allspfrouters (int ifindex)
{
  struct ipv6_mreq mreq6;
  int retval;
  
  assert(ifindex);
  mreq6.ipv6mr_interface = ifindex;
  memcpy (&mreq6.ipv6mr_multiaddr, &allspfrouters6,
          sizeof (struct in6_addr));

  retval = setsockopt (shim->fd, IPPROTO_IPV6, IPV6_JOIN_GROUP,
                       &mreq6, sizeof (mreq6));

 if (retval < 0)
   zlog_err ("Network: Join AllSPFRouters on ifindex %d failed: %s",
             ifindex, safe_strerror (errno));
}

void
shim_leave_allspfrouters (u_int ifindex)
{
  struct ipv6_mreq mreq6;

  assert (ifindex);
  mreq6.ipv6mr_interface = ifindex;
  memcpy (&mreq6.ipv6mr_multiaddr, &allspfrouters6,
          sizeof (struct in6_addr));

  if (setsockopt (shim->fd, IPPROTO_IPV6, IPV6_LEAVE_GROUP,
                  &mreq6, sizeof (mreq6)) < 0)
    zlog_warn ("Network: Leave AllSPFRouters on ifindex %d Failed: %s",
               ifindex, safe_strerror (errno));
#if 0
  else
    zlog_debug ("Network: Leave AllSPFRouters on ifindex %d", ifindex);
#endif
}

void
shim_join_alldrouters (u_int ifindex)
{
  struct ipv6_mreq mreq6;

  assert (ifindex);
  mreq6.ipv6mr_interface = ifindex;
  memcpy (&mreq6.ipv6mr_multiaddr, &alldrouters6,
          sizeof (struct in6_addr));

  if (setsockopt (shim->fd, IPPROTO_IPV6, IPV6_JOIN_GROUP,
                  &mreq6, sizeof (mreq6)) < 0)
    zlog_warn ("Network: Join AllDRouters on ifindex %d Failed: %s",
               ifindex, safe_strerror (errno));
#if 0
  else
    zlog_debug ("Network: Join AllDRouters on ifindex %d", ifindex);
#endif
}

void
shim_leave_alldrouters (u_int ifindex)
{
  struct ipv6_mreq mreq6;

  assert (ifindex);
  mreq6.ipv6mr_interface = ifindex;
  memcpy (&mreq6.ipv6mr_multiaddr, &alldrouters6,
          sizeof (struct in6_addr));

  if (setsockopt (shim->fd, IPPROTO_IPV6, IPV6_LEAVE_GROUP,
                  &mreq6, sizeof (mreq6)) < 0)
    zlog_warn ("Network: Leave AllDRouters on ifindex %d Failed", ifindex);
#if 0
  else
    zlog_debug ("Network: Leave AllDRouters on ifindex %d", ifindex);
#endif
}

static int 
iov_count (struct iovec *iov)  
{
  int i;
  for (i = 0; iov[i].iov_base; i++)
    ;  
  return i;
}

static int iov_totallen (struct iovec *iov)
{  
  int i;
  int totallen = 0;
  for (i = 0; iov[i].iov_base; i++)    totallen += iov[i].iov_len;
  return totallen;
}

int
shim_sendmsg (struct in6_addr *src, struct in6_addr *dst,
              unsigned int *ifindex, struct iovec *message, 
              unsigned int fd, struct stream * buf, unsigned int len)
{
  int retval;
  struct msghdr smsghdr;
  struct cmsghdr *scmsgp;
  u_char cmsgbuf[CMSG_SPACE(sizeof (struct in6_pktinfo))];
  struct in6_pktinfo *pktinfo;
  struct sockaddr_in6 dst_sin6;

  assert (dst);
  assert (*ifindex);

  scmsgp = (struct cmsghdr *)cmsgbuf;
  pktinfo = (struct in6_pktinfo *)(CMSG_DATA(scmsgp));
  memset (&dst_sin6, 0, sizeof (struct sockaddr_in6));

  /* source address */
  pktinfo->ipi6_ifindex = *ifindex;
  if (src)
    memcpy (&pktinfo->ipi6_addr, src, sizeof (struct in6_addr));
  else
    memset (&pktinfo->ipi6_addr, 0, sizeof (struct in6_addr));

  /* destination address */
  dst_sin6.sin6_family = AF_INET6;
#ifdef SIN6_LEN
  dst_sin6.sin6_len = sizeof (struct sockaddr_in6);
#endif /*SIN6_LEN*/
  memcpy (&dst_sin6.sin6_addr, dst, sizeof (struct in6_addr));
#ifdef HAVE_SIN6_SCOPE_ID
  dst_sin6.sin6_scope_id = *ifindex;
#endif

  /* send control msg */
  scmsgp->cmsg_level = IPPROTO_IPV6;
  scmsgp->cmsg_type = IPV6_PKTINFO;
  scmsgp->cmsg_len = CMSG_LEN (sizeof (struct in6_pktinfo));
  /* scmsgp = CMSG_NXTHDR (&smsghdr, scmsgp); */

  /* send msg hdr */
  memset (&smsghdr, 0, sizeof (smsghdr));
  smsghdr.msg_iov = message;
  smsghdr.msg_iovlen = iov_count (message);
  smsghdr.msg_name = (caddr_t) &dst_sin6;
  smsghdr.msg_namelen = sizeof (struct sockaddr_in6);
  smsghdr.msg_control = (caddr_t) cmsgbuf;
  smsghdr.msg_controllen = sizeof (cmsgbuf);

  retval = stream_sendmsg (buf, fd, &smsghdr, 0, stream_get_size(buf));
  if (retval != iov_totallen (message))
    zlog_warn ("sendmsg failed: ifindex: %d: %s (%d)",
               *ifindex, safe_strerror (errno), errno);

  return retval;
}

int 
shim_recvmsg(struct in6_addr * src, struct in6_addr * dst,
	     unsigned int * ifindex, struct iovec * message, unsigned int fd, struct stream * buf, unsigned int len)
{
  int retval;
  struct msghdr rmsghdr;
  struct cmsghdr *rcmsgp;
  u_char cmsgbuf[CMSG_SPACE(sizeof (struct in6_pktinfo))];
  struct in6_pktinfo *pktinfo;
  struct sockaddr_in6 src_sin6;

  rcmsgp = (struct cmsghdr *)cmsgbuf;
  pktinfo = (struct in6_pktinfo *)(CMSG_DATA(rcmsgp));
  memset (&src_sin6, 0, sizeof (struct sockaddr_in6));

  /* receive control msg */
  rcmsgp->cmsg_level = IPPROTO_IPV6;
  rcmsgp->cmsg_type = IPV6_PKTINFO;
  rcmsgp->cmsg_len = CMSG_LEN (sizeof (struct in6_pktinfo));
  /* rcmsgp = CMSG_NXTHDR (&rmsghdr, rcmsgp); */

  /* receive msg hdr */
  memset (&rmsghdr, 0, sizeof (rmsghdr));
  rmsghdr.msg_iov = message;
  rmsghdr.msg_iovlen = iov_count (message);
  rmsghdr.msg_name = (caddr_t) &src_sin6;
  rmsghdr.msg_namelen = sizeof (struct sockaddr_in6);
  rmsghdr.msg_control = (caddr_t) cmsgbuf;
  rmsghdr.msg_controllen = sizeof (cmsgbuf);

//  retval = recvmsg (fd, &rmsghdr, 0); 
  retval = stream_recvmsg (buf, fd, &rmsghdr, 0, len);
  if (retval < 0)
    zlog_warn ("recvmsg failed: %s", safe_strerror (errno));
  else if (retval == iov_totallen (message))
    zlog_warn ("recvmsg read full buffer size: %d", retval);

  /* source address */
  assert (src);
  memcpy (src, &src_sin6.sin6_addr, sizeof (struct in6_addr));

  /* destination address */
  if (ifindex)
  {
    *ifindex = pktinfo->ipi6_ifindex;
  }
  if (dst)
    memcpy (dst, &pktinfo->ipi6_addr, sizeof (struct in6_addr));

  return retval;
}
