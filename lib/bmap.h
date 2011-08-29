#ifndef BMAP_H
#define BMAP_H

struct bmap
{
  uint16_t key;
  int count;
};

extern void bmap_init();
extern void bmap_reset();
extern void bmap_hook_add(void(*) (struct bmap *));
extern void bmap_hook_delete(void (*) (struct bmap *));
extern struct bmap * bmap_lookup(const uint16_t checksum);
extern struct bmap * bmap_set(unsigned int checksum);
extern int bmap_unset(unsigned int checksum);

#endif
