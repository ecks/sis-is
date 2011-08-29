#ifndef SV_H
#define SV_H

#define SV_HEADER_SIZE             42
#define SVZ_HEADER_SIZE            10
#define SVZ_OUT_HEADER_SIZE	   4
#define SVZ_IN_HEADER_SIZE         6

#define SV_JOIN_ALLSPF             0
#define SV_LEAVE_ALLSPF            1
#define SV_JOIN_ALLD               2
#define SV_LEAVE_ALLD              3
#define SV_MESSAGE		   4

extern void sv_create_header (struct stream * s, uint16_t cmd);

#endif
