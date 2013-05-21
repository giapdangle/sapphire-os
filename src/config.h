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

#ifndef _CONFIG_H
#define _CONFIG_H

#include "fs.h"
#include "threading.h"
#include "target.h"
#include "eeprom.h"
#include "ip.h"
#include "types.h"
#include "keyvalue.h"
#include "crypt.h"

#define CFG_VERSION                     11

#define CFG_KEY_SIZE                    ( CRYPT_KEY_SIZE )
#define CFG_STR_LEN                     ( SAPPHIRE_TYPE_MAX_LEN )
#define CFG_INDEX_SIZE                  16

// error log section size:
#define CFG_FILE_SIZE_ERROR_LOG         512

// error log section start:
#define CFG_FILE_ERROR_LOG_START        ( EE_ARRAY_SIZE - CFG_FILE_SIZE_ERROR_LOG )

// main config section size
#define CFG_FILE_MAIN_SIZE              3072

// main config section start
#define CFG_FILE_MAIN_START             ( 16 ) // leave room for "Sapphire" ID string
#define CFG_FILE_MAIN_END               ( CFG_FILE_MAIN_START + CFG_FILE_MAIN_SIZE )


// secure section
#define CFG_FILE_SECURE_SIZE            256
#define CFG_FILE_SECURE_START           CFG_FILE_MAIN_END
#define CFG_FILE_SECURE_END             ( CFG_FILE_SECURE_START + CFG_FILE_SECURE_SIZE );

#define CFG_EXTRA_PARAM_START           128
#define CFG_EXTRA_PARAM_END             254
#define CFG_PARAM_EMPTY_BLOCK           255


#define CFG_PARAM_VERSION 	    				0

#define CFG_PARAM_USER_NAME 					1

#define CFG_PARAM_IP_ADDRESS 					2
#define CFG_PARAM_IP_SUBNET_MASK				3
#define CFG_PARAM_DNS_SERVER                    4
#define CFG_PARAM_INTERNET_GATEWAY              34

#define CFG_PARAM_802_15_4_MAC_ADDRESS			5
#define CFG_PARAM_802_15_4_SHORT_ADDRESS		6
#define CFG_PARAM_802_15_4_PAN_ID				7

#define CFG_PARAM_ENABLE_ROUTING				8
#define CFG_PARAM_ENABLE_WCOM_ACK_REQUEST       12

#define CFG_PARAM_WCOM_MIN_BE                   13
#define CFG_PARAM_WCOM_MAX_BE                   14
#define CFG_PARAM_WCOM_RX_SENSITIVITY           15

#define CFG_PARAM_WCOM_RSSI_FILTER              17
#define CFG_PARAM_WCOM_ETX_FILTER               18
#define CFG_PARAM_WCOM_TRAFFIC_FILTER           19

#define CFG_PARAM_DEVICE_ID                     20

#define CFG_PARAM_WCOM_MAX_TX_POWER             27
#define CFG_PARAM_WCOM_CSMA_TRIES               28
#define CFG_PARAM_WCOM_TX_HW_TRIES              29
#define CFG_PARAM_WCOM_TX_SW_TRIES              30
#define CFG_PARAM_WCOM_CCA_MODE                 31
#define CFG_PARAM_WCOM_CCA_THRESHOLD            32

#define CFG_PARAM_FIRMWARE_LOAD_COUNT           35
#define CFG_PARAM_MAX_LOG_SIZE                  36

#define CFG_PARAM_WCOM_MAX_PROV_LIST            37

#define CFG_PARAM_WCOM_ADAPTIVE_CCA             38
#define CFG_PARAM_WCOM_ADAPTIVE_CCA_RES         39
#define CFG_PARAM_WCOM_ADAPTIVE_CCA_MIN_BE      40
#define CFG_PARAM_WCOM_ADAPTIVE_CCA_MAX_BE      41

#define CFG_PARAM_WCOM_MAX_NEIGHBORS            42
#define CFG_PARAM_MAX_ROUTES                    43
#define CFG_PARAM_MAX_ROUTE_DISCOVERIES         44

#define CFG_PARAM_ENABLE_TIME_SYNC              45

#define CFG_PARAM_MAX_KV_NOTIFICATIONS          46

#define CFG_PARAM_MAX_KV_SUBSCRIPTIONS          47

#define CFG_PARAM_MANUAL_IP_ADDRESS 		    48
#define CFG_PARAM_MANUAL_SUBNET_MASK			49
#define CFG_PARAM_MANUAL_DNS_SERVER             50
#define CFG_PARAM_MANUAL_INTERNET_GATEWAY       51

#define CFG_PARAM_HEARTBEAT_INTERVAL            52

#define CFG_PARAM_ENABLE_MANUAL_IP              53

#define CFG_PARAM_KEY_VALUE_SERVER              54
#define CFG_PARAM_KEY_VALUE_SERVER_PORT         55


// Key IDs
#define CFG_KEY_WCOM_AUTH                       0
#define CFG_KEY_WCOM_ENC                        1
#define CFG_MAX_KEYS                            2


typedef struct{
    ip_addr_t ip;
    ip_addr_t subnet;
    ip_addr_t dns_server;
    ip_addr_t internet_gateway;
} cfg_ip_config_t;

typedef struct{
    char log[CFG_FILE_SIZE_ERROR_LOG];
} cfg_error_log_t;


int8_t cfg_i8_kv_handler( 
    kv_op_t8 op,
    kv_grp_t8 group,
    kv_id_t8 id,
    void *data,
    uint16_t len );

uint16_t cfg_u16_total_blocks( void );
uint16_t cfg_u16_free_blocks( void );

void cfg_v_set( uint8_t parameter, void *value );
int8_t cfg_i8_get( uint8_t parameter, void *value );

void cfg_v_set_security_key( uint8_t key_id, const uint8_t key[CFG_KEY_SIZE] );
void cfg_v_get_security_key( uint8_t key_id, uint8_t key[CFG_KEY_SIZE] );

void cfg_v_set_ip_config( cfg_ip_config_t *_ip_config );

bool cfg_b_ip_configured( void );
bool cfg_b_manual_ip( void );
bool cfg_b_wcom_configured( void );
extern bool cfg_b_is_gateway( void ) __attribute__((weak));

bool cfg_b_get_boolean( uint8_t parameter );
uint16_t cfg_u16_get_short_addr( void );

void cfg_v_default_all( void );

void cfg_v_write_config( void );
void cfg_v_init( void );

void cfg_v_write_error_log( cfg_error_log_t *log );
void cfg_v_read_error_log( cfg_error_log_t *log );
void cfg_v_erase_error_log( void );
uint16_t cfg_u16_error_log_size( void );


#endif


