#ifndef _ZEBRA_SHIMD_H
#define _ZEBRA_SHIMD_H

struct shim_master
{
  struct thread_master *master;
  time_t start_time;
};

struct shim
{
  struct thread *t_write;  
  struct thread *t_read;
  int fd;
  int maxsndbuflen;
  struct stream *ibuf;
};

extern struct shim_master *sm;
extern struct thread_master *master;

extern void shim_terminate (void);
extern void shim_master_init (void);

#endif
