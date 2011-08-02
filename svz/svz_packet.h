#ifndef SV_PACKET_H
#define SV_PACKET_H

#define OSPF_MAX_PACKET_SIZE  65535U   /* includes IP Header size. */

extern void shim_hello_print (struct ospf6_header * oh);
extern void shim_dbdesc_print (struct ospf6_header * oh);
extern void shim_lsreq_print (struct ospf6_header *oh);
extern void shim_lsupdate_print (struct ospf6_header *oh);
extern int shim_iobuf_size (unsigned int size);

extern int shim_receive (struct tclient * tclient);

//extern int shim_hello_send (struct stream * s, struct shim_interface * si);
extern void shim_send(struct in6_addr * src, struct in6_addr * dst, 
	              struct shim_interface * si, struct stream * ibuf, uint16_t length);
#endif
