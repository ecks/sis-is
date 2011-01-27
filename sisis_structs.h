/*
 * SIS-IS Structures
 * Stephen Sigwart
 * University of Delaware
 *
 * Some structs copied from zebra's prefix.h
 */

#ifndef _SISIS_STRUCTS_H
#define _SISIS_STRUCTS_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>

/* Linked list */
/*
struct list
{
	struct listnode * head;
}

struct listnode
{
	struct listnode * prev;
	struct listnode * next;
	void * data;
}
*/

struct sisis_request_ack_info
{
	unsigned long request_id;
	pthread_mutex_t * mutex;
	short flags;
	#define SISIS_REQUEST_ACK_INFO_ACKED				1
	#define SISIS_REQUEST_ACK_INFO_NACKED				2
};

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
	struct prefix_ipv4 *p;
	struct in_addr *gate;
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
	struct prefix_ipv6 *p;
	struct in_addr *gate;
	unsigned int ifindex;
	u_int32_t vrf_id;
	u_int32_t metric;
	u_char distance;
};
#endif /* HAVE_IPV6 */

#endif