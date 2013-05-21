

#ifndef _BRIDGING_H
#define _BRIDGING_H

#include "ip.h"


#define BRIDGE_TABLE_SIZE               32

#define BRIDGE_FLAGS_MANUAL_IP          0x01
#define BRIDGE_FLAGS_REQUEST_IP         0x02
#define BRIDGE_FLAGS_IP_VALID           0x04

typedef struct{
    uint16_t short_addr;
    ip_addr_t ip;
    uint32_t lease;         // in seconds
    uint32_t time_left;     // in seconds
    uint8_t flags;
} bridge_t; // 15 bytes


void bridge_v_init( void );

bridge_t *bridge_b_get_bridge( ip_addr_t ip );
bridge_t *bridge_b_get_bridge2( uint16_t short_addr );
bridge_t *bridge_b_get_new( void );
void bridge_v_add_to_bridge( bridge_t *bridge );


#endif

// (C)2011 - 2012 by Jeremy Billheimer



