#include "zebra.h"
#include "memory.h"
#include "hash.h"
#include "bmap.h"

struct hash * buffers;

static void (*bmap_add_hook) (struct bmap *) = NULL;
static void (*bmap_delete_hook) (struct bmap *) = NULL;

static struct bmap *
bmap_new (void)
{
  struct bmap * new;

  new = XCALLOC(MTYPE_BMAP, sizeof(struct bmap));
  return new;
}

static void
bmap_free(struct bmap * bmap)
{
  XFREE(MTYPE_BMAP, bmap);
}

/* if there is no entry
 * returns NULL
 */
struct bmap *
bmap_lookup(const uint16_t checksum)
{
  struct bmap key;
  struct bmap * bmap;

  key.key = checksum;
  bmap = hash_lookup (buffers, &key);

  return bmap;
}

void
bmap_hook_add(void (*func) (struct bmap *))
{
  bmap_add_hook = func;
}

void
bmap_hook_delete(void (*func) (struct bmap *))
{
  bmap_delete_hook = func;
}

static void *
bmap_alloc(void * arg)
{
  struct bmap * bmaparg = arg;
  struct bmap * bmap;

  bmap = bmap_new();
  bmap->key = bmaparg->key;
  bmap->count = 0;
  bmap->sent = 0;
  return bmap;
}

/* if there is no entry,
 * then it is created
 */
static struct bmap *
bmap_get(const uint16_t checksum)
{
  struct bmap key;
  
  key.key = checksum;

  return (struct bmap *) hash_get(buffers, &key, bmap_alloc);
}

static unsigned int
bmap_make(void * data)
{
  struct bmap * bmap = data;
  return (unsigned int)bmap->key;
}

static int
bmap_cmp(const void * arg1, const void * arg2)
{
  const struct bmap * bmap1 = arg1;
  const struct bmap * bmap2 = arg2;

  return bmap1->key == bmap2->key;
}

struct bmap *
bmap_set(unsigned int checksum)
{
  struct bmap * bmap;

  bmap = bmap_get(checksum);
  
  if(bmap_add_hook)
    (*bmap_add_hook) (bmap);

  return bmap;
}

int 
bmap_unset(unsigned int checksum)
{
  struct bmap * bmap;

  bmap = bmap_lookup(checksum);
  if(!bmap)
    return 0;

  if(bmap_delete_hook)
    (*bmap_delete_hook) (bmap);

  hash_release(buffers, bmap);
  bmap_free(bmap);

  return 1;
}

void
bmap_reset()
{
  hash_clean(buffers, (void (*) (void *)) bmap_free);
}

void
bmap_init()
{
  buffers = hash_create(bmap_make, bmap_cmp);
}
