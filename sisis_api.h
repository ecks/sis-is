/*
 * SIS-IS Test program.
 * Stephen Sigwart
 * University of Delaware
 */

#ifndef _SISIS_API_H
#define _SISIS_API_H

#define HAVE_IPV6

#include "sisis_structs.h"

#define SISIS_VERSION 1

#define SISIS_REREGISTRATION_TIMEOUT			20

// SIS-IS Commands/Messages
#define SISIS_CMD_REGISTER_ADDRESS				1
#define SISIS_CMD_UNREGISTER_ADDRESS			2
#define SISIS_ACK							            3
#define SISIS_NACK							     			4

// Prefix lengths
#define SISIS_ADD_PREFIX_LEN_PTYPE				16
#define SISIS_ADD_PREFIX_LEN_HOST_NUM			24

extern int sisis_listener_port;
extern char * sisis_listener_ip_addr;

// IPv4 Rib
extern struct list * ipv4_rib_routes;

void sisis_process_message(char * msg, int msg_len);

/**
 * Construct SIS-IS address.
 *
 * sisis_addr String to store resulting SIS-IS/IP address in.
 * 
 * Returns zero on success.
 */
int sisis_create_addr(uint16_t ptype, uint32_t host_num, uint64_t pid, char * sisis_addr);

/**
 * Split an SIS-IS address into components.
 *
 * sisis_addr SIS-IS/IP address
 */
struct sisis_addr_components get_sisis_addr_components(char * sisis_addr);

/**
 * Registers SIS-IS process.
 *
 * sisis_addr String to store resulting SIS-IS/IP address in.
 * 
 * Returns zero on success.
 */
int sisis_register(uint16_t ptype, uint32_t host_num, uint64_t pid, char * sisis_addr);

/**
 * Unregisters SIS-IS process.
 * Returns zero on success.
 */
int sisis_unregister(uint16_t ptype, uint32_t host_num, uint64_t pid);

/**
 * Dump kernel routing table.
 * Returns zero on success.
 */
int sisis_dump_kernel_routes();
int sisis_rib_add_ipv4(struct route_ipv4 *);
#ifdef HAVE_IPV6
int sisis_rib_add_ipv6(struct route_ipv6 *);
#endif /* HAVE_IPV6 */

/** Callback functions when receiving RIB updates. */
struct subscribe_to_rib_changes_info
{
	int (*rib_add_ipv4_route)(struct route_ipv4 *);
	int (*rib_remove_ipv4_route)(struct route_ipv4 *);
	#ifdef HAVE_IPV6
	int (*rib_add_ipv6_route)(struct route_ipv6 *);
	int (*rib_remove_ipv6_route)(struct route_ipv6 *);
	#endif /* HAVE_IPV6 */
};

/** Subscribe to route add/remove messages */
int subscribe_to_rib_changes(struct subscribe_to_rib_changes_info * info);

/**
 * Get SIS-IS addresses for a specific process type.  It is the receivers
 * responsibility to free the list when done with it.
 */
struct list * get_sisis_addrs_for_process_type(uint16_t ptype);

/**
 * Get SIS-IS addresses for a specific process type and host.  It is the receivers
 * responsibility to free the list when done with it.
 */
struct list * get_sisis_addrs_for_process_type_and_host(uint16_t ptype, uint32_t host_num);

#endif // _SISIS_API_H