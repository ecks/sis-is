#ifndef SV_NETWORK_H
#define SV_NETWORK_H

extern int svz_net_init(void);

extern int svz_net_message_send(struct stream * buf);
#endif
