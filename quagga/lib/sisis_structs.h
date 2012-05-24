/*
 * SIS-IS Structures
 * Stephen Sigwart
 * University of Delaware
 *
 * Some structs copied from zebra's prefix.h
 */

#ifndef _SISIS_STRUCTS_H
#define _SISIS_STRUCTS_H

/* Linked list */
struct list_sis
{
	struct listnode_sis * head;
	struct listnode_sis * tail;
	unsigned int size;
};

struct listnode_sis
{
	struct listnode_sis * prev;
	struct listnode_sis * next;
	void * data;
};

#define LIST_APPEND(list,node) { if(!list->head){list->head = list->tail = node;node->prev = node->next = NULL;list->size=1;}else{node->prev=list->tail;node->next=NULL;list->tail->next=node;list->tail=node;list->size++;} }
#define FREE_LINKED_LIST(list) { \
	struct listnode_sis * node = list->head, *tmp;\
	while (node != NULL) {\
		tmp=node->next;\
		free(node->data);\
		free(node);\
		node=tmp;\
	}\
	free(list); }
#define FREE_LINKED_LIST_NOT_DATA(list) { \
	struct listnode_sis * node = list->head, *tmp;\
	while (node != NULL) {\
		tmp=node->next;\
		free(node);\
		node=tmp;\
	}\
	free(list); }
#define LIST_FOREACH(list,node) for(node = list->head; node != NULL; node = node->next)

struct sisis_request_ack_info
{
	short valid;
	unsigned long request_id;
	pthread_mutex_t mutex;
	short flags;
	#define SISIS_REQUEST_ACK_INFO_ACKED				(1<<0)
	#define SISIS_REQUEST_ACK_INFO_NACKED				(1<<1)
};

#ifndef USE_IPV6 /* IPv4 Version */
/* SIS-IS address components */
struct sisis_addr_components
{
        unsigned int ptype;
        unsigned int host_num;
        unsigned int pid;
};
#endif /* IPv4 Version */

/* IPv4 prefix structure. */
//struct prefix_ipv4
//{
//  u_char family;
//  u_char prefixlen;
//  struct in_addr prefix __attribute__ ((aligned (8)));
//};

/* IPv6 prefix structure. */
//#ifdef HAVE_IPV6
//struct prefix_ipv6
//{
//  u_char family;
//  u_char prefixlen;
//  struct in6_addr prefix __attribute__ ((aligned (8)));
//};
//#endif /* HAVE_IPV6 */

/* IPv4 and IPv6 unified prefix structure. */
//struct prefix
//{
//  u_char family;
//  u_char prefixlen;
//  union 
//  {
//    u_char prefix;
//    struct in_addr prefix4;
//#ifdef HAVE_IPV6
//    struct in6_addr prefix6;
//#endif /* HAVE_IPV6 */
//    struct 
//    {
//      struct in_addr id;
//      struct in_addr adv_router;
//    } lp;
//    u_char val[8];
//  } u __attribute__ ((aligned (8)));
//};

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
