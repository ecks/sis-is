/*
 * SIS-IS Rout(e)ing protocol - sisisd_zebra.c
 *
 * Copyright (C) 2010,2011   Stephen Sigwart
 *                           University of Delaware
 *
 * This program is free software; you can redistribute it and/or modify it 
 * under the terms of the GNU General Public Licenseas published by the Free 
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,but WITHOUT 
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or 
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for 
 * more details.

 * You should have received a copy of the GNU General Public License along 
 * with this program; if not, write to the Free Software Foundation, Inc., 
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <zebra.h>

#include "prefix.h"
#include "zebra/interface.h"
#include "zclient.h"

#include "sisisd/sisisd.h"

/* All information about zebra. */
struct zclient *zclient = NULL;

void sisis_zebra_init (void)
{
  /* Set default values. */
  zclient = zclient_new ();
  zclient_init (zclient, ZEBRA_ROUTE_BGP);
  zclient->router_id_update = NULL;
  zclient->interface_add = NULL;
  zclient->interface_delete = NULL;
  zclient->interface_address_add = NULL;
  zclient->interface_address_delete = NULL;
  zclient->ipv4_route_add = NULL;
  zclient->ipv4_route_delete = NULL;
  zclient->interface_up = NULL;
  zclient->interface_down = NULL;
#ifdef HAVE_IPV6
  zclient->ipv6_route_add = NULL;
  zclient->ipv6_route_delete = NULL;
#endif /* HAVE_IPV6 */

  /* Interface related init. */
  // TODO: if_init ();
}
