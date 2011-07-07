#include "zebra.h"
#include "stream.h"

#include "sv.h"

void
sv_create_header (struct stream * s, uint16_t cmd)
{
  stream_putw (s, SV_HEADER_SIZE);
  stream_putw (s, cmd);
}
