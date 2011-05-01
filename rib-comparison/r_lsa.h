#ifndef R_LSA_H
#define R_LSA_H

struct r_lsa_header
{
  struct in_addr id;
};

struct r_lsa
{
  struct r_lsa_header * data;
  int lock;
  struct r_lsdb * lsdb;
};

struct r_external_lsa
{
  struct r_lsa_header header;
};

struct r_as_external_lsa
{
  struct r_lsa_header header;
  struct in_addr mask;
};

extern struct r_lsa * r_lsa_new(void);
extern void r_lsa_free(struct r_lsa * lsa);
extern struct r_lsa * r_lsa_lock(struct r_lsa * lsa);
extern struct r_lsa_header * r_lsa_data_new(size_t size);
extern void r_lsa_data_free(struct r_lsa_header * lsah);
#endif
