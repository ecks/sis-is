#ifndef SV_H
#define SV_H

#define SV_HEADER_SIZE             42
#define SVZ_HEADER_SIZE            8

#define SV_JOIN_ALLSPF             0
#define SV_LEAVE_ALLSPF            1
#define SV_JOIN_ALLD               2
#define SV_LEAVE_ALLD              3
#define SV_MESSAGE		   4

extern void sv_create_header (struct stream * s, uint16_t cmd);

#endif
