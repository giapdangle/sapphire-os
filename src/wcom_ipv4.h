/* 
 * <license>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 *
 * This file is part of the Sapphire Operating System
 *
 * Copyright 2013 Sapphire Open Systems
 *
 * </license>
 */

#ifndef _WCOM_IPv4_H
#define _WCOM_IPv4_H

#include "ip.h"
#include "memory.h"
#include "list.h"
#include "netmsg.h"
#include "wcom_mac.h"

#define WCOM_IPv4_MAX_RX_STREAMS 	        4
#define WCOM_IPv4_MAX_TX_MESSAGES 	        4
#define WCOM_IPv4_MAX_TX_ROUTES 	        4

#define WCOM_IPv4_REPLAY_CACHE_ENTRIES		16

#define WCOM_IPv4_TIMEOUT_TICK_RATE_MS      100 // rx stream timeout tick rate, and 

#define WCOM_IPv4_REPLAY_MAX_AGE_TICKS      20  // replay cache aging
#define WCOM_IPv4_RX_FRAGMENT_TIMEOUT       10 // in ticks

// frame type flags
#define WCOM_FRAME_FLAGS_IPv4               0b10000000
#define WCOM_FRAME_FLAGS_SOURCE_ROUTE       0b01000000
#define WCOM_FRAME_FLAGS_AUTH               0b00100000

#define WCOM_FRAME_FLAGS( flags )     ( flags & 0xf0 )             

typedef list_node_t wcom_ipv4_msg_t;
typedef struct{
    uint8_t flags;
    uint8_t age;
    uint8_t tag;
    uint16_t source_addr;
    uint16_t next_hop;
    uint16_t offset;
    uint8_t data; // first data byte
} wcom_ipv4_msg_state_t;

typedef struct{
    bool secure_frame;
} wcom_ipv4_tx_options_t;

typedef struct{
	uint8_t flags;
	uint8_t tag;	
	uint16_t size;
	uint16_t offset;
} wcom_ipv4_frag_header_t;

typedef struct{
    uint16_t forward_cost;
    uint8_t hop_count;
    uint8_t next_hop;
} wcom_ipv4_source_route_header_t;
#define WCOM_IPv4_ROUTE_HEADER_SIZE( hops ) ( sizeof(wcom_ipv4_source_route_header_t) + \
                                             ( sizeof(uint16_t) * hops ) ) 

typedef struct{
    uint32_t replay_counter;
    uint8_t auth_tag[CRYPT_AUTH_TAG_SIZE];
} wcom_ipv4_auth_header_t;

typedef struct{
	ip_addr_t source_addr;
	uint8_t tag;
	uint8_t age;
} wcom_ipv4_replay_cache_entry_t;

void wcom_ipv4_v_init( void );

void wcom_ipv4_v_parse_header( uint32_t header, 
                               wcom_ipv4_frag_header_t *parsed_header );

uint32_t wcom_ipv4_u32_build_header( uint8_t flags, 
                                     uint8_t tag, 
                                     uint16_t size, 
                                     uint16_t offset );

bool wcom_ipv4_b_add_to_replay_cache( ip_addr_t source_addr, uint8_t tag );

void wcom_ipv4_v_received_mac_frame( wcom_mac_addr_t *addr, 
                                     wcom_mac_rx_options_t *options,
                                     uint8_t *data, 
                                     uint8_t len );

bool wcom_ipv4_b_busy( void );

int8_t wcom_ipv4_i8_send_packet( netmsg_t netmsg );






#endif
