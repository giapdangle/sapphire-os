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

#ifndef _WCOM_MAC_H
#define _WCOM_MAC_H

#include "at86rf230.h"
#include "netmsg.h"
#include "crypt.h"
#include "list.h"

// number of frames to cache in the replay cache.
// this needs to be less than 256 since the entries will never time out.
// that way, the entries will cycle around and be cleared before a source
// ever repeats a sequence number.
#define WCOM_MAC_REPLAY_CACHE_ENTRIES   8

#define WCOM_MAC_MAX_QUEUED_FRAMES      8

#define WCOM_MAC_ADDR_BROADCAST     0xffff

#define WCOM_MAC_PAN_BROADCAST      0xffff
#define WCOM_MAC_PAN_ID_NOT_PRESENT 0
#define WCOM_MAC_ADDR_NOT_PRESENT   0

typedef struct{
	uint64_t dest_addr;
	uint8_t dest_mode;
	uint16_t dest_pan_id;
	uint64_t source_addr;
	uint8_t source_mode;
	uint16_t source_pan_id;
} wcom_mac_addr_t;

#define WCOM_MAC_ADDR_MODE_NONE 0
#define WCOM_MAC_ADDR_MODE_SHORT 2
#define WCOM_MAC_ADDR_MODE_LONG 3

typedef uint16_t wcom_mac_fcf_t16; // frame control field

#define WCOM_MAC_FCF_TYPE				0b0000000000000111
#define WCOM_MAC_FCF_SEC				0b0000000000001000
#define WCOM_MAC_FCF_FP					0b0000000000010000
#define WCOM_MAC_FCF_ACK_REQ			0b0000000000100000
#define WCOM_MAC_FCF_INTRA_PAN			0b0000000001000000

#define WCOM_MAC_FCF_DEST_ADDR_MODE		0b0000110000000000
#define WCOM_MAC_FCF_SOURCE_ADDR_MODE	0b1100000000000000

// types:
#define WCOM_MAC_FCF_TYPE_DATA			0b0000000000000001
#define WCOM_MAC_FCF_TYPE_ACK			0b0000000000000010

// addressing modes
#define WCOM_MAC_FCF_DEST_ADDR_NONE		0b0000000000000000
#define WCOM_MAC_FCF_DEST_ADDR_SHORT	0b0000100000000000
#define WCOM_MAC_FCF_DEST_ADDR_LONG		0b0000110000000000

#define WCOM_MAC_FCF_SOURCE_ADDR_NONE	0b0000000000000000
#define WCOM_MAC_FCF_SOURCE_ADDR_SHORT	0b1000000000000000
#define WCOM_MAC_FCF_SOURCE_ADDR_LONG	0b1100000000000000

// protocol control field (non standard!)
typedef uint8_t wcom_mac_pcf_t8;
#define WCOM_MAC_PROTOCOL_MASK          0b00001111
#define WCOM_MAC_PROTOCOL_RAW           0b00000000
#define WCOM_MAC_PROTOCOL_IPv4          0b00000001
#define WCOM_MAC_PROTOCOL_NEIGHBOR      0b00000010
#define WCOM_MAC_PROTOCOL_TIMESYNC      0b00000011
#define WCOM_MAC_SECURITY_AUTH          0b10000000
#define WCOM_MAC_SECURITY_ENC           0b01000000

#define WCOM_MAC_PROTOCOL(field)        ( field & WCOM_MAC_PROTOCOL_MASK )

// security header (NOT 802.15.4 compliant)
typedef struct{
    uint32_t replay_counter;
} wcom_mac_auth_header_t;

typedef uint8_t wcom_mac_tx_status_t8;
#define WCOM_MAC_TX_STATUS_IDLE 0 // transmit thread is idle
#define WCOM_MAC_TX_STATUS_BUSY 1 // radio is busy, could not transmit
#define WCOM_MAC_TX_STATUS_OK 2 // frame sent OK
#define WCOM_MAC_TX_STATUS_FAILED 3 // transmission failed

// ack frame length
#define WCOM_ACK_FRAME_LEN 5

typedef struct{
	bool ack_request;
    bool secure_frame;
    uint8_t protocol;
} wcom_mac_tx_options_t;

typedef struct{
    uint8_t protocol;
    bool security_enabled;
    uint8_t ed;
    uint8_t lqi;
    uint32_t timestamp;
} wcom_mac_rx_options_t;

typedef struct{
    uint16_t source_addr;
    uint8_t sequence;
} wcom_mac_replay_cache_entry_t;


typedef list_node_t wcom_mac_msg_t;
typedef struct{
    wcom_mac_addr_t addr;
    wcom_mac_tx_options_t tx_options;
    wcom_mac_tx_status_t8 tx_status;
    bool auto_release;
    uint8_t frame_len;
    uint8_t data; // first data byte
} wcom_mac_msg_state_t;


uint8_t wcom_mac_u8_decode_address( const uint8_t *data, 
                                    wcom_mac_addr_t *addr );

void wcom_mac_v_rx_handler( rx_frame_buf_t *frame );

void wcom_mac_v_init( void );
uint8_t wcom_mac_u8_tx_q_size( void );
uint8_t wcom_mac_u8_get_local_be( void );

uint8_t wcom_mac_u8_calc_mac_header_length( wcom_mac_addr_t *addr, 
                                            wcom_mac_tx_options_t *options );

uint8_t wcom_mac_u8_calc_max_data_length( wcom_mac_addr_t *addr, 
                                          wcom_mac_tx_options_t *options );

void wcom_mac_v_init_addr( uint16_t dest_addr, wcom_mac_addr_t *addr );

uint8_t wcom_mac_u8_build_frame( wcom_mac_addr_t *addr, 
                                 wcom_mac_tx_options_t *options, 
                                 uint16_t type,
                                 uint8_t *data, 
                                 uint8_t length,
                                 uint8_t *buf );

bool wcom_mac_b_busy( void );

wcom_mac_msg_t wcom_mac_m_create_tx_msg( wcom_mac_addr_t *addr, 
                                         wcom_mac_tx_options_t *options, 
                                         uint16_t type,
                                         uint8_t *data, 
                                         uint8_t length,
                                         bool auto_release );

bool wcom_mac_b_msg_done( wcom_mac_msg_t msg );

uint16_t wcom_mac_u16_get_len( wcom_mac_msg_t msg );

void wcom_mac_v_set_tx_status( wcom_mac_msg_t msg, 
                               wcom_mac_tx_status_t8 tx_status );

wcom_mac_tx_status_t8 wcom_mac_u8_get_tx_status( wcom_mac_msg_t msg );

bool wcom_mac_b_is_autorelease( wcom_mac_msg_t msg );

void wcom_mac_v_release_msg( wcom_mac_msg_t msg );

int8_t wcom_mac_i8_transmit_frame( wcom_mac_addr_t *addr, 
                                   wcom_mac_tx_options_t *options, 
                                   uint16_t type,
                                   uint8_t *data, 
                                   uint8_t length );

void wcom_mac_v_mute( void );


#endif

