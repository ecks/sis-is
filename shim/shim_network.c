#include "zebra.h"

#include "sockunion.h"
#include "log.h"
#include "sockopt.h"
#include "privs.h"
#include "prefix.h"

#include "ospfd/ospfd.h"
#include "ospf6d/ospf6_proto.h"
extern struct zebra_privs_t shimd_privs;

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
