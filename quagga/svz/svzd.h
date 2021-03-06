#ifndef SVZD_H
#define SVZD_H

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

struct shim_master
{
  struct thread_master *master;
  struct list * listen_sockets;
  time_t start_time;
  struct shim * shim;
};

extern struct shim_master *sm;
extern struct thread_master *master;
extern struct shim * shim;

extern void shim_terminate (void);
extern void shim_master_init (void);
extern void shim_init (uint64_t host_num);
extern struct shim * shim_new (uint64_t hostnum);

#endif
