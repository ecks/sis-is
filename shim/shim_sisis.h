#ifndef SHIM_SISIS_H
#define SHIM_SISIS_H

#define SHIM_SISIS_PORT 50000

struct sisis_listener
{
  int fd;
  int sisis_fd;
  struct stream * ibuf;
  struct thread *thread;
};


extern int shim_sisis_init (uint64_t host_num);
extern int shim_sisis_read(struct thread * thread);
extern int shim_sisis_accept(struct thread * thread);
extern int shim_sisis_listener(int sock, struct sockaddr * sa, socklen_t slen);
extern int shim_sisis_write(struct stream * obuf, struct buffer * wb);
#endif
