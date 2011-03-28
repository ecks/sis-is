/* ISIS dump routine
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

#include <zebra.h>

#include "log.h"
#include "stream.h"
#include "sockunion.h"
#include "command.h"
#include "prefix.h"
#include "thread.h"
#include "linklist.h"
#include "if.h"
#include "vty.h"

#include "isisd/dict.h"
#include "isisd/isis_constants.h"
#include "isisd/isis_common.h"
#include "isisd/isis_circuit.h"
#include "isisd/isisd.h"
#include "isisd/isis_tlv.h"
#include "isisd/isis_lsp.h"
#include "isisd/isis_pdu.h"
#include "isisd/isis_dynhn.h"
#include "isisd/isis_misc.h"
#include "isisd/isis_flags.h"
#include "isisd/isis_csm.h"
#include "isisd/isis_adjacency.h"
#include "isisd/isis_spf.h"
#include "isisd/isis_dump.h"

extern struct thread_master *master;
extern struct isis * isis;

enum isis_dump_type
{
  ISIS_DUMP_ALL,
  ISIS_DUMP_UPDATES,
  ISIS_DUMP_ROUTES
};

enum MRT_MSG_TYPES {
   MSG_NULL,
   MSG_START,                   /* sender is starting up */
   MSG_DIE,                     /* receiver should shut down */
   MSG_I_AM_DEAD,               /* sender is shutting down */
   MSG_PEER_DOWN,               /* sender's peer is down */
   MSG_PROTOCOL_BGP,            /* msg is a BGP packet */
   MSG_PROTOCOL_RIP,            /* msg is a RIP packet */
   MSG_PROTOCOL_IDRP,           /* msg is an IDRP packet */
   MSG_PROTOCOL_RIPNG,          /* msg is a RIPNG packet */
   MSG_PROTOCOL_BGP4PLUS,       /* msg is a BGP4+ packet */
   MSG_PROTOCOL_BGP4PLUS_01,    /* msg is a BGP4+ (draft 01) packet */
   MSG_PROTOCOL_OSPF,           /* msg is an OSPF packet */
   MSG_TABLE_DUMP,              /* routing table dump */
   MSG_TABLE_DUMP_V2,           /* routing table dump, version 2 */
	 MSG_PROTOCOL_BGP4MP,         /* msg is a BGP4MP packet */
	 MSG_PROTOCOL_BGP4MP_ET,      /* msg is a BGP4MP packet */
	 MSG_PROTOCOL_ISIS,           /* msg is a ISIS packet */
	 MSG_PROTOCOL_ISIS_ET         /* msg is a ISIS_ET packet */
};

static int isis_dump_interval_func (struct thread *);

struct isis_dump
{
  enum isis_dump_type type;

  char *filename;

  FILE *fp;

  unsigned int interval;

  char *interval_str;

  struct thread *t_interval;
};

/* ISIS packet dump output buffer. */
struct stream *isis_dump_obuf;

/* ISIS dump strucuture for 'dump isis all' */
struct isis_dump isis_dump_all;

/* ISIS dump structure for 'dump isis updates' */
// TODO: struct isis_dump isis_dump_updates;

/* ISIS dump structure for 'dump isis routes' */
// TODO: struct isis_dump isis_dump_routes;

/* Dump whole ISIS table is very heavy process.  */
// TODO: struct thread *t_isis_dump_routes;

/* Some define for ISIS packet dump. */
static FILE *
isis_dump_open_file (struct isis_dump *isis_dump)
{
  int ret;
  time_t clock;
  struct tm *tm;
  char fullpath[MAXPATHLEN];
  char realpath[MAXPATHLEN];
  mode_t oldumask;

  time (&clock);
  tm = localtime (&clock);

  if (isis_dump->filename[0] != DIRECTORY_SEP)
    {
      sprintf (fullpath, "%s/%s", vty_get_cwd (), isis_dump->filename);
      ret = strftime (realpath, MAXPATHLEN, fullpath, tm);
    }
  else
    ret = strftime (realpath, MAXPATHLEN, isis_dump->filename, tm);

  if (ret == 0)
    {
      zlog_warn ("isis_dump_open_file: strftime error");
      return NULL;
    }

  if (isis_dump->fp)
    fclose (isis_dump->fp);


  oldumask = umask(0777 & ~LOGFILE_MASK);
  isis_dump->fp = fopen (realpath, "w");

  if (isis_dump->fp == NULL)
    {
      zlog_warn ("isis_dump_open_file: %s: %s", realpath, strerror (errno));
      umask(oldumask);
      return NULL;
    }
  umask(oldumask);  

  return isis_dump->fp;
}

static int
isis_dump_interval_add (struct isis_dump *isis_dump, int interval)
{
  int secs_into_day;
  time_t t;
  struct tm *tm;

  if (interval > 0)
    {
      /* Periodic dump every interval seconds */
      if ((interval < 86400) && ((86400 % interval) == 0))
	{
	  /* Dump at predictable times: if a day has a whole number of
	   * intervals, dump every interval seconds starting from midnight
	   */
	  (void) time(&t);
	  tm = localtime(&t);
	  secs_into_day = tm->tm_sec + 60*tm->tm_min + 60*60*tm->tm_hour;
	  interval = interval - secs_into_day % interval; /* always > 0 */
	}
      isis_dump->t_interval = thread_add_timer (master, isis_dump_interval_func, 
					       isis_dump, interval);
    }
  else
    {
      /* One-off dump: execute immediately, don't affect any scheduled dumps */
      isis_dump->t_interval = thread_add_event (master, isis_dump_interval_func,
					       isis_dump, 0);
    }

  return 0;
}

/* Dump common header. */
static void
isis_dump_header (struct stream *obuf, int type, int subtype)
{
  time_t now;

  /* Set header. */
  time (&now);

  /* Put dump packet header. */
  stream_putl (obuf, now);	
  stream_putw (obuf, type);
  stream_putw (obuf, subtype);

  stream_putl (obuf, 0);	/* len */
}

static void
isis_dump_set_size (struct stream *s, int type)
{
  stream_putl_at (s, 8, stream_get_endp (s) - ISIS_DUMP_HEADER_SIZE);
}

#if 0
// TODO: Remove
static void
isis_dump_routes_index_table(struct isis *isis)
{
	struct peer *peer;
  struct listnode *node;
  uint16_t peerno = 0;
  struct stream *obuf;

  obuf = isis_dump_obuf;
  stream_reset (obuf);

  /* MRT header */
  isis_dump_header (obuf, MSG_TABLE_DUMP_V2, 0);

  /* Collector ISIS ID */
  stream_put_in_addr (obuf, &isis->router_id);

  /* View name */
  if(isis->name)
    {
      stream_putw (obuf, strlen(isis->name));
      stream_put(obuf, isis->name, strlen(isis->name));
    }
  else
    {
      stream_putw(obuf, 0);
    }

  /* Peer count */
  stream_putw (obuf, listcount(isis->peer));

  /* Walk down all peers */
  for(ALL_LIST_ELEMENTS_RO (isis->peer, node, peer))
    {

      /* Peer's type */
      if (sockunion_family(&peer->su) == AF_INET)
        {
          stream_putc (obuf, TABLE_DUMP_V2_PEER_INDEX_TABLE_AS4+TABLE_DUMP_V2_PEER_INDEX_TABLE_IP);
        }
#ifdef HAVE_IPV6
      else if (sockunion_family(&peer->su) == AF_INET6)
        {
          stream_putc (obuf, TABLE_DUMP_V2_PEER_INDEX_TABLE_AS4+TABLE_DUMP_V2_PEER_INDEX_TABLE_IP6);
        }
#endif /* HAVE_IPV6 */

      /* Peer's ISIS ID */
      stream_put_in_addr (obuf, &peer->remote_id);

      /* Peer's IP address */
      if (sockunion_family(&peer->su) == AF_INET)
        {
          stream_put_in_addr (obuf, &peer->su.sin.sin_addr);
        }
#ifdef HAVE_IPV6
      else if (sockunion_family(&peer->su) == AF_INET6)
        {
          stream_write (obuf, (u_char *)&peer->su.sin6.sin6_addr,
                        IPV6_MAX_BYTELEN);
        }
#endif /* HAVE_IPV6 */

      /* Peer's AS number. */
      /* Note that, as this is an AS4 compliant quagga, the RIB is always AS4 */
      stream_putl (obuf, peer->as);

      /* Store the peer number for this peer */
      peer->table_dump_index = peerno;
      peerno++;
    }

  isis_dump_set_size(obuf, MSG_TABLE_DUMP_V2);

  fwrite (STREAM_DATA (obuf), stream_get_endp (obuf), 1, isis_dump_routes.fp);
  fflush (isis_dump_routes.fp);
}
#endif

// TODO: Fix parameters & caller
/* Runs under child process. */
static unsigned int
isis_dump_routes_func ()
{
	struct stream *obuf;
  
  if (isis_dump_all.fp == NULL)
    return;

  obuf = isis_dump_obuf;
  stream_reset(obuf);
	
	// Go through each area
	struct isis_area *area;
  struct listnode *node, *next_node;

  for (ALL_LIST_ELEMENTS (isis->area_list, node, next_node, area))
	{
		// Testing
		fwrite ("Test1\n", 6, 1, isis_dump_all.fp);
		fflush (isis_dump_all.fp);
		
		// Go through all levels
		int level;
		for (level = 0; level < ISIS_LEVELS; level++)
		{
			// Testing
			fwrite ("Test2\n", 6, 1, isis_dump_all.fp);
			fflush (isis_dump_all.fp);
			
			dnode_t *node = dict_first(area->lspdb[level]), *next;
			while (node != NULL)
			{
				// Testing
				fwrite ("Test3\n", 6, 1, isis_dump_all.fp);
				fflush (isis_dump_all.fp);
				
				// Reset stream
				stream_reset(obuf);
				
				// Get next
				next = dict_next (area->lspdb[level], node);
				
				// Testing
				fwrite ("Test4\n", 6, 1, isis_dump_all.fp);
				fflush (isis_dump_all.fp);
				
				// Get LSP
				struct isis_lsp *lsp = dnode_get (node);
				if (lsp != NULL && lsp->pdu != NULL)
				{
					// Testing
					fwrite ("Test4.1\n", 8, 1, isis_dump_all.fp);
					fflush (isis_dump_all.fp);
					
					// Duplicate stream
					struct stream * dup_lsp = stream_dup(lsp->pdu);
					// Testing
					fwrite ("Test4.3\n", 8, 1, isis_dump_all.fp);
					fflush (isis_dump_all.fp);
					
					// Get stream size
					size_t pdu_size = stream_get_endp(dup_lsp);
					char tmp[64];
					sprintf(tmp, "%lu\n", (unsigned long)stream_get_getp(dup_lsp));
					fwrite (tmp, strlen(tmp), 1, isis_dump_all.fp);
					fflush (isis_dump_all.fp);
					stream_reset(dup_lsp);
					if (pdu_size > 0)
					{
						u_char * dup_buf = malloc(sizeof(u_char) * pdu_size);
						/*
						sprintf(tmp, "%x\t%x\n", dup_buf, dup_lsp->data);
						fwrite (tmp, strlen(tmp), 1, isis_dump_all.fp);
						fflush (isis_dump_all.fp);
						
						stream_get((void *)dup_buf, dup_lsp, pdu_size);
						// Testing
						fwrite ("Test4.4\n", 8, 1, isis_dump_all.fp);
						fflush (isis_dump_all.fp);
						*/
						
						stream_put(obuf, (void *)dup_lsp->data, pdu_size);
						stream_free(dup_lsp);
						free(dup_buf);
						
						// Testing
						fwrite ("Test4.6\n", 8, 1, isis_dump_all.fp);
						fflush (isis_dump_all.fp);
						
						// Print MRT header
						isis_dump_header (obuf, MSG_PROTOCOL_ISIS, 0);
						
						// Set size and write
						isis_dump_set_size(obuf, 0);
						fwrite (STREAM_DATA (obuf), stream_get_endp (obuf), 1, isis_dump_all.fp);
					}
				}
				
				// Testing
				fwrite ("Test5\n", 6, 1, isis_dump_all.fp);
				fflush (isis_dump_all.fp);
				
				// Switch to next
				node = next;
			}
		}
		
		// Testing
		fwrite ("Test6\n", 6, 1, isis_dump_all.fp);
		fflush (isis_dump_all.fp);
	}
	
	// Flush file
  fflush (isis_dump_all.fp);
}

static int
isis_dump_interval_func (struct thread *t)
{
  struct isis_dump *isis_dump;
  isis_dump = THREAD_ARG (t);
  isis_dump->t_interval = NULL;

  /* Reschedule dump even if file couldn't be opened this time... */
  if (isis_dump_open_file (isis_dump) != NULL)
    {
      /* In case of isis_dump_routes, we need special route dump function. */
      if (isis_dump->type == ISIS_DUMP_ALL)
	{
	  isis_dump_routes_func ();
	  /* Close the file now. For a RIB dump there's no point in leaving
	   * it open until the next scheduled dump starts. */
	  fclose(isis_dump->fp); isis_dump->fp = NULL;
	}
    }

  /* if interval is set reschedule */
  if (isis_dump->interval > 0)
    isis_dump_interval_add (isis_dump, isis_dump->interval);

  return 0;
}

// TODO: Remove
#if 0
/* Dump common information. */
static void
isis_dump_common (struct stream *obuf, struct peer *peer, int forceas4)
{
  char empty[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

  /* Source AS number and Destination AS number. */
  if (forceas4 || CHECK_FLAG (peer->cap, PEER_CAP_AS4_RCV) )
    {
      stream_putl (obuf, peer->as);
      stream_putl (obuf, peer->local_as);
    }
  else
    {
      stream_putw (obuf, peer->as);
      stream_putw (obuf, peer->local_as);
    }

  if (peer->su.sa.sa_family == AF_INET)
    {
      stream_putw (obuf, peer->ifindex);
      stream_putw (obuf, AFI_IP);

      stream_put (obuf, &peer->su.sin.sin_addr, IPV4_MAX_BYTELEN);

      if (peer->su_local)
	stream_put (obuf, &peer->su_local->sin.sin_addr, IPV4_MAX_BYTELEN);
      else
	stream_put (obuf, empty, IPV4_MAX_BYTELEN);
    }
#ifdef HAVE_IPV6
  else if (peer->su.sa.sa_family == AF_INET6)
    {
      /* Interface Index and Address family. */
      stream_putw (obuf, peer->ifindex);
      stream_putw (obuf, AFI_IP6);

      /* Source IP Address and Destination IP Address. */
      stream_put (obuf, &peer->su.sin6.sin6_addr, IPV6_MAX_BYTELEN);

      if (peer->su_local)
	stream_put (obuf, &peer->su_local->sin6.sin6_addr, IPV6_MAX_BYTELEN);
      else
	stream_put (obuf, empty, IPV6_MAX_BYTELEN);
    }
#endif /* HAVE_IPV6 */
}

/* Dump ISIS status change. */
void
isis_dump_state (struct peer *peer, int status_old, int status_new)
{
  struct stream *obuf;

  /* If dump file pointer is disabled return immediately. */
  if (isis_dump_all.fp == NULL)
    return;

  /* Make dump stream. */
  obuf = isis_dump_obuf;
  stream_reset (obuf);

  isis_dump_header (obuf, MSG_PROTOCOL_ISIS4MP, ISIS4MP_STATE_CHANGE_AS4);
  isis_dump_common (obuf, peer, 1);/* force this in as4speak*/

  stream_putw (obuf, status_old);
  stream_putw (obuf, status_new);

  /* Set length. */
  isis_dump_set_size (obuf, MSG_PROTOCOL_ISIS4MP);

  /* Write to the stream. */
  fwrite (STREAM_DATA (obuf), stream_get_endp (obuf), 1, isis_dump_all.fp);
  fflush (isis_dump_all.fp);
}

static void
isis_dump_packet_func (struct isis_dump *isis_dump, struct peer *peer,
		      struct stream *packet)
{
  struct stream *obuf;

  /* If dump file pointer is disabled return immediately. */
  if (isis_dump->fp == NULL)
    return;

  /* Make dump stream. */
  obuf = isis_dump_obuf;
  stream_reset (obuf);

  /* Dump header and common part. */
  if (CHECK_FLAG (peer->cap, PEER_CAP_AS4_RCV) )
    { 
      isis_dump_header (obuf, MSG_PROTOCOL_ISIS4MP, ISIS4MP_MESSAGE_AS4);
    }
  else
    {
      isis_dump_header (obuf, MSG_PROTOCOL_ISIS4MP, ISIS4MP_MESSAGE);
    }
  isis_dump_common (obuf, peer, 0);

  /* Packet contents. */
  stream_put (obuf, STREAM_DATA (packet), stream_get_endp (packet));
  
  /* Set length. */
  isis_dump_set_size (obuf, MSG_PROTOCOL_ISIS4MP);

  /* Write to the stream. */
  fwrite (STREAM_DATA (obuf), stream_get_endp (obuf), 1, isis_dump->fp);
  fflush (isis_dump->fp);
}

/* Called from isis_packet.c when ISIS packet is received. */
void
isis_dump_packet (struct peer *peer, int type, struct stream *packet)
{
  /* isis_dump_all. */
  isis_dump_packet_func (&isis_dump_all, peer, packet);

  /* isis_dump_updates. */
  if (type == ISIS_MSG_UPDATE)
    isis_dump_packet_func (&isis_dump_updates, peer, packet);
}

#endif

static unsigned int
isis_dump_parse_time (const char *str)
{
  int i;
  int len;
  int seen_h;
  int seen_m;
  int time;
  unsigned int total;

  time = 0;
  total = 0;
  seen_h = 0;
  seen_m = 0;
  len = strlen (str);

  for (i = 0; i < len; i++)
    {
      if (isdigit ((int) str[i]))
	{
	  time *= 10;
	  time += str[i] - '0';
	}
      else if (str[i] == 'H' || str[i] == 'h')
	{
	  if (seen_h)
	    return 0;
	  if (seen_m)
	    return 0;
	  total += time * 60 *60;
	  time = 0;
	  seen_h = 1;
	}
      else if (str[i] == 'M' || str[i] == 'm')
	{
	  if (seen_m)
	    return 0;
	  total += time * 60;
	  time = 0;
	  seen_h = 1;
	}
      else
	return 0;
    }
  return total + time;
}

static int
isis_dump_set (struct vty *vty, struct isis_dump *isis_dump,
              enum isis_dump_type type, const char *path,
              const char *interval_str)
{
  unsigned int interval;
  
  if (interval_str)
    {
      
      /* Check interval string. */
      interval = isis_dump_parse_time (interval_str);
      if (interval == 0)
	{
	  vty_out (vty, "Malformed interval string%s", VTY_NEWLINE);
	  return CMD_WARNING;
	}

      /* Don't schedule duplicate dumps if the dump command is given twice */
      if (interval == isis_dump->interval &&
	  type == isis_dump->type &&
          path && isis_dump->filename && !strcmp (path, isis_dump->filename))
	{
          return CMD_SUCCESS;
	}

      /* Set interval. */
      isis_dump->interval = interval;
      if (isis_dump->interval_str)
	free (isis_dump->interval_str);
      isis_dump->interval_str = strdup (interval_str);
      
    }
  else
    {
      interval = 0;
    }
    
  /* Create interval thread. */
  isis_dump_interval_add (isis_dump, interval);

  /* Set type. */
  isis_dump->type = type;

  /* Set file name. */
  if (isis_dump->filename)
    free (isis_dump->filename);
  isis_dump->filename = strdup (path);

  /* This should be called when interval is expired. */
  isis_dump_open_file (isis_dump);

  return CMD_SUCCESS;
}

static int
isis_dump_unset (struct vty *vty, struct isis_dump *isis_dump)
{
  /* Set file name. */
  if (isis_dump->filename)
    {
      free (isis_dump->filename);
      isis_dump->filename = NULL;
    }

  /* This should be called when interval is expired. */
  if (isis_dump->fp)
    {
      fclose (isis_dump->fp);
      isis_dump->fp = NULL;
    }

  /* Create interval thread. */
  if (isis_dump->t_interval)
    {
      thread_cancel (isis_dump->t_interval);
      isis_dump->t_interval = NULL;
    }

  isis_dump->interval = 0;

  if (isis_dump->interval_str)
    {
      free (isis_dump->interval_str);
      isis_dump->interval_str = NULL;
    }
  

  return CMD_SUCCESS;
}

DEFUN (dump_isis_all,
       dump_isis_all_cmd,
       "dump isis all PATH",
       "Dump packet\n"
       "ISIS packet dump\n"
       "Dump all ISIS packets\n"
       "Output filename\n")
{
  return isis_dump_set (vty, &isis_dump_all, ISIS_DUMP_ALL, argv[0], NULL);
}

DEFUN (dump_isis_all_interval,
       dump_isis_all_interval_cmd,
       "dump isis all PATH INTERVAL",
       "Dump packet\n"
       "ISIS packet dump\n"
       "Dump all ISIS packets\n"
       "Output filename\n"
       "Interval of output\n")
{
  return isis_dump_set (vty, &isis_dump_all, ISIS_DUMP_ALL, argv[0], argv[1]);
}

DEFUN (no_dump_isis_all,
       no_dump_isis_all_cmd,
       "no dump isis all [PATH] [INTERVAL]",
       NO_STR
       "Dump packet\n"
       "ISIS packet dump\n"
       "Dump all ISIS packets\n")
{
  return isis_dump_unset (vty, &isis_dump_all);
}

/* TODO
DEFUN (dump_isis_updates,
       dump_isis_updates_cmd,
       "dump isis updates PATH",
       "Dump packet\n"
       "ISIS packet dump\n"
       "Dump ISIS updates only\n"
       "Output filename\n")
{
  return isis_dump_set (vty, &isis_dump_updates, ISIS_DUMP_UPDATES, argv[0], NULL);
}

DEFUN (dump_isis_updates_interval,
       dump_isis_updates_interval_cmd,
       "dump isis updates PATH INTERVAL",
       "Dump packet\n"
       "ISIS packet dump\n"
       "Dump ISIS updates only\n"
       "Output filename\n"
       "Interval of output\n")
{
  return isis_dump_set (vty, &isis_dump_updates, ISIS_DUMP_UPDATES, argv[0], argv[1]);
}

DEFUN (no_dump_isis_updates,
       no_dump_isis_updates_cmd,
       "no dump isis updates [PATH] [INTERVAL]",
       NO_STR
       "Dump packet\n"
       "ISIS packet dump\n"
       "Dump ISIS updates only\n")
{
  return isis_dump_unset (vty, &isis_dump_updates);
}

DEFUN (dump_isis_routes,
       dump_isis_routes_cmd,
       "dump isis routes-mrt PATH",
       "Dump packet\n"
       "ISIS packet dump\n"
       "Dump whole ISIS routing table\n"
       "Output filename\n")
{
  return isis_dump_set (vty, &isis_dump_routes, ISIS_DUMP_ROUTES, argv[0], NULL);
}

DEFUN (dump_isis_routes_interval,
       dump_isis_routes_interval_cmd,
       "dump isis routes-mrt PATH INTERVAL",
       "Dump packet\n"
       "ISIS packet dump\n"
       "Dump whole ISIS routing table\n"
       "Output filename\n"
       "Interval of output\n")
{
  return isis_dump_set (vty, &isis_dump_routes, ISIS_DUMP_ROUTES, argv[0], argv[1]);
}

DEFUN (no_dump_isis_routes,
       no_dump_isis_routes_cmd,
       "no dump isis routes-mrt [PATH] [INTERVAL]",
       NO_STR
       "Dump packet\n"
       "ISIS packet dump\n"
       "Dump whole ISIS routing table\n")
{
  return isis_dump_unset (vty, &isis_dump_routes);
}
*/

/* ISIS node structure. */
static struct cmd_node isis_dump_node =
{
  DUMP_NODE,
  "",
  1
};

static int
config_write_isis_dump (struct vty *vty)
{
  if (isis_dump_all.filename)
    {
      if (isis_dump_all.interval_str)
	vty_out (vty, "dump isis all %s %s%s", 
		 isis_dump_all.filename, isis_dump_all.interval_str,
		 VTY_NEWLINE);
      else
	vty_out (vty, "dump isis all %s%s", 
		 isis_dump_all.filename, VTY_NEWLINE);
    }
		/* TODO
  if (isis_dump_updates.filename)
    {
      if (isis_dump_updates.interval_str)
	vty_out (vty, "dump isis updates %s %s%s", 
		 isis_dump_updates.filename, isis_dump_updates.interval_str,
		 VTY_NEWLINE);
      else
	vty_out (vty, "dump isis updates %s%s", 
		 isis_dump_updates.filename, VTY_NEWLINE);
    }
  if (isis_dump_routes.filename)
    {
      if (isis_dump_routes.interval_str)
	vty_out (vty, "dump isis routes-mrt %s %s%s", 
		 isis_dump_routes.filename, isis_dump_routes.interval_str,
		 VTY_NEWLINE);
      else
	vty_out (vty, "dump isis routes-mrt %s%s", 
		 isis_dump_routes.filename, VTY_NEWLINE);
    }
		*/
  return 0;
}

/* Initialize ISIS packet dump functionality. */
void
isis_dump_init (void)
{
  memset (&isis_dump_all, 0, sizeof (struct isis_dump));
	// TODO: Remove
  //memset (&isis_dump_updates, 0, sizeof (struct isis_dump));
  //memset (&isis_dump_routes, 0, sizeof (struct isis_dump));
	
	isis_dump_obuf = stream_new (RECEIVE_LSP_BUFFER_SIZE + ISIS_DUMP_HEADER_SIZE);

  install_node (&isis_dump_node, config_write_isis_dump);

  install_element (CONFIG_NODE, &dump_isis_all_cmd);
	install_element (CONFIG_NODE, &dump_isis_all_interval_cmd);
  install_element (CONFIG_NODE, &no_dump_isis_all_cmd);
	/* TODO
  install_element (CONFIG_NODE, &dump_isis_updates_cmd);
  install_element (CONFIG_NODE, &dump_isis_updates_interval_cmd);
  install_element (CONFIG_NODE, &no_dump_isis_updates_cmd);
  install_element (CONFIG_NODE, &dump_isis_routes_cmd);
  install_element (CONFIG_NODE, &dump_isis_routes_interval_cmd);
  install_element (CONFIG_NODE, &no_dump_isis_routes_cmd);
	*/
}

void
isis_dump_finish (void)
{
  stream_free (isis_dump_obuf);
  isis_dump_obuf = NULL;
}
