/* ISIS dump routine.
   Copyright (C) 1999 Kunihiro Ishiguro
   Copyright (C) 2011 Stephen Sigwart

This file is part of GNU Zebra.

GNU Zebra is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

GNU Zebra is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU Zebra; see the file COPYING.  If not, write to the Free
Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
02111-1307, USA.  */

#ifndef _QUAGGA_ISIS_DUMP_H
#define _QUAGGA_ISIS_DUMP_H

/* MRT compatible packet dump values.  */
/* type value */
#define MSG_PROTOCOL_ISIS  32

#define ISIS_DUMP_HEADER_SIZE 12

extern struct isis *isis;
extern void isis_dump_init (void);
extern void isis_dump_finish (void);
// TODO: Remove
//extern void isis_dump_state (struct peer *, int, int);
//extern void isis_dump_packet (struct peer *, int, struct stream *);

#endif /* _QUAGGA_ISIS_DUMP_H */
