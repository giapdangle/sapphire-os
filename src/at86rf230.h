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

#ifndef _AT86RF230_H
#define _AT86RF230_H

#include "target.h"
#include "system.h"
#include "threading.h"

#define RF_RX_BUFFER_SIZE       4
#define RF_PLL_CAL_INTERVAL		30000

#define SIGNAL_RF_RECEIVE       SIGNAL_SYS_1

// maximum frame size
// DO NOT CHANGE THIS!!!
#define RF_MAX_FRAME_SIZE 		127

#define RF_LOWEST_CHANNEL 		11
#define RF_HIGHEST_CHANNEL 		26


#define TRX24_REG(a) (uint16_t)&a

#define TRX_STATUS_MSK  0x1F
#define TRX_STATUS_SHFT 0

// TRX_STATE 0x02 bits
#define TRAC_STATUS_MSK		0b11100000
#define TRAC_STATUS_SHFT	5
#define TRX_CMD_MSK			0b00011111
#define TRX_CMD_SHFT		0

// trac status codes:
#define TRAC_STATUS_SUCCESS					0
#define TRAC_STATUS_CHANNEL_ACCESS_FAILURE	3
#define TRAC_STATUS_NO_ACK					5


// PHY_TX_PWR 0x05 bits
#define TX_PWR_MSK			0b00001111
#define TX_PWR_SHFT			0

// PHY_RSSI 0x06 bits:
#define RSSI_MSK			0b00011111
#define RSSI_SHFT			0
#define RND_MSK             0b01100000
#define RND_SHFT			5

// cca mode codes:
#define MODE_1_THRESH		1
#define MODE_2_CS			2
#define MODE_3_THRESH_CS	3

// PHY_CC_CCA 0x08 bits:
//#define CCA_REQUEST			7
#define CCA_MODE_MSK		0b01100000
#define CCA_MODE_SHFT		6
#define CHANNEL_MSK			0b00011111
#define CHANNEL_SHFT		0

// CCA_THRES 0x09
#define CCA_CS_THRES_MSK	0b11110000
#define CCA_CS_THRES_SHFT	4
#define CCA_ED_THRES_MSK	0b00001111
#define CCA_ED_THRES_SHFT	0

// XAH_CTRL_0
#define XAH_FRAME_TRIES_MSK     0b11110000
#define XAH_FRAME_TRIES_SHFT    4
#define XAH_CSMA_TRIES_MSK      0b00001110
#define XAH_CSMA_TRIES_SHFT     1

typedef uint8_t cca_status_t8;
#define CCA_STATUS_IN_PROGRESS  0
#define CCA_STATUS_CHANNEL_BUSY 1
#define CCA_STATUS_CHANNEL_IDLE 2
#define CCA_STATUS_TIMEOUT 3
#define CCA_STATUS_NOT_IN_RX_MODE 4
#define CCA_STATUS_PLL_OFF 5

// CCA request timeout in us
#define CCA_REQUEST_TIMEOUT 200


// Data rates
#define RF_DATA_RATE_250	0
#define RF_DATA_RATE_500	1
#define RF_DATA_RATE_1000	2
#define RF_DATA_RATE_2000	3

#define RF_DATA_RATE_MSK	0b00000011

// receiver sensitivity
#define RF_RX_SENS_MSK		0b00001111

// receiver auto ack time
#define RF_AACK_TIME_192us	0
#define RF_AACK_TIME_32us	1

#define RF_AACK_TIME_MSK	0b00000001

// transmit channel selections
#define RF_TX_CHANNEL_LOWEST        LOWEST_CHANNEL
#define RF_TX_CHANNEL_HIGHEST       HIGHEST_CHANNEL

typedef uint8_t rf_mode_t8;
#define RF_MODE_NORMAL              0
#define RF_MODE_PROMISCUOUS         1
#define RF_MODE_TX_ONLY             2
#define RF_MODE_SLEEP               3

typedef uint8_t rf_tx_mode_t8;
#define RF_TX_MODE_AUTO_RETRY       0
#define RF_TX_MODE_BASIC            1

typedef uint8_t rf_tx_status_t8;
#define RF_TRANSMIT_STATUS_OK               0
#define RF_TRANSMIT_STATUS_BUSY             1
#define RF_TRANSMIT_STATUS_FAILED_NO_ACK    2
#define RF_TRANSMIT_STATUS_FAILED_CCA       3

typedef struct{
    uint8_t len;
    uint8_t lqi;
    uint8_t ed;
    uint32_t timestamp;
    uint8_t data[RF_MAX_FRAME_SIZE];
} rx_frame_buf_t;

// prototypes:
uint8_t rf_u8_init( void );

void rf_v_set_mode( rf_mode_t8 mode );
rf_mode_t8 rf_u8_get_mode( void );

void rf_v_set_auto_ack_time( uint8_t time );
void rf_v_set_rx_sensitivity( uint8_t sens );
void rf_v_set_data_rate( uint8_t rate );
void rf_v_set_internal_state( uint8_t mode );
uint8_t rf_u8_get_internal_state( void );
uint8_t rf_u8_get_tx_status( void ); // name conflict
void rf_v_set_power( uint8_t power );

void rf_v_set_channel( uint8_t channel );
uint8_t rf_u8_get_channel( void );
uint8_t rf_u8_get_part_num( void );
uint8_t rf_u8_get_man_id0( void );
uint8_t rf_u8_get_man_id1( void );
uint8_t rf_u8_get_version_num( void );
void rf_v_set_short_addr( uint16_t addr );
void rf_v_set_long_addr( uint64_t long_address );
void rf_v_set_pan_id( uint16_t pan_id );
uint8_t rf_u8_get_random( void );

void rf_v_set_cca_mode( uint8_t mode );
void rf_v_set_cca_ed_threshold( uint8_t threshold );
void rf_v_set_be( uint8_t min_be, uint8_t max_be );
void rf_v_set_csma_tries( uint8_t tries );
void rf_v_set_frame_retries( uint8_t tries );

void rf_v_calibrate_pll( void );
void rf_v_calibrate_delay( void );

void rf_v_transmit( void );

void rf_v_write_frame_buf( uint8_t length, const uint8_t *data );
uint8_t rf_u8_read_frame_buf( uint8_t max_length, uint8_t *ptr );

// transmit api:
int8_t rf_i8_request_transmit_mode( rf_tx_mode_t8 mode );
rf_tx_status_t8 rf_u8_get_transmit_status( void );


void rf_v_sleep( void );
void rf_v_wakeup( void );
bool rf_b_is_sleeping( void );


#endif


