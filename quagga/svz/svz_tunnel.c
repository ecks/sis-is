#include <zebra.h>
#include <time.h>

#include "stream.h"
#include "buffer.h"
#include "network.h"
#include "if.h"
#include "log.h"
#include "thread.h"
#include "memory.h"

#include "svz/svz_tunnel.h"

enum event {TCLIENT_SCHEDULE, TCLIENT_READ, TCLIENT_CONNECT};

static void svz_tunnel_event (enum event, struct tclient *);

extern struct thread_master * master;

struct tclient *
svz_tunnel_new ()
{
  struct tclient * tclient;
  tclient = XCALLOC(MTYPE_ZCLIENT, sizeof(struct tclient));
  
  tclient->ibuf = stream_new(ZEBRA_MAX_PACKET_SIZ);
  tclient->obuf = stream_new(ZEBRA_MAX_PACKET_SIZ);
  tclient->wb = buffer_new(0);
  
  return tclient;
}

void
svz_tunnel_free (struct tclient * tclient)
{
  if (tclient->ibuf)
    stream_free(tclient->ibuf);
  if (tclient->obuf)
    stream_free(tclient->obuf);
  if (tclient->wb)
    buffer_free(tclient->wb);

  XFREE(MTYPE_ZCLIENT, tclient);
}

void
svz_tunnel_stop (struct tclient * tclient)
{
  THREAD_OFF(tclient->t_read);
  THREAD_OFF(tclient->t_connect);
  THREAD_OFF(tclient->t_write);

  stream_reset(tclient->ibuf);
  stream_reset(tclient->obuf);

  buffer_reset(tclient->wb);

  if (tclient->sock >= 0)
  {
    close(tclient->sock);
    tclient->sock = -1;
  }
  tclient->fail = 0;
}

void
svz_tunnel_reset (struct tclient * tclient)
{
  svz_tunnel_stop(tclient);
  svz_tunnel_init(tclient);
}

void
svz_tunnel_init (struct tclient * tclient)
{ 
  tclient->enable = 1;
  
  tclient->sock = -1;

  svz_tunnel_event (TCLIENT_SCHEDULE, tclient);
}

/* Make socket to zebra daemon. Return zebra socket. */
int
svz_tunnel_socket (void)
{
  int sock;
  int ret;
  struct sockaddr_in serv;

  /* We should think about IPv6 connection. */
  sock = socket (AF_INET, SOCK_STREAM, 0);
  if (sock < 0)
    return -1;

  /* Make server socket. */
  memset (&serv, 0, sizeof (struct sockaddr_in));
  serv.sin_family = AF_INET;
  serv.sin_port = htons (ZEBRA_PORT);
#ifdef HAVE_STRUCT_SOCKADDR_IN_SIN_LEN
  serv.sin_len = sizeof (struct sockaddr_in);
#endif /* HAVE_STRUCT_SOCKADDR_IN_SIN_LEN */
  serv.sin_addr.s_addr = htonl(INADDR_LOOPBACK);;

  /* Connect to zebra. */
  ret = connect (sock, (struct sockaddr *) &serv, sizeof (serv));
  if (ret < 0)
    {
      close (sock);
      return -1;
    }
  return sock;
}

#include <sys/un.h>

int
svz_tunnel_socket_un (const char * path)
{
  int ret; 
  int sock, len; 
  struct sockaddr_un addr;

  sock = socket (AF_UNIX, SOCK_STREAM, 0);
  if (sock < 0) 
    return -1;
  
  /* Make server socket. */ 
  memset (&addr, 0, sizeof (struct sockaddr_un));
  addr.sun_family = AF_UNIX;
  strncpy (addr.sun_path, path, strlen (path));
#ifdef HAVE_STRUCT_SOCKADDR_UN_SUN_LEN
  len = addr.sun_len = SUN_LEN(&addr);
#else
  len = sizeof (addr.sun_family) + strlen (addr.sun_path);
#endif /* HAVE_STRUCT_SOCKADDR_UN_SUN_LEN */

  ret = connect (sock, (struct sockaddr *) &addr, len);
  if (ret < 0) 
    {    
      close (sock);
      return -1;
    }    
  return sock;
} 

static int
svz_tunnel_failed(struct tclient * tclient)
{
  tclient->fail++;
  svz_tunnel_stop(tclient);
  svz_tunnel_event(TCLIENT_CONNECT, tclient);
  return -1;
}

static int
svz_tunnel_flush_data(struct thread * thread)
{
  struct tclient * tclient = THREAD_ARG(thread);
 
  tclient->t_write = NULL;
  if(tclient->sock < 0)
    return -1;

  switch (buffer_flush_available(tclient->wb, tclient->sock))
  {
    case BUFFER_ERROR:
      zlog_warn("%s: buffer_flush_available failed on zclient fd %d, closing",
                __func__, tclient->sock);
      return svz_tunnel_failed(tclient);
      break;
    case BUFFER_PENDING:
      tclient->t_write = thread_add_write(master, svz_tunnel_flush_data,
                                          tclient, tclient->sock);
      break;
    case BUFFER_EMPTY:
      break;
  }    
  return 0;
}

int
svz_tunnel_send_message (struct tclient * tclient)
{
  if (tclient->sock < 0)
    return -1;
  switch(buffer_write(tclient->wb, tclient->sock, STREAM_DATA(tclient->obuf) + stream_get_getp(tclient->obuf),
                      stream_get_endp(tclient->obuf) - stream_get_getp(tclient->obuf)))
  {
    case BUFFER_ERROR:
      zlog_warn("%s: buffer_write failed to tclient fd %d, closing",
                __func__, tclient->sock);
      return svz_tunnel_failed(tclient);
    case BUFFER_EMPTY:
      THREAD_OFF(tclient->t_write);
      break;
    case BUFFER_PENDING:
      THREAD_WRITE_ON(master, tclient->t_write, 
                      svz_tunnel_flush_data, tclient, tclient->sock);
      break;
  }
  return 0;
}

int 
svz_tunnel_start (struct tclient * tclient)
{
  if (! tclient->enable)
    return 0;

  if (tclient->sock >= 0)
    return 0;

  if (tclient->t_connect)
    return 0;
 
#ifdef HAVE_TCP_ZEBRA 
  tclient->sock = svz_tunnel_socket();
#else
  tclient->sock = svz_tunnel_socket_un(ZEBRA_SERV_PATH);
#endif

  if (tclient->sock < 0)
  {  
    tclient->fail++;
    svz_tunnel_event(TCLIENT_CONNECT, tclient);
    return -1;
  }
 
  if (set_nonblocking(tclient->sock) < 0)
    zlog_warn("%s: set_nonblocking(%d) failed", __func__, tclient->sock);
 
  tclient->fail = 0;
  
  svz_tunnel_event(TCLIENT_READ, tclient);

  return 0;
}

static int
svz_tunnel_connect(struct thread * t)
{
  struct tclient * tclient;
  
  tclient = THREAD_ARG(t);
  tclient->t_connect = NULL;
  
  return svz_tunnel_start(tclient);
}

static int
svz_tunnel_read(struct thread * thread)
{
  uint16_t length, command;
  uint8_t marker, version;
  struct stream * dbuf;
  int ret;
  size_t already;
  struct tclient * tclient;

  zlog_notice("Reading packet from Zebra!");

  /* Get socket to zebra. */
  tclient = THREAD_ARG (thread);
  tclient->t_read = NULL;

  stream_reset(tclient->ibuf);

  /* Read zebra header (if we don't have it already). */
  if ((already = stream_get_endp(tclient->ibuf)) < ZEBRA_HEADER_SIZE)
  {
    ssize_t nbyte;
    if (((nbyte = stream_read_try(tclient->ibuf, tclient->sock,
                                   ZEBRA_HEADER_SIZE-already)) == 0) ||
        (nbyte == -1))
    {
      return svz_tunnel_failed(tclient);
    }
    if (nbyte != (ssize_t)(ZEBRA_HEADER_SIZE-already))
    {
      /* Try again later. */
      svz_tunnel_event (TCLIENT_READ, tclient);
      return 0;
    }
    already = ZEBRA_HEADER_SIZE;   
  }

  dbuf = stream_dup(tclient->ibuf);

  stream_set_getp(dbuf, 0);

  length = stream_getw(dbuf);
  marker = stream_getc(dbuf);
  version = stream_getc(dbuf);
  command = stream_getw(dbuf);
 
  if(already < length)
  {
    ssize_t nbyte;
    if (((nbyte = stream_read_try(tclient->ibuf, tclient->sock,
                                  length-already)) == 0) ||
       (nbyte == -1)) 
     {       
         zlog_debug("zebra connection closed socket [%d].", tclient->sock);
         return svz_tunnel_failed(tclient);
     }        
     if (nbyte != (ssize_t)(length-already))
     {        
       /* Try again later. */
       svz_tunnel_event (TCLIENT_READ, tclient);
       return 0;
     }
  }

  stream_free(dbuf);

  shim_receive(tclient);

  svz_tunnel_event(TCLIENT_READ, tclient);

  return 0;
}

extern void
svz_tunnel_event (enum event event, struct tclient * tclient)
{
  switch(event)
  { 
    case TCLIENT_SCHEDULE:
      if (! tclient->t_connect)
        tclient->t_connect = thread_add_event(master, svz_tunnel_connect, tclient, 0);
      break;
    case TCLIENT_CONNECT:
      if (tclient->fail >= 10)
        return;
      if (! tclient->t_connect)
        tclient->t_connect = thread_add_timer(master, svz_tunnel_connect, tclient, tclient->fail < 3 ? 10 : 60);
      break;
    case TCLIENT_READ:
      tclient->t_read = thread_add_read(master, svz_tunnel_read, tclient, tclient->sock);
      break;
  }
}
