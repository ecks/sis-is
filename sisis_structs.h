/*
 * SIS-IS Structures
 * Stephen Sigwart
 * University of Delaware
 *
 * Some structs copied from zebra's prefix.h
 */

#ifndef _SISIS_STRUCTS_H
#define _SISIS_STRUCTS_H

/* IPv4 prefix structure. */
struct prefix_ipv4
{
  u_char family;
  u_char prefixlen;
  struct in_addr prefix __attribute__ ((aligned (8)));
};

/* IPv6 prefix structure. */
#ifdef HAVE_IPV6
struct prefix_ipv6
{
  u_char family;
  u_char prefixlen;
  struct in6_addr prefix __attribute__ ((aligned (8)));
};
#endif /* HAVE_IPV6 */

struct route_ipv4
{
	int type;
	int flags;
	struct prefix_ipv4 *p,
	struct in4_addr *gate;
	struct in_addr *src;
	unsigned int ifindex;
	u_int32_t vrf_id;
	u_int32_t metric;
	u_char distance;
};

#ifdef HAVE_IPV6
struct route_ipv6
{
	int type;
	int flags;
	struct prefix_ipv6 *p,
	struct in6_addr *gate;
	unsigned int ifindex;
	u_int32_t vrf_id;
	u_int32_t metric;
	u_char distance;
};
#endif /* HAVE_IPV6 */

#endif