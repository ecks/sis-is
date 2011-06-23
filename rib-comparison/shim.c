#include "zebra.h"

#include "prefix.h"
#include "if.h"
#include "sockunion.h"
#include "lib/version.h"
#include "log.h"
#include "sockopt.h"
#include "privs.h"
#include "sigevent.h"

#include "ospfd/ospfd.h"

#include "rib-comparison/shimd.h"

/* ospfd privileges */
zebra_capabilities_t _caps_p [] =
{
  ZCAP_NET_RAW,
  ZCAP_BIND,
};

struct zebra_privs_t shimd_privs =
{
#if defined(QUAGGA_USER) && defined(QUAGGA_GROUP)
  .user = QUAGGA_USER,
  .group = QUAGGA_GROUP,
#endif
#if defined(VTY_GROUP)
  .vty_group = VTY_GROUP,
#endif
  .caps_p = _caps_p,
  .cap_num_p = sizeof(_caps_p)/sizeof(_caps_p[0]),
  .cap_num_i = 0
};

struct thread_master *master;

/* Process ID saved for use by init system */
const char *pid_file = PATH_SHIM_PID;

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
  struct thread thread;
  uint64_t host_num;

  // Check number of args
  if (argc <  2)
  {
    printf("Usage: %s <host_num>\n", argv[0]);
    exit(1);
  }

  // Get host number
  sscanf (argv[1], "%llu", &host_num);

  shim_master_init();
  master = sm->master;

  zprivs_init (&shimd_privs);
  signal_init (master, Q_SIGC(shim_signals), shim_signals);

  shim_sisis_init(host_num);

  /* Process id file create. */
  pid_output (pid_file);

  zlog_notice("Shim starts");

  while (thread_fetch (master, &thread))
    thread_call (&thread);

  return 0;
}
