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

#ifndef _WCOM_TIME_H
#define _WCOM_TIME_H

#include "wcom_mac.h"

#define WCOM_TIME_FLAGS_NTP_SYNC        0x01
#define WCOM_TIME_FLAGS_INITIAL_SYNC    0x02
#define WCOM_TIME_FLAGS_SYNC            0x04

#define WCOM_TIME_SOURCE_GATEWAY        127

#define WCOM_TIME_SYNC_DRIFT_INIT_TIME  4
#define WCOM_TIME_SYNC_INIT_RANDOMNESS  6
#define WCOM_TIME_SYNC_INTERVAL_MIN     120
#define WCOM_TIME_SYNC_RANDOMNESS       0
#define WCOM_TIME_SYNC_LOSS_SECONDS     480

// NTP timestamp
typedef struct{
    uint32_t seconds;
    uint32_t fraction;
} ntp_ts_t;

typedef struct{
    uint8_t flags;
    uint16_t source_addr;
    uint8_t clk_source;
    uint8_t depth;
    uint8_t sequence;
    int32_t drift;
    uint32_t local_time;
    uint32_t network_time;
    ntp_ts_t ntp_time;
} wcom_time_info_t;


#define WCOM_TIME_MSG_TYPE_TIMESTAMP    1
typedef struct{
    uint8_t type;
    uint8_t flags;
    uint8_t depth;
    uint8_t clk_source;
    uint8_t sequence;
    uint32_t timestamp;
    ntp_ts_t ntp_time;
    uint8_t reserved[8];
} wcom_time_msg_timestamp_t;

#define WCOM_TIME_MSG_TYPE_REQUEST      2
typedef struct{
    uint8_t type;
} wcom_time_msg_request;


void wcom_time_v_init( void );

uint8_t wcom_time_u8_get_flags( void );
void wcom_time_v_get_info( wcom_time_info_t *info );
bool wcom_time_b_sync( void );
bool wcom_time_b_ntp_sync( void );

void wcom_time_v_send_ts( uint16_t dest_addr );
void wcom_time_v_reset( void );

uint32_t wcom_time_u32_elapsed_local( uint32_t local );
uint32_t wcom_time_u32_compensated_network_time( uint32_t local );
uint32_t wcom_time_u32_get_network_time( void );
uint32_t wcom_time_u32_get_network_time_ms( void );
ntp_ts_t wcom_time_t_get_ntp_time( void );

void wcom_time_v_sync( 
    uint16_t source_addr, 
    uint8_t depth, 
    uint8_t clk_source, 
    uint8_t sequence,
    uint32_t local_timestamp,
    uint32_t network_timestamp,
    ntp_ts_t ntp_time );

void wcom_time_v_receive_msg( wcom_mac_addr_t *addr, 
                              wcom_mac_rx_options_t *options, 
                              uint8_t *data, 
                              uint8_t len );


#endif

