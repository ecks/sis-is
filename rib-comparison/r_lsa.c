#include <zebra.h>

#include "prefix.h"
#include "table.h"
#include "memory.h"
#include "log.h"

#include "rib-comparison/r_lsdb.h"
#include "rib-comparison/r_lsa.h"

struct r_lsa *
r_lsa_new(void)
{
  struct r_lsa * new;
  
  new = XCALLOC(MTYPE_R_LSA, sizeof(struct r_lsa));
  new->lock = 1;
  return new;
}

void
r_lsa_free(struct r_lsa * lsa)
{
  assert(lsa->lock == 0);

  if(lsa->data != NULL)
    r_lsa_data_free(lsa->data);

  memset(lsa, 0, sizeof(struct r_lsa));
  XFREE(MTYPE_R_LSA, lsa); 
}

struct r_lsa *
r_lsa_lock(struct r_lsa * lsa)
{
  lsa->lock++;
  return lsa;
}

void
r_lsa_unlock(struct r_lsa * lsa)
{
  lsa->lock--;

  if(lsa->lock == 0)
  {
    r_lsa_free(lsa); 
    lsa = NULL;
  }
}

struct r_lsa_header *
r_lsa_data_new(size_t size)
{
  return XCALLOC(MTYPE_R_LSA_DATA, sizeof(size));
}

void r_lsa_data_free(struct r_lsa_header * lsah)
{
  XFREE(MTYPE_R_LSA_DATA, lsah);
}
