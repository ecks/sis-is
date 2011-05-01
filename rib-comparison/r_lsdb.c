#include <zebra.h>

#include "prefix.h"
#include "table.h"
#include "memory.h"
#include "log.h"

#include "rib-comparison/r_lsa.h"
#include "rib-comparison/r_lsdb.h"

struct r_lsdb * r_lsdb_new()
{
  struct r_lsdb  * new;
  new = XCALLOC(MTYPE_R_LSDB, sizeof(struct r_lsdb));
  new->db = route_table_init();
  
  return new;
}

void
r_lsdb_free(struct r_lsdb * lsdb)
{
  r_lsdb_cleanup(lsdb);
  XFREE(MTYPE_R_LSDB, lsdb);
}

void
r_lsdb_cleanup(struct r_lsdb * lsdb)
{
  r_lsdb_delete_all(lsdb);

  route_table_finish(lsdb->db);
}

static void 
r_lsdb_prefix_set(struct prefix_ls * lp, struct r_lsa * lsa)
{
  lp->family = 0;
  lp->prefixlen = 64;
  lp->id = lsa->data->id;
}

static void
r_lsdb_delete_entry(struct r_lsdb * lsdb, struct route_node *rn)
{
  struct r_lsa * lsa = rn->info;

  if(!lsa)
    return;

  lsdb->count--;
  rn->info = NULL;
  route_unlock_node(rn);
  r_lsa_unlock(lsa);
  return;
}

void
r_lsdb_add(struct r_lsdb * lsdb, struct r_lsa * lsa)
{
  struct route_table * table;
  struct prefix_ls lp;
  struct route_node * rn;

  table = lsdb->db;
  r_lsdb_prefix_set(&lp, lsa);
  rn = route_node_get(table, (struct prefix *)&lp);
 
  if(rn->info && rn->info == lsa)
    return;

  if(rn->info)
    r_lsdb_delete_entry(lsdb, rn);

  lsdb->count++;
//  lsdb->checksum += ntohs(lsa->data->checksum);  
  rn->info = r_lsa_lock(lsa);
}

void
r_lsdb_delete(struct r_lsdb * lsdb, struct route_node * rn)
{
  r_lsdb_delete_entry(lsdb, rn);
} 

void
r_lsdb_delete_all(struct r_lsdb * lsdb)
{
  struct route_table * table;
  struct route_node * rn;

  table = lsdb->db;
  for(rn = route_top(table); rn; rn = route_next(rn))
    if(rn->info != NULL)
      r_lsdb_delete_entry(lsdb, rn);
}

struct r_lsa *
r_lsdb_lookup(struct r_lsdb * lsdb, struct r_lsa * lsa)
{
  struct route_table * table;
  struct prefix_ls lp;
  struct route_node * rn;
  struct r_lsa * find;

  table = lsdb->db;
  r_lsdb_prefix_set(&lp, lsa);
  rn = route_node_lookup(table, (struct prefix *)&lp);
  if(rn)
  {
    find = rn->info;
    return find;
  }
  return NULL; 
}

unsigned long
r_lsdb_count_all(struct r_lsdb * lsdb)
{
  return lsdb->count;
}

unsigned int
r_lsdb_checksum(struct r_lsdb * lsdb)
{
  return lsdb->checksum;
}

unsigned long
r_lsdb_isempty(struct r_lsdb * lsdb)
{
  return (lsdb->count == 0);
} 

