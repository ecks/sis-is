#ifndef _ZEBRA_SHIMD_H
#define _ZEBRA_SHIMD_H

struct shim_master
{
  struct thread_master *master;
  struct list * listen_sockets;
  time_t start_time;
};

struct shim
{
  struct thread *t_write;  
  struct thread *t_read;
  struct thread *sis_read;
  int fd;
  int sis_fd;
  int maxsndbuflen;
  struct stream *ibuf;
  struct stream *obuf;
};

extern struct shim_master *sm;
extern struct thread_master *master;

extern void shim_init (uint64_t hostnum);
extern struct shim * shim_new (uint64_t hostnum);
extern void shim_terminate (void);
extern void shim_master_init (void);

#endif
