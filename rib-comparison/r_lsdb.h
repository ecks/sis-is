#ifndef R_LSDB
#define R_LSDB

struct r_lsdb
{
  unsigned long count;
  unsigned long count_self;
  unsigned int checksum;
  struct route_table *db;
};

struct r_lsdb * r_lsdb_new(void);
extern void r_lsdb_free(struct r_lsdb * lsdb);
extern  void r_lsdb_cleanup(struct r_lsdb * lsdb);
extern void r_lsdb_add(struct r_lsdb * lsdb, struct r_lsa * lsa);
extern void r_lsdb_delete(struct r_lsdb * lsdb, struct route_node * rn);
extern void r_lsdb_delete_all(struct r_lsdb * lsdb);
extern struct r_lsa * r_lsdb_lookup(struct r_lsdb * lsdb, struct r_lsa * lsa);
extern unsigned long r_lsdb_count_all(struct r_lsdb * lsdb);
extern unsigned int r_lsdb_checksum(struct r_lsdb * lsdb);
extern unsigned long r_lsdb_isempty(struct r_lsdb * lsdb);

#endif
