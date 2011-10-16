#include "zebra.h"

#include "prefix.h"
#include "if.h"
#include "sockunion.h"
#include "lib/version.h"
#include "log.h"
#include "sockopt.h"
#include "privs.h"
#include "sigevent.h"
#include "command.h"
#include "memory.h"

#include "ospfd/ospfd.h"

#include "svz/svzd.h"

#define SVZ_DEFAULT_CONFIG       "svzd.conf"

/* ospfd privileges */
zebra_capabilities_t _caps_p [] =
{
  ZCAP_NET_RAW,
  ZCAP_BIND,
};


struct zebra_privs_t svd_privs =
{
#if defined(QUAGGA_USER) && defined(QUAGGA_GROUP)
//  .user = QUAGGA_USER,
//  .group = QUAGGA_GROUP,
#endif
#if defined(VTY_GROUP)
  .vty_group = VTY_GROUP,
#endif
  .caps_p = _caps_p,
  .cap_num_p = sizeof(_caps_p)/sizeof(_caps_p[0]),
  .cap_num_i = 0
};

struct option longopts[] =
{
  { "config_file", required_argument, NULL, 'f'},
  { "help", no_argument,	      NULL, 'h'},
  { 0 }
};

char config_default[] = SYSCONFDIR SVZ_DEFAULT_CONFIG;

char * progname;

struct thread_master *master;

/* Process ID saved for use by init system */
const char *pid_file = PATH_SVZ_PID;

static void
usage (char * progname, int status)
{
  if (status != 0)
    fprintf (stderr, "Try `%s --help' for more information.\n", progname);
  else
  {
    printf ("Usage : %s [OPTION...]\n\n\
Daemon which manages SVZ.\n\n\
-f, --config_file  Set configuration file name\n\
-h, --help         Display this help and exit\n\
\n\
Report bugs to zebra@zebra.org\n", progname);
  }   

  exit (status);
}

/* SIGHUP handler. */
static void
sighup (void)
{
  zlog (NULL, LOG_INFO, "SIGHUP received");
}
/* SIGINT / SIGTERM handler. */
static void
sigint (void)
{
  zlog_notice ("Terminating on signal");
  shim_terminate ();
}
/* SIGUSR1 handler. */
static void
sigusr1 (void)
{
  zlog_rotate (NULL);
}

struct quagga_signal_t shim_signals[] =
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

int
main (int argc, char *argv[], char *envp[])
{
  char * p;
  struct thread thread;
  int opt;
  char * config_file = NULL;
  uint64_t host_num = 1;

  progname = ((p = strrchr (argv[0], '/')) ? ++p : argv[0]);
  /* Command line argument treatment. */
  while (1)
  {
    opt = getopt_long (argc, argv, "f:n:", longopts, 0);

    if (opt == EOF)
      break;

    switch (opt)
    {
      case 0:
        break;
      case 'f':
        config_file = optarg;
        break;
      case 'n':
        host_num = *optarg;
        break;
      case 'h':
        usage (progname, 0);
        break;
      default:
        usage (progname, 1);
    }
  }

  shim_master_init();
  master = sm->master;

  /* Initialization */
  zlog_default = openzlog (progname, ZLOG_SHIM,
                           LOG_CONS|LOG_NDELAY|LOG_PID,
                           LOG_DAEMON);
  zprivs_init (&svd_privs);
  signal_init (master, Q_SIGC(shim_signals), shim_signals);
  cmd_init(1);
  vty_init(master);
  memory_init ();
  if_init ();

  shim_init(host_num);

  sort_node();

  vty_read_config (config_file, config_default);

  /* Process id file create. */
  pid_output (pid_file);

  zlog_notice("Svz starts");

  while (thread_fetch (master, &thread))
    thread_call (&thread);

  return 0;
}
