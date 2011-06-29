#include <zebra.h>

#include <thread.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include "stream.h"
#include <stdlib.h>
#include <string.h>
#include <sockopt.h>
#include <sys/time.h>
#include <privs.h>
#include <memory.h>
#include "log.h"
#include "linklist.h"

#include "sisis_structs.h"
#include "sisis_api.h"
#include "sisis_process_types.h"

extern struct zebra_privs_t shimd_privs;

#include "shim/shimd.h"
#include "shim/shim_sisis.h"
#include "rospf6d/ospf6_message.h"

#define BACKLOG 10
int
shim_sisis_init (uint64_t host_num)
{
  int sockfd;
  uint64_t ptype, ptype_version, pid, timestamp;
  char sisis_addr[INET6_ADDRSTRLEN];

  // Store process type
  ptype = (uint64_t)SISIS_PTYPE_RIBCOMP_SHIM;
  ptype_version = (uint64_t)VERSION;

  // Get pid
  pid = getpid();        // Get start time
  struct timeval tv;
  gettimeofday(&tv, NULL);
  timestamp = (tv.tv_sec * 100 + (tv.tv_usec / 10000)) & 0x00000000ffffffffLLU;   // In 100ths of seconds

  // Register SIS-IS address
  if (sisis_register(sisis_addr, ptype, ptype_version, host_num, pid, timestamp) != 0)
  {
    printf("Failed to register SIS-IS address.\n");
    exit(1);        
  }
  
  printf("Opening socket at %s on port %i.\n", sisis_addr, SHIM_SISIS_PORT);

  // Set up socket address info
  struct addrinfo hints, *addr;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET6;     // IPv6
  hints.ai_socktype = SOCK_STREAM;  // TCP
  char port_str[8];
  sprintf(port_str, "%u", SHIM_SISIS_PORT);
  getaddrinfo(sisis_addr, port_str, &hints, &addr);

  // Create socket
  if ((sockfd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol)) < 0)
  {
    printf("Failed to open socket.\n");
    exit(1);
  }

  if ((shim_sisis_listener(sockfd, addr->ai_addr, addr->ai_addrlen)) != 0)
    close(sockfd);

  // Bind to port
//  if (bind(sockfd, addr->ai_addr, addr->ai_addrlen) < 0)
//  {
//    printf("Failed to bind socket to port.\n");
//    exit(2);
//  }

//  if (listen(sockfd, BACKLOG) < 0)
//  {
//    printf("Failed to listen.\n");
//    exit(2);
//  }

  return sockfd;
}

int
shim_sisis_listener(int sock, struct sockaddr * sa, socklen_t salen)
{
  struct sisis_listener * listener;
  int ret,en;

  sockopt_reuseaddr(sock);
  sockopt_reuseport(sock);

  if (sa->sa_family == AF_INET6) 
  {
    int on = 1;
    setsockopt (sock, IPPROTO_IPV6, IPV6_V6ONLY,
                (void *) &on, sizeof (on));
  }

  if (shimd_privs.change (ZPRIVS_RAISE) )
    zlog_err ("shim_sisis_listener: could not raise privs");

  ret = bind (sock, sa, salen);
  en = errno;
  if (shimd_privs.change (ZPRIVS_LOWER) )
    zlog_err ("shim_sisis_listener: could not lower privs");

  if (ret < 0)
  {
    zlog_err ("bind: %s", safe_strerror (en));
    return ret;
  }

  ret = listen (sock, 3);
  if (ret < 0)
  {
    zlog_err ("listen: %s", safe_strerror (errno));
    return ret;
  }

  listener = XMALLOC (MTYPE_SHIM_SISIS_LISTENER, sizeof(*listener));
  listener->fd = sock;
  listener->ibuf = stream_new(1500 + 8);
  memcpy(&listener->su, sa, salen);
  listener->thread = thread_add_read (master, shim_sisis_accept, listener, sock);
  listnode_add (sm->listen_sockets, listener);

  return 0;
}

int
shim_sisis_accept(struct thread * thread)
{
  int accept_sock;
  int sisis_sock;
  struct sisis_listener *listener;
  union sockunion su;
  char buf[SU_ADDRSTRLEN];

  listener  = THREAD_ARG(thread);
  accept_sock = THREAD_FD (thread);
  if (accept_sock < 0)
    {   
      zlog_err ("accept_sock is negative value %d", accept_sock);
      return -1; 
    }   
  listener->thread = thread_add_read (master, shim_sisis_accept, listener, accept_sock);
  
  sisis_sock = sockunion_accept(accept_sock, &su);

  if (sisis_sock < 0)
    {
      zlog_err ("[Error] SISIS socket accept failed (%s)", safe_strerror (errno));
      return -1;
    } 
  
  zlog_notice ("SISIS connection from host %s", inet_sutop (&su, buf));

  listener->thread = thread_add_read (master, shim_sisis_read, listener, sisis_sock);
  return 0;
}

int 
shim_sisis_read(struct thread * thread)
{
  struct sisis_listener *listener;
  int sisis_sock;
  uint16_t length, command;
  int nbytes;

  zlog_notice("Reading packet from SISIS connection!\n");

  /* first of all get listener pointer. */
  listener = THREAD_ARG (thread);
  sisis_sock = THREAD_FD (thread);

  /* prepare for next packet. */
  listener->thread = thread_add_read (master, shim_sisis_read, listener, sisis_sock);

  nbytes = stream_read_unblock (listener->ibuf, sisis_sock, 4);

  if (nbytes < 0)
  {
    // error
    return -1;
  }

  if (nbytes == 0)
  {
    // didnt read anything
    return -1;
  }

  if (stream_get_endp (listener->ibuf) != (4))
  {
    // didnt read whole stream
    return -1;
  }

  /* read OSPF packet. */
  length = stream_getw (listener->ibuf);
  command = stream_getw (listener->ibuf);

  switch(command)
  {
    case ROSPF6_JOIN_ALLSPF:
      printf("join allspf received\n");
      break;
    default:
      break;
  }
 
  return 0;
}
