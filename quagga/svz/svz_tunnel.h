#ifndef SVZ_TUNNEL_H
#define SVZ_TUNNEL_H

#define ZEBRA_MAX_PACKET_SIZ          4096

#define ZEBRA_HEADER_SIZE   	      6

struct tclient
{
  int sock;

  int enable;

  int fail;

  struct stream * ibuf;

  struct stream * obuf;

  struct buffer * wb;

  struct thread * t_read;
  struct thread * t_connect;
  struct thread * t_write;
};
  
extern struct tclient * svz_tunnel_new (void);
extern void svz_tunnel_init(struct tclient *);
extern int svz_tunnel_start(struct tclient *);
extern void svz_tunnel_stop(struct tclient *);
extern void svz_tunnel_reset(struct tclient *);
extern void svz_tunnel_free(struct tclient *);
extern int svz_tunnel_socket(void);
extern int svz_tunnel_socket_un(const char *);

extern int svz_tunnel_send_message(struct tclient *);
#endif 
