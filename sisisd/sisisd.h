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
#define SISIS_MESSAGE_VERSION 1
#define SISIS_PORT_DEFAULT 54345
#define SISIS_ADDRESS_TIMEOUT 30

/* Default configuration settings for sisisd.  */
#define SISIS_DEFAULT_CONFIG             "sisisd.conf"

// SIS-IS Commands/Messages
#define SISIS_CMD_REGISTER_ADDRESS				1
#define SISIS_CMD_UNREGISTER_ADDRESS			2
#define SISIS_ACK							            3
#define SISIS_NACK							     			4

struct sisis_info
{ 
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

extern struct sisis_info * sisis_info;

void sisis_init (void);
time_t sisis_clock (void);
void sisis_master_init (void);
void sisis_terminate (void);

// Duplicated in sisis_api.c
int sisis_construct_message(char ** buf, unsigned short version, unsigned int request_id, unsigned short cmd, void * data, unsigned short data_len);
void sisis_process_message(char * msg, int msg_len, int sock, struct sockaddr * from, socklen_t from_len);

int sisis_rib_add_ipv4 (int type, int flags, struct prefix_ipv4 *p, 
	      struct in_addr *gate, struct in_addr *src,
	      unsigned int ifindex, u_int32_t vrf_id,
	      u_int32_t metric, u_char distance);
int sisis_rib_add_ipv6 (int type, int flags, struct prefix_ipv6 *p,
	      struct in6_addr *gate, unsigned int ifindex, u_int32_t vrf_id,
	      u_int32_t metric, u_char distance);

extern int sisis_socket (unsigned short, const char *);

#endif /* SISISD_H */