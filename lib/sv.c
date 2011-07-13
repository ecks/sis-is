#include "zebra.h"
#include "stream.h"

#include "sv.h"

void
sv_create_header (struct stream * s, uint16_t cmd)
{
  stream_putw (s, SV_HEADER_SIZE);
  stream_putw (s, cmd);
  stream_put (s, 0, 16); // src - leave blank for now
  stream_put (s, 0, 16); // dst - leave blank for now
  stream_putl (s, 0);    // ifindex - leave blank for now
  stream_putw (s, 0);    // leave the checksum blank for now
}
