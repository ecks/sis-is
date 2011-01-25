/*
 * SIS-IS Rout(e)ing protocol - sisis_main.c
 *
 * Copyright (C) 2010,2011   Stephen Sigwart
 *                           University of Delaware
 * Based on bgpd_main.c
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

#include "vector.h"
// TODO: Remove
// #include "vty.h"
#include "command.h"
#include "getopt.h"
#include "thread.h"
#include <lib/version.h>
#include "memory.h"
#include "prefix.h"
#include "log.h"
#include "privs.h"
#include "sigevent.h"
#include "zclient.h"
#include "routemap.h"
#include "filter.h"
#include "plist.h"

#include "sisisd/sisisd.h"

/* sisis options, we use GNU getopt library. */
static const struct option longopts[] = 
{
  { "daemon",      no_argument,       NULL, 'd'},
  { "config_file", required_argument, NULL, 'f'},
  { "pid_file",    required_argument, NULL, 'i'},
  { "sisis_port",  required_argument, NULL, 'p'},
  { "listenon",    required_argument, NULL, 'l'},
  { "retain",      no_argument,       NULL, 'r'},
  { "user",        required_argument, NULL, 'u'},
  { "group",       required_argument, NULL, 'g'},
  { "version",     no_argument,       NULL, 'v'},
  { "help",        no_argument,       NULL, 'h'},
  { 0 }
};

/* signal definitions */
void sighup (void);
void sigint (void);
void sigusr1 (void);

static void sisis_exit (int);

static struct quagga_signal_t sisis_signals[] = 
{
  { 
    .signal = SIGHUP, 
    .handler = &sighup,
  },
  {
    .signal = SIGUSR1,
    .handler = &sigusr1,
  },
  {
    .signal = SIGINT,
    .handler = &sigint,
  },
  {
    .signal = SIGTERM,
    .handler = &sigint,
  },
};

/* Configuration file and directory. */
char config_default[] = SYSCONFDIR SISIS_DEFAULT_CONFIG;

/* Route retain mode flag. */
static int retain_mode = 0;

/* Master of threads. */
struct thread_master *master;

/* Manually specified configuration file name.  */
char *config_file = NULL;

/* Process ID saved for use by init system */
static const char *pid_file = PATH_SISISD_PID;

/* privileges */
static zebra_capabilities_t _caps_p [] =  
{
    ZCAP_BIND, 
    ZCAP_NET_RAW,
};

struct zebra_privs_t sisisd_privs =
{
#if defined(QUAGGA_USER) && defined(QUAGGA_GROUP)
  .user = QUAGGA_USER,
  .group = QUAGGA_GROUP,
#endif
#ifdef VTY_GROUP
  .vty_group = VTY_GROUP,
#endif
  .caps_p = _caps_p,
  .cap_num_p = sizeof(_caps_p)/sizeof(_caps_p[0]),
  .cap_num_i = 0,
};

/* Help information display. */
static void usage (char *progname, int status)
{
  if (status != 0)
    fprintf (stderr, "Try `%s --help' for more information.\n", progname);
  else
    {    
      printf ("Usage : %s [OPTION...]\n\n\
Daemon which manages kernel routing table management and \
redistribution between different routing protocols.\n\n\
-d, --daemon       Runs in daemon mode\n\
-f, --config_file  Set configuration file name\n\
-i, --pid_file     Set process identifier file name\n\
-p, --sisis_port   Set sisis protocol's port number\n\
-l, --listenon     Listen on specified address (implies -n)\n\
-r, --retain       When program terminates, retain added route by sisisd.\n\
-n, --no_kernel    Do not install route to kernel.\n\
-u, --user         User to run as\n\
-g, --group        Group to run as\n\
-v, --version      Print program version\n\
-h, --help         Display this help and exit\n\
\n\
Report bugs to %s\n", progname, ZEBRA_BUG_ADDRESS);
    }

  exit (status);
}

/* SIGHUP handler. */
void sighup (void)
{
  zlog (NULL, LOG_INFO, "SIGHUP received");

  /* Terminate all thread. */
  sisis_terminate ();
  // TODO: Remove // sisis_reset ();
  zlog_info ("sisisd restarting!");

  // TODO: Remove or fix
  /* Reload config file. */
  // vty_read_config (config_file, config_default);

  // TODO: Remove
  /* Create VTY's socket */
  //vty_serv_sock (vty_addr, vty_port, SISIS_VTYSH_PATH);

  /* Try to return to normal operation. */
}

/* SIGINT handler. */
void sigint (void)
{
  zlog_notice ("Terminating on signal");

  if (! retain_mode)
    sisis_terminate ();

  sisis_exit (0);
}

/* SIGUSR1 handler. */
void sigusr1 (void)
{
  zlog_rotate (NULL);
}

/*
  Try to free up allocations we know about so that diagnostic tools such as
  valgrind are able to better illuminate leaks.

  Zebra route removal and protocol teardown are not meant to be done here.
  For example, "retain_mode" may be set.
*/
static void sisis_exit (int status)
{
  struct sisis_addr *sisis_addr;
  struct listnode *node, *nnode;
  int *socket;
  struct interface *ifp;
  extern struct zclient *zclient;

  /* it only makes sense for this to be called on a clean exit */
  assert (status == 0);

  /* TODO: reverse sisis_master_init */
  //for (ALL_LIST_ELEMENTS (sisis_info->sisis_addrs, node, nnode, sisis_addr))
    //sisis_delete (sisis_addr);
  list_free (sisis_info->sisis_addrs);

  /* reverse sisis_master_init */
  for (ALL_LIST_ELEMENTS_RO(sisis_info->listen_sockets, node, socket))
  {
    if (close ((int)(long)socket) == -1)
      zlog_err ("close (%d): %s", (int)(long)socket, safe_strerror (errno));
  }
  list_delete (sisis_info->listen_sockets);

  /* reverse sisis_zebra_init/if_init */
  if (retain_mode)
    if_add_hook (IF_DELETE_HOOK, NULL);
  for (ALL_LIST_ELEMENTS (iflist, node, nnode, ifp))
    if_delete (ifp);
  list_free (iflist);

  /* TODO: Remove
  // reverse sisis_attr_init
  sisis_attr_finish ();

  // reverse sisis_dump_init
  sisis_dump_finish ();

  // reverse sisis_route_init
  sisis_route_finish ();

  // reverse sisis_route_map_init/route_map_init
  route_map_finish ();

  // reverse sisis_scan_init
  sisis_scan_finish ();

  // reverse access_list_init
  access_list_add_hook (NULL);
  access_list_delete_hook (NULL);
  access_list_reset ();

  // reverse sisis_filter_init
  as_list_add_hook (NULL);
  as_list_delete_hook (NULL);
  sisis_filter_reset ();

  // reverse prefix_list_init
  prefix_list_add_hook (NULL);
  prefix_list_delete_hook (NULL);
  prefix_list_reset ();
  */

  // TODO: Remove
  /* reverse community_list_init */
  //community_list_terminate (sisis_clist);

  // TODO: Remove
  //cmd_terminate ();
  // vty_terminate ();
  if (zclient)
    zclient_free (zclient);

  /* TODO: reverse sisis_master_init */
  //if (master)
    //thread_master_free (master);

  if (zlog_default)
    closezlog (zlog_default);
  
  // TODO: Remove
  /*
  if (CONF_SISIS_DEBUG (normal, NORMAL))
    log_memstats_stderr ("sisisd");
  */
  
  exit (status);
}

/* Main routine of sisisd. Treatment of argument and start sisis finite
   state machine is handled at here. */
int main (int argc, char **argv)
{
  char *p;
  int opt;
  int daemon_mode = 0;
  char *progname;
  struct thread thread;
  int tmp_port;

  /* Set umask before anything for security */
  umask (0027);

  /* Preserve name of myself. */
  progname = ((p = strrchr (argv[0], '/')) ? ++p : argv[0]);

  /* sisis master init. */
  sisis_master_init();

  /* Command line argument treatment. */
  while (1) 
  {
    opt = getopt_long (argc, argv, "df:i:hp:l:ru:g:v", longopts, 0);
    
    if (opt == EOF)
      break;

    switch (opt) 
    {
      case 0:
        break;
      case 'd':
        daemon_mode = 1;
        break;
      case 'f':
        config_file = optarg;
        break;
      case 'i':
        pid_file = optarg;
        break;
      case 'p':
        tmp_port = atoi (optarg);
        if (tmp_port <= 0 || tmp_port > 0xffff)
          sisis_info->port = SISIS_PORT_DEFAULT;
        else
          sisis_info->port = tmp_port;
        break;
      case 'r':
        retain_mode = 1;
        break;
      case 'l':
        sisis_info->address = optarg;
        /* listenon implies -n */
      case 'u':
        sisisd_privs.user = optarg;
        break;
      case 'g':
        sisisd_privs.group = optarg;
        break;
      case 'v':
        print_version (progname);
        exit (0);
        break;
      case 'h':
        usage (progname, 0);
        break;
      default:
        usage (progname, 1);
        break;
      }
    }

  /* Make thread master. */
  master = sisis_info->master;

  /* Initializations. */
  srand (time (NULL));
  signal_init (master, Q_SIGC(sisis_signals), sisis_signals);
  zprivs_init (&sisisd_privs);
  cmd_init (1);
  // TODO: Remove
  // memory_init ();

  /* sisis related initialization.  */
  sisis_init();

  // TODO: Remove
  /* Parse config file. */
  //vty_read_config (config_file, config_default);

  /* Turn into daemon if daemon_mode is set. */
  if (daemon_mode && daemon (0, 0) < 0)
  {
    zlog_err("sisisd daemon failed: %s", strerror(errno));
    return (1);
  }

  /* Process ID file creation. */
  pid_output (pid_file);

  // TODO: Remove
  /* Make sisis vty socket. */
  //vty_serv_sock (vty_addr, vty_port, SISIS_VTYSH_PATH);

  /* Print banner. */
  zlog_notice ("sisisd %s starting: sisis@%s:%d", QUAGGA_VERSION,
	       (sisis_info->address ? sisis_info->address : "<all>"),
	       sisis_info->port);
  
  /* Start finite state machine, here we go! */
  while (thread_fetch (master, &thread))
    thread_call (&thread);
  
  // TODO: Fix memory issues on exit
  
  /* Not reached. */
  return (0);
}
