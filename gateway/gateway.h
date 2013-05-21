
#ifndef _GATEWAY_H
#define _GATEWAY_H

#define MAX_ARP_TRIES       10
#define ARP_TIMEOUT         100 // in ms

void gateway_v_init( void );

void gateway_v_send_ethernet( netmsg_t netmsg );
void gateway_v_receive_ethernet( uint16_t type, netmsg_t netmsg );


#endif

