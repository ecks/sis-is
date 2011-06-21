#ifndef _ZEBRA_SHIM_PACKET_H
#define _ZEBRA_SHIM_PACKET_H

#define OSPF_MAX_PACKET_SIZE  65535U   /* includes IP Header size. */

extern int shim_read (struct thread *thread);

#endif
