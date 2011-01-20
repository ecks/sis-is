/*
 * SIS-IS Rout(e)ing protocol - sisisd.h   
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

#ifndef SISISD_H
#define SISISD_H

#define SISISD_VERSION "1.0"
//#define SISIS_REGISTRATION_PORT 54345
#define SISIS_PORT_DEFAULT 54345

/* Default configuration settings for sisisd.  */
#define SISIS_DEFAULT_CONFIG             "sisisd.conf"

struct sisis_info
{
  /* SIS-IS Addresses */
  struct list *sisis_addrs;
  
  /* SIS-IS thread master.  */
  struct thread_master *master;
  
  /* work queues */
  struct work_queue *process_main_queue;
  struct work_queue *process_rsclient_queue;
  
  /* Listening sockets */
  struct list *listen_sockets;
  
  /* SIS-IS port number.  */
  u_int16_t port;

  /* Listener address */
  char *address;

  /* SIS-IS start time.  */
  time_t start_time;
};

/* SIS-IS address structure.  */
struct sisis_addr
{
  struct prefix p;
  time_t expires;
};

static struct sisis_info * sisis_info;

void sisis_init (void);
time_t sisis_clock (void);
void sisis_master_init (void);
void sisis_terminate (void);

#endif /* SISISD_H */