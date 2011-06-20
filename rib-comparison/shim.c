#include "zebra.h"

#include "prefix.h"
#include "if.h"
#include "sockunion.h"
#include "log.h"
#include "sockopt.h"

#include "ospfd/ospfd.h"

int
main (void)
{
  int ospf_sock;
  int ret, hincl = 1;

//  if ( ospfd_privs.change (ZPRIVS_RAISE) )
//    zlog_err ("ospf_sock_init: could not raise privs, %s",
//               safe_strerror (errno) );
    
  ospf_sock = socket (AF_INET, SOCK_RAW, IPPROTO_OSPFIGP);
  if (ospf_sock < 0)
    {
      int save_errno = errno;
//      if ( ospfd_privs.change (ZPRIVS_LOWER) )
//        zlog_err ("ospf_sock_init: could not lower privs, %s",
//                   safe_strerror (errno) );
//      zlog_err ("ospf_read_sock_init: socket: %s", safe_strerror (save_errno));
      exit(1);
    }
    
#ifdef IP_HDRINCL
  /* we will include IP header with packet */
  ret = setsockopt (ospf_sock, IPPROTO_IP, IP_HDRINCL, &hincl, sizeof (hincl));
  if (ret < 0)
    {
      int save_errno = errno;
//      if ( ospfd_privs.change (ZPRIVS_LOWER) )
//        zlog_err ("ospf_sock_init: could not lower privs, %s",
//                   safe_strerror (errno) );
//      zlog_warn ("Can't set IP_HDRINCL option for fd %d: %s",
//      		 ospf_sock, safe_strerror(save_errno));
    }
#elif defined (IPTOS_PREC_INTERNETCONTROL)
#warning "IP_HDRINCL not available on this system"
#warning "using IPTOS_PREC_INTERNETCONTROL"
  ret = setsockopt_ipv4_tos(ospf_sock, IPTOS_PREC_INTERNETCONTROL);
  if (ret < 0)
    {
      int save_errno = errno;
//      if ( ospfd_privs.change (ZPRIVS_LOWER) )
//        zlog_err ("ospf_sock_init: could not lower privs, %s",
//                   safe_strerror (errno) );
//      zlog_warn ("can't set sockopt IP_TOS %d to socket %d: %s",
//      		 tos, ospf_sock, safe_strerror(save_errno));
      close (ospf_sock);	/* Prevent sd leak. */
      return ret;
    }
#else /* !IPTOS_PREC_INTERNETCONTROL */
#warning "IP_HDRINCL not available, nor is IPTOS_PREC_INTERNETCONTROL"
//  zlog_warn ("IP_HDRINCL option not available");
#endif /* IP_HDRINCL */

//  ret = setsockopt_ifindex (AF_INET, ospf_sock, 1);

//  if (ret < 0)
//     zlog_warn ("Can't set pktinfo option for fd %d", ospf_sock);

//  if (ospfd_privs.change (ZPRIVS_LOWER))
//    {
//      zlog_err ("ospf_sock_init: could not lower privs, %s",
//               safe_strerror (errno) );
//    }
 
  return ospf_sock;
}
