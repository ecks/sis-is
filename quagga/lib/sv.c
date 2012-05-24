#include "zebra.h"
#include "stream.h"

#include "sv.h"

void
sv_create_header (struct stream * s, uint16_t cmd)
{
  stream_putw (s, SV_HEADER_SIZE);                                // 0
  stream_putw (s, cmd);                                           // 2
  stream_put (s, 0, 16); // src - leave blank for now             // 4
  stream_put (s, 0, 16); // dst - leave blank for now             // 20
  stream_putl (s, 0);    // ifindex - leave blank for now         // 36
  stream_putw (s, 0);    // leave the checksum blank for now      // 40
}
