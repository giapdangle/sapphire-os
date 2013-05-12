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

#ifndef _WCOM_NEIGHBORS_H
#define _WCOM_NEIGHBORS_H

#include "at86rf230.h"
#include "ip.h"
#include "crypt.h"
#include "wcom_mac.h"

#define WCOM_NEIGHBOR_PROTOCOL_VERSION  1

#define WCOM_CHANNEL_SCAN_BEACON_WAIT   50 // in milliseconds
#define WCOM_CHANNEL_RESET_WAIT         20 // in seconds

#define WCOM_BEACON_INTERVAL_MIN        1
#define WCOM_BEACON_INTERVAL_MAX        32

#define WCOM_NEIGHBOR_MAX_AGE_NEW       30 // in seconds
#define WCOM_NEIGHBOR_MAX_AGE           90 // in seconds

#define WCOM_NEIGHBOR_MAX_DEPTH         8

#define WCOM_NEIGHBOR_WHITE_BIT_RSSI    1
#define WCOM_NEIGHBOR_WHITE_BIT_ETX     24

#define WCOM_NEIGHBOR_DROP_ETX          96

#define WCOM_NEIGHBOR_FLAGS_ROUTER      0x0001
#define WCOM_NEIGHBOR_FLAGS_GATEWAY     0x0002
#define WCOM_NEIGHBOR_FLAGS_DOWNSTREAM  0x0004
#define WCOM_NEIGHBOR_FLAGS_UPSTREAM    0x0008
#define WCOM_NEIGHBOR_FLAGS_FULL        0x0010
#define WCOM_NEIGHBOR_FLAGS_NO_JOIN     0x0020
#define WCOM_NEIGHBOR_FLAGS_TIME_SYNC   0x0040
#define WCOM_NEIGHBOR_FLAGS_JOIN        0x0080
#define WCOM_NEIGHBOR_FLAGS_NEW         0x0100


typedef struct{
    uint16_t flags;
    ip_addr_t ip;
    uint16_t short_addr;
    uint8_t iv[CRYPT_KEY_SIZE];
    uint32_t replay_counter;  
    uint8_t lqi;
    uint8_t rssi;
    uint8_t prr;
    uint8_t etx;
    uint8_t delay;
    uint8_t traffic_accumulator;
    uint8_t traffic_avg;
    uint8_t age;
} wcom_neighbor_t; // 36 bytes

#define WCOM_NEIGHBOR_MSG_TYPE_BEACON       1
typedef struct{
    uint8_t type;
    uint8_t version;
    uint16_t flags;
    ip_addr_t ip;
    uint16_t upstream;
    uint8_t depth;
    uint8_t reserved[16];
    uint32_t counter;
    uint8_t auth_tag[CRYPT_AUTH_TAG_SIZE];
} wcom_msg_beacon_t;

#define WCOM_NEIGHBOR_MSG_TYPE_FLASH        2
typedef struct{
    uint8_t type;
    uint8_t version;
    uint64_t challenge;
    uint8_t iv[CRYPT_KEY_SIZE];
    uint8_t auth_tag[CRYPT_AUTH_TAG_SIZE];
} wcom_msg_flash_t;

#define WCOM_NEIGHBOR_MSG_TYPE_THUNDER      3
typedef struct{
    uint8_t type;
    uint8_t version;
    uint64_t response;
    uint8_t iv[CRYPT_KEY_SIZE];
    uint32_t counter;
    uint8_t auth_tag[CRYPT_AUTH_TAG_SIZE];
} wcom_msg_thunder_t;

#define WCOM_NEIGHBOR_MSG_TYPE_EVICT        4
typedef struct{
    uint8_t type;
    uint8_t version;
    uint32_t counter;
    uint8_t auth_tag[CRYPT_AUTH_TAG_SIZE];
} wcom_msg_evict_t;





bool wcom_neighbors_b_is_neighbor( uint16_t short_addr );
ip_addr_t wcom_neighbors_a_get_ip( uint16_t short_addr );
uint16_t wcom_neighbors_u16_get_short( ip_addr_t ip );
uint16_t wcom_neighbors_u16_get_upstream( void );
uint8_t wcom_neighbors_u8_get_depth( void );
uint8_t wcom_neighbors_u8_get_beacon_interval( void );
uint8_t wcom_neighbors_u8_get_flags( uint16_t short_addr );
uint16_t wcom_neighbors_u16_get_gateway( void );

uint8_t wcom_neighbors_u8_get_channel( uint16_t short_addr );
wcom_neighbor_t *wcom_neighbors_p_get_neighbor( uint16_t short_addr );
uint8_t wcom_neighbors_u8_get_etx( uint16_t short_addr );
uint8_t wcom_neighbors_u8_get_cost( uint16_t short_addr );

void wcom_neighbors_v_received_from( uint16_t short_addr, wcom_mac_rx_options_t *options );
void wcom_neighbors_v_sent_to( uint16_t short_addr );
void wcom_neighbors_v_tx_ack( uint16_t short_addr );
void wcom_neighbors_v_tx_failure( uint16_t short_addr );
void wcom_neighbors_v_delay( uint16_t short_addr, uint16_t delay );

void wcom_neighbors_v_receive_msg( wcom_mac_addr_t *addr, 
                                   wcom_mac_rx_options_t *options, 
                                   uint8_t *data, 
                                   uint8_t len );

void wcom_neighbors_v_flush( void );

void wcom_neighbors_v_init( void );


#endif


