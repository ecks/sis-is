/*
 * SIS-IS Rout(e)ing protocol - sisisd.c
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

#include "sisisd/sisisd.h"

struct sisis_info * sisis_info;

void sisis_init ()
{
  /* Init zebra. */
  isis_zebra_init ();
	
	/* NOTES:
	  zapi_ipv4_route (ZEBRA_IPV4_ROUTE_ADD, zclient, (struct prefix_ipv4 *) p, &api);
	  zapi_ipv4_route (ZEBRA_IPV4_ROUTE_DELETE, zclient, (struct prefix_ipv4 *) p, &api);
	*/
}

/* time_t value that is monotonicly increasing
 * and uneffected by adjustments to system clock
 */
time_t sisis_clock (void)
{
  struct timeval tv;

  quagga_gettime(QUAGGA_CLK_MONOTONIC, &tv);
  return tv.tv_sec;
}

void isis_master_init (void)
{
  memset (&sisis_info, 0, sizeof (struct sisis_info));

  sisis_info = &sisis_info;
  sisis_info->sisis_addrs = list_new();
  sisis_info->listen_sockets = list_new();
  sisis_info->port = SISIS_PORT_DEFAULT;
  sisis_info->master = thread_master_create();
  sisis_info->start_time = sisis_clock();
}
