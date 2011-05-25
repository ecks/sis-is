/*
 * SIS-IS Test program.
 * Stephen Sigwart
 * University of Delaware
 */

#ifndef _SISIS_API_H
#define _SISIS_API_H

#define HAVE_IPV6
#define USE_IPV6

#include "sisis_structs.h"
#include <pthread.h>

#define SISIS_VERSION 1

#define SISIS_REREGISTRATION_TIMEOUT			20

// SIS-IS Commands/Messages
#define SISIS_CMD_REGISTER_ADDRESS				1
#define SISIS_CMD_UNREGISTER_ADDRESS			2
#define SISIS_ACK							            3
#define SISIS_NACK							     			4

#ifndef USE_IPV6 /* IPv4 Version */
// Prefix lengths
#define SISIS_ADD_PREFIX_LEN_PTYPE				32
#define SISIS_ADD_PREFIX_LEN_HOST_NUM			64
#endif /* IPv4 Version */

#ifdef USE_IPV6
typedef struct {
	char * name;
	short bits;
	int flags;
#define SISIS_COMPONENT_FIXED					(1 << 0)
	uint64_t fixed_val;		// Fixed values only for if bits <= 64
} sisis_component_t;
#endif /* USE_IPV6 */

extern int sisis_listener_port;
extern char * sisis_listener_ip_addr;

#define AWAITING_ACK_POOL_SIZE 25

#define MAX_REREGISTRATION_THREADS 5

/** Information for reregistration thread */
typedef struct {
	pthread_t thread;
	char * addr;
	short active;
	int idx;
} reregistration_info_t;

// IPv4 & IPv6 RIBs
extern struct list * ipv4_rib_routes;
extern struct list * ipv6_rib_routes;

void sisis_process_message(char * msg, int msg_len);

/**
 * Construct SIS-IS address.
 *
 * sisis_addr String to store resulting SIS-IS/IP address in.
 * 
 * Returns zero on success.
 */
#ifdef USE_IPV6
int sisis_create_addr(char * sisis_addr, ...);
#else /* IPv4 Version */
int sisis_create_addr(unsigned int ptype, unsigned int host_num, unsigned int pid, char * sisis_addr);
#endif /* USE_IPV6 */

/**
 * Split an SIS-IS address into components.
 *
 * sisis_addr SIS-IS/IP address
 */
#ifdef USE_IPV6
int get_sisis_addr_components(char * sisis_addr, ...);
#else /* IPv4 Version */
struct sisis_addr_components get_sisis_addr_components(char * sisis_addr);
#endif /* USE_IPV6 */

/**
 * Registers SIS-IS process.
 *
 * sisis_addr String to store resulting SIS-IS/IP address in.
 * 
 * Returns zero on success.
 */
#ifdef USE_IPV6
int sisis_register(char * sisis_addr, ...);
#else /* IPv4 Version */
int sisis_register(unsigned int ptype, unsigned int host_num, unsigned int pid, char * sisis_addr);
#endif /* USE_IPV6 */

#ifdef USE_IPV6
/**
 * Unregisters SIS-IS process.
 *
 * First parameter is ignored.  Set to NULL
 * 
 * Returns zero on success.
 */
int sisis_unregister(void * nil, ...);
#else /* IPv4 Version */
/**
 * Unregisters SIS-IS process.
 * Returns zero on success.
 */
int sisis_unregister(unsigned int ptype, unsigned int host_num, unsigned int pid);
#endif /* USE_IPV6 */

/**
 * Dump kernel routing table.
 * Returns zero on success.
 */
int sisis_dump_kernel_routes();

#ifdef HAVE_IPV6
/**
 * Dump kernel routing table.
 * Returns zero on success.
 */
int sisis_dump_kernel_ipv6_routes_to_tables(struct list * rib);
#endif

int sisis_rib_add_ipv4 (struct route_ipv4 * route, void * data);
#ifdef HAVE_IPV6
int sisis_rib_add_ipv6 (struct route_ipv6 *, void * data);
#endif /* HAVE_IPV6 */

/** Callback functions when receiving RIB updates. */
struct subscribe_to_rib_changes_info
{
	int (*rib_add_ipv4_route)(struct route_ipv4 *, void *);
	int (*rib_remove_ipv4_route)(struct route_ipv4 *, void *);
	#ifdef HAVE_IPV6
	int (*rib_add_ipv6_route)(struct route_ipv6 *, void *);
	int (*rib_remove_ipv6_route)(struct route_ipv6 *, void *);
	#endif /* HAVE_IPV6 */
	void * data;
	struct sisis_netlink_routing_table_info * subscribe_info;
};

/** Subscribe to route add/remove messages */
int subscribe_to_rib_changes(struct subscribe_to_rib_changes_info * info);

/** Unsubscribe to route add/remove messages */
int unsubscribe_to_rib_changes(struct subscribe_to_rib_changes_info * info);

#ifdef USE_IPV6
/**
 * Get SIS-IS addresses that match a given IP prefix.  It is the receiver's
 * responsibility to free the list when done with it.
 */
struct list * get_sisis_addrs_for_prefix(struct prefix_ipv6 * p);

/**
 * Creates an IPv6 prefix
 */
struct prefix_ipv6 sisis_make_ipv6_prefix(char * addr, int prefix_len);
#else /* IPv4 Version */
/**
 * Get SIS-IS addresses for a specific process type.  It is the receivers
 * responsibility to free the list when done with it.
 */
struct list * get_sisis_addrs_for_process_type(unsigned int ptype);

/**
 * Get SIS-IS addresses for a specific process type and host.  It is the receivers
 * responsibility to free the list when done with it.
 */
struct list * get_sisis_addrs_for_process_type_and_host(unsigned int ptype, unsigned int host_num);
#endif // _SISIS_API_H#endif /* USE_IPV6 */

#endif // _SISIS_API_H