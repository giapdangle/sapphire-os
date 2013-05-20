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


#include "cpu.h"

#include "system.h"

#include "at86rf230.h"
#include "fs.h"
#include "memory.h"
#include "sockets.h"
#include "threading.h"
#include "random.h"
#include "timers.h"
#include "target.h"

#include "crc.h"
#include "config.h"
#include "fs.h"
#include "io.h"
#include "wcom_mac.h"
#include "keyvalue.h"

//#define NO_LOGGING
#include "logging.h"

#include <stddef.h>
#include <string.h>



KV_SECTION_META kv_meta_t sys_cfg_kv[] = {
    { KV_GROUP_SYS_CFG, CFG_PARAM_VERSION,                  SAPPHIRE_TYPE_UINT16,      0,                   0, cfg_i8_kv_handler,  "version" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_USER_NAME,                SAPPHIRE_TYPE_STRING128,   0,                   0, cfg_i8_kv_handler,  "name" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_IP_ADDRESS,               SAPPHIRE_TYPE_IPv4,        KV_FLAGS_READ_ONLY,  0, cfg_i8_kv_handler,  "ip" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_IP_SUBNET_MASK,           SAPPHIRE_TYPE_IPv4,        KV_FLAGS_READ_ONLY,  0, cfg_i8_kv_handler,  "subnet" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_DNS_SERVER,               SAPPHIRE_TYPE_IPv4,        KV_FLAGS_READ_ONLY,  0, cfg_i8_kv_handler,  "dns_server" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_802_15_4_MAC_ADDRESS,     SAPPHIRE_TYPE_MAC64,       0,                   0, cfg_i8_kv_handler,  "mac_addr" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_802_15_4_SHORT_ADDRESS,   SAPPHIRE_TYPE_UINT16,      0,                   0, cfg_i8_kv_handler,  "short_addr" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_802_15_4_PAN_ID,          SAPPHIRE_TYPE_UINT16,      0,                   0, cfg_i8_kv_handler,  "pan_id" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_ENABLE_ROUTING,           SAPPHIRE_TYPE_BOOL,        0,                   0, cfg_i8_kv_handler,  "enable_routing" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_ENABLE_WCOM_ACK_REQUEST,  SAPPHIRE_TYPE_BOOL,        0,                   0, cfg_i8_kv_handler,  "enable_ack_request" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_WCOM_MIN_BE,              SAPPHIRE_TYPE_UINT16,      0,                   0, cfg_i8_kv_handler,  "wcom_min_be" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_WCOM_MAX_BE,              SAPPHIRE_TYPE_UINT16,      0,                   0, cfg_i8_kv_handler,  "wcom_max_be" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_WCOM_RX_SENSITIVITY,      SAPPHIRE_TYPE_UINT16,      0,                   0, cfg_i8_kv_handler,  "wcom_rx_sensitivity" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_WCOM_RSSI_FILTER,         SAPPHIRE_TYPE_UINT16,      0,                   0, cfg_i8_kv_handler,  "wcom_rssi_filter" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_WCOM_ETX_FILTER,          SAPPHIRE_TYPE_UINT16,      0,                   0, cfg_i8_kv_handler,  "wcom_etx_filter" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_WCOM_TRAFFIC_FILTER,      SAPPHIRE_TYPE_UINT16,      0,                   0, cfg_i8_kv_handler,  "wcom_traffic_filter" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_DEVICE_ID,                SAPPHIRE_TYPE_MAC64,       0,                   0, cfg_i8_kv_handler,  "device_id" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_WCOM_MAX_TX_POWER,        SAPPHIRE_TYPE_UINT16,      0,                   0, cfg_i8_kv_handler,  "wcom_max_tx_power" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_WCOM_CSMA_TRIES,          SAPPHIRE_TYPE_UINT16,      0,                   0, cfg_i8_kv_handler,  "wcom_csma_tries" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_WCOM_TX_HW_TRIES,         SAPPHIRE_TYPE_UINT16,      0,                   0, cfg_i8_kv_handler,  "wcom_tx_hw_tries" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_WCOM_TX_SW_TRIES,         SAPPHIRE_TYPE_UINT16,      0,                   0, cfg_i8_kv_handler,  "wcom_tx_sw_tries" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_WCOM_CCA_MODE,            SAPPHIRE_TYPE_UINT16,      0,                   0, cfg_i8_kv_handler,  "wcom_cca_mode" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_WCOM_CCA_THRESHOLD,       SAPPHIRE_TYPE_UINT16,      0,                   0, cfg_i8_kv_handler,  "wcom_cca_threshold" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_INTERNET_GATEWAY,         SAPPHIRE_TYPE_IPv4,        KV_FLAGS_READ_ONLY,  0, cfg_i8_kv_handler,  "internet_gateway" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_FIRMWARE_LOAD_COUNT,      SAPPHIRE_TYPE_UINT16,      0,                   0, cfg_i8_kv_handler,  "fw_load_count" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_MAX_LOG_SIZE,             SAPPHIRE_TYPE_UINT16,      0,                   0, cfg_i8_kv_handler,  "max_log_size" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_WCOM_MAX_PROV_LIST,       SAPPHIRE_TYPE_UINT16,      0,                   0, cfg_i8_kv_handler,  "wcom_max_prov" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_WCOM_ADAPTIVE_CCA,        SAPPHIRE_TYPE_BOOL,        0,                   0, cfg_i8_kv_handler,  "wcom_adaptive_cca" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_WCOM_ADAPTIVE_CCA_RES,    SAPPHIRE_TYPE_UINT16,      0,                   0, cfg_i8_kv_handler,  "wcom_adaptive_cca_resolution" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_WCOM_ADAPTIVE_CCA_MIN_BE, SAPPHIRE_TYPE_UINT16,      0,                   0, cfg_i8_kv_handler,  "wcom_adaptive_cca_min_be" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_WCOM_ADAPTIVE_CCA_MAX_BE, SAPPHIRE_TYPE_UINT16,      0,                   0, cfg_i8_kv_handler,  "wcom_adaptive_cca_max_be" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_WCOM_MAX_NEIGHBORS,       SAPPHIRE_TYPE_UINT16,      0,                   0, cfg_i8_kv_handler,  "wcom_max_neighbors" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_MAX_ROUTES,               SAPPHIRE_TYPE_UINT16,      0,                   0, cfg_i8_kv_handler,  "max_routes" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_MAX_ROUTE_DISCOVERIES,    SAPPHIRE_TYPE_UINT16,      0,                   0, cfg_i8_kv_handler,  "max_route_discoveries" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_ENABLE_TIME_SYNC,         SAPPHIRE_TYPE_BOOL,        0,                   0, cfg_i8_kv_handler,  "enable_time_sync" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_MANUAL_IP_ADDRESS,        SAPPHIRE_TYPE_IPv4,        0,                   0, cfg_i8_kv_handler,  "manual_ip" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_MANUAL_SUBNET_MASK,       SAPPHIRE_TYPE_IPv4,        0,                   0, cfg_i8_kv_handler,  "manual_subnet" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_MANUAL_DNS_SERVER,        SAPPHIRE_TYPE_IPv4,        0,                   0, cfg_i8_kv_handler,  "manual_dns_server" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_MANUAL_INTERNET_GATEWAY,  SAPPHIRE_TYPE_IPv4,        0,                   0, cfg_i8_kv_handler,  "manual_internet_gateway" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_ENABLE_MANUAL_IP,         SAPPHIRE_TYPE_BOOL,        0,                   0, cfg_i8_kv_handler,  "enable_manual_ip" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_KEY_VALUE_SERVER,         SAPPHIRE_TYPE_IPv4,        0,                   0, cfg_i8_kv_handler,  "key_value_server" },
    { KV_GROUP_SYS_CFG, CFG_PARAM_KEY_VALUE_SERVER_PORT,    SAPPHIRE_TYPE_UINT16,      0,                   0, cfg_i8_kv_handler,  "key_value_server_port" },
};



static cfg_ip_config_t ip_config;


#define CFG_PARAM_DATA_LEN 8
typedef struct{
    kv_id_t8 id;
    sapphire_type_t8 type;
    uint8_t block_number;
    uint8_t reserved;
    uint8_t data[CFG_PARAM_DATA_LEN];
} cfg_block_t;

#define CFG_TOTAL_BLOCKS ( ( CFG_FILE_MAIN_SIZE / sizeof(cfg_block_t) ) - 1 )

typedef struct{
    kv_id_t8 id;
    uint8_t block_number;
} cfg_index_t;

static cfg_index_t cfg_index[CFG_INDEX_SIZE];
uint8_t cfg_index_insert;


static uint16_t error_log_vfile_handler( vfile_op_t8 op, uint32_t pos, void *ptr, uint16_t len ){
    
    // the pos and len values are already bounds checked by the FS driver
    switch( op ){
        
        case FS_VFILE_OP_READ:
            ee_v_read_block( CFG_FILE_ERROR_LOG_START + pos, ptr, len );
            break;

        case FS_VFILE_OP_SIZE:
            len = cfg_u16_error_log_size(); 
            break;
        
        case FS_VFILE_OP_DELETE:
            cfg_v_erase_error_log();
            break;

        default:
            len = 0;

            break;
    }

    return len;
}

static uint16_t fw_info_vfile_handler( vfile_op_t8 op, uint32_t pos, void *ptr, uint16_t len ){
    
    // the pos and len values are already bounds checked by the FS driver
    switch( op ){
        
        case FS_VFILE_OP_READ:
            memcpy_P( ptr, (void *)FW_INFO_ADDRESS + pos, len );
            break;

        case FS_VFILE_OP_SIZE:
            len = sizeof(fw_info_t); 
            break;

        default:
            len = 0;

            break;
    }

    return len;
}

static uint16_t hw_info_vfile_handler( vfile_op_t8 op, uint32_t pos, void *ptr, uint16_t len ){
    
    // read hardware info from sys module
    hw_info_t hw_info;
    sys_v_get_hw_info( &hw_info );

    // the pos and len values are already bounds checked by the FS driver
    switch( op ){
        
        case FS_VFILE_OP_READ:
            memcpy_P( ptr, (void *)&hw_info + pos, len );
            break;

        case FS_VFILE_OP_SIZE:
            len = sizeof(hw_info_t); 
            break;

        default:
            len = 0;

            break;
    }

    return len;
}

static uint16_t block_address( uint16_t block_number ){
    
    ASSERT( block_number < CFG_TOTAL_BLOCKS );
    
    return ( block_number * sizeof(cfg_block_t) ) + CFG_FILE_MAIN_START;
}   

static kv_id_t8 read_block_id( uint16_t block_number ){
    
    return ee_u8_read_byte( block_address( block_number ) );
}

static kv_id_t8 read_block_number( uint16_t block_number ){
    
    return ee_u8_read_byte( block_address( block_number ) + offsetof(cfg_block_t, block_number) );
}

static void erase_block( uint16_t block_number ){
    
    // remove from index
    for( uint8_t i = 0; i < CFG_INDEX_SIZE; i++ ){
            
        if( cfg_index[i].block_number == block_number ){
            
            memset( &cfg_index[i], 0xff, sizeof(cfg_index_t) );
        }
    }

    ee_v_write_byte_blocking( block_address( block_number ), CFG_PARAM_EMPTY_BLOCK );
}

static void read_block( uint16_t block_number, cfg_block_t *block ){
    
    ee_v_read_block( block_address( block_number ), (uint8_t *)block, sizeof(cfg_block_t) );
}

static int16_t seek_block( kv_id_t8 id, uint8_t n ){
    
    // search index
    if( n == 0 ){

        for( uint8_t i = 0; i < CFG_INDEX_SIZE; i++ ){
                
            if( cfg_index[i].id == id ){
                
                return cfg_index[i].block_number;
            }
        }
    }

    for( uint16_t i = 0; i < CFG_TOTAL_BLOCKS; i++ ){
        
        if( ( read_block_id( i ) == id ) &&
            ( read_block_number( i ) == n ) ){
            
            // add to index
            if( n == 0 ){
                
                cfg_index[cfg_index_insert].id              = id;
                cfg_index[cfg_index_insert].block_number    = i;

                cfg_index_insert++;

                if( cfg_index_insert >= CFG_INDEX_SIZE ){
                    
                    cfg_index_insert = 0;
                }
            }

            return i;
        }
    }

    return -1;
}

static void write_block( uint16_t block_number, const cfg_block_t *block ){
    
    uint8_t *data = (uint8_t *)block;
    uint16_t addr = block_address( block_number );

    // write everything except the ID
    ee_v_write_block( addr + 1, data + 1, sizeof(cfg_block_t) - 1 );

    // write ID
    ee_v_write_byte_blocking( addr, block->id );
}

static int16_t get_free_block( void ){

    for( uint16_t i = 0; i < CFG_TOTAL_BLOCKS; i++ ){
        
        if( read_block_id( i ) == CFG_PARAM_EMPTY_BLOCK ){
            
            return i;
        }
    }

    return -1;
}

// scan and remove blocks that have a KV type mismatch, or are
// not listed in the KV system
static void clean_blocks( void ){
       
    cfg_block_t block;

    uint16_t blocks_present[CFG_TOTAL_BLOCKS];
    memset( blocks_present, 0, sizeof(blocks_present) );

    for( uint16_t i = 0; i < CFG_TOTAL_BLOCKS; i++ ){
        
        read_block( i, &block );
        
        // check ID
        if( block.id == CFG_PARAM_EMPTY_BLOCK ){
            
            // empty block
            continue;
        }
        
        // get type
        sapphire_type_t8 type = kv_i8_type( KV_GROUP_SYS_CFG, block.id );
        
        // check if parameter was not found in the KV system
        // (possibly a parameter was removed)
        // OR 
        // the type listed in the parameter data does not match the
        // type listed in the KV system (parameter type may have been
        // changed) 
        // OR 
        // the parameter is already in the eeprom and this is a duplicate.
        //
        if( ( type < 0 ) || 
            ( type != block.type ) ||
            ( ( blocks_present[block.id] & ( 1 << block.block_number ) ) != 0 ) ){
            
            log_v_debug_P( PSTR("Cfg check found bad block ID:%d"), block.id );

            erase_block( i );
        }
        
        // mark this parameter as present
        blocks_present[block.id] |= ( 1 << block.block_number );
    }
}

uint16_t cfg_u16_total_blocks( void ){

    return CFG_TOTAL_BLOCKS;
}

uint16_t cfg_u16_free_blocks( void ){
    
    uint16_t blocks = 0;
    
    for( uint16_t i = 0; i < CFG_TOTAL_BLOCKS; i++ ){
        
        if( read_block_id( i ) == CFG_PARAM_EMPTY_BLOCK ){
            
            blocks++;
        }
    }
    
    return blocks;
}

static void write_param( uint8_t parameter, void *value ){
    
    sapphire_type_t8 type = kv_i8_type( KV_GROUP_SYS_CFG, parameter );
    
    uint16_t len = type_u16_size( type );

    // get number of blocks for this parameter
    uint8_t n_blocks = ( len / CFG_PARAM_DATA_LEN ) + 1;
 
    // iterate through blocks
    for( uint8_t i = 0; i < n_blocks; i++ ){

        // set up new parameter block
        cfg_block_t param;
        param.id            = parameter;
        param.type          = type;
        param.block_number  = i;
        param.reserved      = 0xff;
        
        memset( param.data, 0, sizeof(param.data) );

        uint16_t copy_len = len;
        
        if( copy_len > CFG_PARAM_DATA_LEN ){
            
            copy_len = CFG_PARAM_DATA_LEN;
        }

        memcpy( param.data, value, copy_len );
        
        // get a free block
        int16_t free_block = get_free_block();

        // no blocks available, that's sad
        if( free_block < 0 ){

            sys_v_set_warnings( SYS_WARN_CONFIG_FULL );
            
            return;
        }

        // seek to old parameter
        int16_t block = seek_block( parameter, i );

        // write new block
        write_block( free_block, &param );

        // read back block
        cfg_block_t check_param;
        read_block( free_block, &check_param );

        // compare
        if( memcmp( &check_param, &param, sizeof(check_param) ) != 0 ){
            
            // block write failed
            erase_block( free_block );
            
            // back track iterator
            i--;
            
            sys_v_set_warnings( SYS_WARN_CONFIG_WRITE_FAIL );

            // retry
            continue;
        }

        // erase old block (if it exists)
        if( block >= 0 ){
            
            erase_block( block );
        }

        value += copy_len;
        len -= copy_len;
    }
}

static int8_t read_param( uint8_t parameter, void *value ){
    
    // seek to parameter
    int16_t block = seek_block( parameter, 0 );

    // get data len from kv system
    int16_t len = kv_i16_len( KV_GROUP_SYS_CFG, parameter );

    // check if KV param was found
    if( len < 0 ){
        
        return -1;
    }

    if( block < 0 ){
        
        // block not found, we're going to return all 0s for the data
        
        // set zeroes
        if( len > 0 ){
            
            memset( value, 0, len );
        }
        
        // parameter not found
        return -1;
    }
    
    // get number of blocks for this parameter
    uint8_t n_blocks = ( len / CFG_PARAM_DATA_LEN ) + 1;
    
    // iterate through blocks
    for( uint8_t i = 0; i < n_blocks; i++ ){
        
        // skip seeking first block, we already have it
        if( i != 0 ){

            // seek next block
            block = seek_block( parameter, i );
        }
        
        // check if we're missing a block
        if( block < 0 ){
            
            return -1;
        }

        cfg_block_t param;
        read_block( block, &param );

        uint16_t copy_len = len;
        
        if( copy_len > CFG_PARAM_DATA_LEN ){
            
            copy_len = CFG_PARAM_DATA_LEN;
        }
        
        // copy data to parameter
        memcpy( value, param.data, copy_len );

        value += copy_len;
        len -= copy_len;
    }
    
    return 0;
}

int8_t cfg_i8_kv_handler( 
    kv_op_t8 op,
    kv_grp_t8 group,
    kv_id_t8 id,
    void *data,
    uint16_t len )
{
    
    ASSERT( group == KV_GROUP_SYS_CFG );

    if( op == KV_OP_GET ){
        
        // ignoring return value, the config system will return 0s as
        // default data in a parameter is not present
        cfg_i8_get( id, data );
    }
    else if( op == KV_OP_SET ){
        
        cfg_v_set( id, data );
    }
    else{
        
        ASSERT( FALSE );
    }

    return 0;
}

void cfg_v_set( uint8_t parameter, void *value ){

    // IP config params are in-memory only
    // and not settable here
    if( parameter == CFG_PARAM_IP_ADDRESS ){
         
        ASSERT( FALSE );
    }
    else if( parameter == CFG_PARAM_IP_SUBNET_MASK ){

        ASSERT( FALSE );
    }
    else if( parameter == CFG_PARAM_DNS_SERVER ){
        
        ASSERT( FALSE );
    }
    else if( parameter == CFG_PARAM_INTERNET_GATEWAY ){
        
        ASSERT( FALSE );
    }
    else{
        
        write_param( parameter, value );
    }
}

int8_t cfg_i8_get( uint8_t parameter, void *value ){
    
    if( ( parameter == CFG_PARAM_IP_ADDRESS ) && ( value != 0 ) ){
        
        // read IP config from in memory state
        memcpy( value, &ip_config.ip, sizeof(ip_config.ip) );
    }
    else if( ( parameter == CFG_PARAM_IP_SUBNET_MASK ) && ( value != 0 ) ){
        
        memcpy( value, &ip_config.subnet, sizeof(ip_config.subnet) );
    }
    else if( ( parameter == CFG_PARAM_DNS_SERVER ) && ( value != 0 ) ){
        
        memcpy( value, &ip_config.dns_server, sizeof(ip_config.dns_server) );
    }
    else if( ( parameter == CFG_PARAM_INTERNET_GATEWAY ) && ( value != 0 ) ){
        
        memcpy( value, &ip_config.internet_gateway, sizeof(ip_config.internet_gateway) );
    }
    else{

        return read_param( parameter, value );
    }

    return 0;
}

void cfg_v_set_security_key( uint8_t key_id, const uint8_t key[CFG_KEY_SIZE] ){
    
    // bounds check
    if( key_id >= CFG_MAX_KEYS ){
        
        return;
    }
    
    ee_v_write_block( CFG_FILE_SECURE_START + ( key_id * CFG_KEY_SIZE ),
                      key,
                      CFG_KEY_SIZE );
}

void cfg_v_get_security_key( uint8_t key_id, uint8_t key[CFG_KEY_SIZE] ){
    
    // bounds check
    if( key_id >= CFG_MAX_KEYS ){
        
        return;
    }
    
    ee_v_read_block( CFG_FILE_SECURE_START + ( key_id * CFG_KEY_SIZE ),
                     key,
                     CFG_KEY_SIZE );
}

void cfg_v_set_ip_config( cfg_ip_config_t *_ip_config ){
    
    ip_config = *_ip_config;
}

bool cfg_b_ip_configured( void ){

    // check if IP is configured
    if( !ip_b_is_zeroes( ip_config.ip ) ){
        
        return TRUE;
    }

    return FALSE;
}

bool cfg_b_manual_ip( void ){
    
    // read manual IP config
    cfg_ip_config_t manual_ip_cfg;

    cfg_i8_get( CFG_PARAM_MANUAL_IP_ADDRESS, &manual_ip_cfg.ip );
    cfg_i8_get( CFG_PARAM_MANUAL_SUBNET_MASK, &manual_ip_cfg.subnet );
    
    // check that we have IP and subnet set at the very least
    if( ( !ip_b_is_zeroes( manual_ip_cfg.ip ) ) &&
        ( !ip_b_addr_compare( manual_ip_cfg.ip, ip_a_addr(255,255,255,255) ) ) &&
        ( !ip_b_is_zeroes( manual_ip_cfg.subnet ) ) &&
        ( !ip_b_addr_compare( manual_ip_cfg.subnet, ip_a_addr(255,255,255,255) ) ) &&
        ( cfg_b_get_boolean( CFG_PARAM_ENABLE_MANUAL_IP ) ) ){
        
        return TRUE;
    }

    return FALSE;
}

bool cfg_b_wcom_configured( void ){
    
    uint16_t pan_id;
    read_param( CFG_PARAM_802_15_4_PAN_ID, &pan_id );
    
    return ( pan_id != WCOM_MAC_PAN_ID_NOT_PRESENT );
}

bool cfg_b_is_gateway( void ){
    
    return FALSE;
}

// this is a quick read function to get boolean parameters.
// it is just a convenience for code that doesn't want to create a local
// boolean variable
// NOTE:
// this function doesn't actually check that the parameter requested is actually a boolean.
// it will return whatever the boolean value of the first byte would be.
bool cfg_b_get_boolean( uint8_t parameter ){
	
    uint8_t value[CFG_STR_LEN]; // longest possible parameter
	
    // get parameter
    cfg_i8_get( parameter, value );
    
    return value[0];    
}

// short cut function to get short address
uint16_t cfg_u16_get_short_addr( void ){
    
    uint16_t addr;
    
    cfg_i8_get( CFG_PARAM_802_15_4_SHORT_ADDRESS, &addr );

    return addr;
}

void cfg_v_set_boolean( uint8_t parameter, bool value ){
    
    cfg_v_set( parameter, &value );
}

void cfg_v_set_u16( uint8_t parameter, uint16_t value ){
    
    cfg_v_set( parameter, &value );
}

void cfg_v_set_string( uint8_t parameter, char *value ){
    
    cfg_v_set( parameter, value );
}

void cfg_v_set_string_P( uint8_t parameter, PGM_P value ){
    
    char s[CFG_STR_LEN];
    strncpy_P( s, value, sizeof(s) );

    cfg_v_set( parameter, s );
}

void cfg_v_set_ipv4( uint8_t parameter, ip_addr_t value ){
    
    cfg_v_set( parameter, &value );
}

void cfg_v_set_mac64( uint8_t parameter, uint8_t *value ){
    
    cfg_v_set( parameter, value );
}

void cfg_v_set_mac48( uint8_t parameter, uint8_t *value ){
    
    cfg_v_set( parameter, value );
}

void cfg_v_set_key128( uint8_t parameter, uint8_t *value ){
    
    cfg_v_set( parameter, value );
}


// write default values to all config items
void cfg_v_default_all( void ){
    
    // erase all the things!
    for( uint16_t i = 0; i < EE_ARRAY_SIZE; i++ ){
        
        wdt_reset();
        ee_v_write_byte_blocking( i, 0xff );
    }

    uint8_t zeroes[CFG_STR_LEN];
    memset( zeroes, 0, sizeof(zeroes) );

    cfg_v_set_string( CFG_PARAM_USER_NAME, (char *)zeroes );

    cfg_v_set_ipv4( CFG_PARAM_MANUAL_IP_ADDRESS, ip_a_addr(0,0,0,0) );
    cfg_v_set_ipv4( CFG_PARAM_MANUAL_SUBNET_MASK, ip_a_addr(0,0,0,0) );
    cfg_v_set_ipv4( CFG_PARAM_MANUAL_DNS_SERVER, ip_a_addr(0,0,0,0) );
    cfg_v_set_ipv4( CFG_PARAM_MANUAL_INTERNET_GATEWAY, ip_a_addr(0,0,0,0) );
    cfg_v_set_ipv4( CFG_PARAM_KEY_VALUE_SERVER, ip_a_addr(0,0,0,0) );

    cfg_v_set_u16( CFG_PARAM_802_15_4_PAN_ID, WCOM_MAC_PAN_ID_NOT_PRESENT );
    cfg_v_set_mac64( CFG_PARAM_802_15_4_MAC_ADDRESS, zeroes );
    cfg_v_set_u16( CFG_PARAM_802_15_4_SHORT_ADDRESS, 0 );
    
    cfg_v_set_boolean( CFG_PARAM_ENABLE_ROUTING, FALSE );
    cfg_v_set_boolean( CFG_PARAM_ENABLE_WCOM_ACK_REQUEST, FALSE );
    cfg_v_set_boolean( CFG_PARAM_ENABLE_TIME_SYNC, FALSE );
    
    cfg_v_set_mac64( CFG_PARAM_DEVICE_ID, zeroes );

    cfg_v_set_u16( CFG_PARAM_WCOM_MAX_TX_POWER, 0 ); // max power
    cfg_v_set_u16( CFG_PARAM_WCOM_CSMA_TRIES, 0 );
    cfg_v_set_u16( CFG_PARAM_WCOM_TX_HW_TRIES, 0 );
    cfg_v_set_u16( CFG_PARAM_WCOM_TX_SW_TRIES, 32 );
    cfg_v_set_u16( CFG_PARAM_WCOM_CCA_MODE, MODE_2_CS );
    cfg_v_set_u16( CFG_PARAM_WCOM_CCA_THRESHOLD, 8 );
    cfg_v_set_u16( CFG_PARAM_WCOM_MIN_BE, 3 );
    cfg_v_set_u16( CFG_PARAM_WCOM_MAX_BE, 5 );
    cfg_v_set_u16( CFG_PARAM_WCOM_RX_SENSITIVITY, 0 );
    cfg_v_set_u16( CFG_PARAM_WCOM_RSSI_FILTER, 16 );
    cfg_v_set_u16( CFG_PARAM_WCOM_ETX_FILTER, 16 );
    cfg_v_set_u16( CFG_PARAM_WCOM_TRAFFIC_FILTER, 16 );
    cfg_v_set_u16( CFG_PARAM_WCOM_MAX_PROV_LIST, 4 );
    cfg_v_set_boolean( CFG_PARAM_WCOM_ADAPTIVE_CCA, TRUE );
    cfg_v_set_u16( CFG_PARAM_WCOM_ADAPTIVE_CCA_RES, 8 );
    cfg_v_set_u16( CFG_PARAM_WCOM_ADAPTIVE_CCA_MIN_BE, 3 );
    cfg_v_set_u16( CFG_PARAM_WCOM_ADAPTIVE_CCA_MAX_BE, 8 );
    cfg_v_set_u16( CFG_PARAM_WCOM_MAX_NEIGHBORS, 8 );
    cfg_v_set_u16( CFG_PARAM_MAX_ROUTES, 8 );
    cfg_v_set_u16( CFG_PARAM_MAX_ROUTE_DISCOVERIES, 3 );
    cfg_v_set_u16( CFG_PARAM_MAX_KV_NOTIFICATIONS, 8 );
    cfg_v_set_u16( CFG_PARAM_MAX_KV_SUBSCRIPTIONS, 8 );
    cfg_v_set_u16( CFG_PARAM_MAX_LOG_SIZE, 32768 );
    cfg_v_set_u16( CFG_PARAM_HEARTBEAT_INTERVAL, 60 );

    cfg_v_set_u16( CFG_PARAM_VERSION, CFG_VERSION );


    cfg_v_set_security_key( CFG_KEY_WCOM_AUTH, zeroes );
    cfg_v_set_security_key( CFG_KEY_WCOM_ENC, zeroes );
}

// init config module
void cfg_v_init( void ){	
    
    COMPILER_ASSERT( CFG_TOTAL_BLOCKS < 256 );

    // clear index
    memset( cfg_index, 0xff, sizeof(cfg_index) );

    // run block clean algorithm
    clean_blocks();

    // create virtual files
    fs_f_create_virtual( PSTR("error_log.txt"), error_log_vfile_handler );
    fs_f_create_virtual( PSTR("fwinfo"), fw_info_vfile_handler );
    fs_f_create_virtual( PSTR("hwinfo"), hw_info_vfile_handler );
    
    // check config version
    uint16_t version;
    if( cfg_i8_get( CFG_PARAM_VERSION, &version ) < 0 ){

        // parameter not present
        cfg_v_default_all();
    }
    // version mismatch
    else if( version != CFG_VERSION ){
        
        cfg_v_default_all();
    }
    
    // check if manual IP config is set
    if( cfg_b_get_boolean( CFG_PARAM_ENABLE_MANUAL_IP ) ){

        // read manual IP config
        cfg_ip_config_t manual_ip_cfg;
        memset( &manual_ip_cfg, 0, sizeof(manual_ip_cfg) );

        cfg_i8_get( CFG_PARAM_MANUAL_IP_ADDRESS, &manual_ip_cfg.ip );
        cfg_i8_get( CFG_PARAM_MANUAL_SUBNET_MASK, &manual_ip_cfg.subnet );
        cfg_i8_get( CFG_PARAM_MANUAL_DNS_SERVER, &manual_ip_cfg.dns_server );
        cfg_i8_get( CFG_PARAM_MANUAL_INTERNET_GATEWAY, &manual_ip_cfg.internet_gateway );
        
        // check that we have IP and subnet set
        if( ( !ip_b_is_zeroes( manual_ip_cfg.ip ) ) &&
            ( !ip_b_addr_compare( manual_ip_cfg.ip, ip_a_addr(255,255,255,255) ) ) &&
            ( !ip_b_is_zeroes( manual_ip_cfg.subnet ) ) &&
            ( !ip_b_addr_compare( manual_ip_cfg.subnet, ip_a_addr(255,255,255,255) ) ) ){
            
            // manual config is set, apply to IP config
            ip_config = manual_ip_cfg;
        }
    }

    // check loader status
    if( sys_u8_get_loader_status() == LDR_STATUS_NEW_FW ){
        
        // increment firmware load count

        uint16_t load_count;
        cfg_i8_get( CFG_PARAM_FIRMWARE_LOAD_COUNT, &load_count );

        load_count++;
        cfg_v_set( CFG_PARAM_FIRMWARE_LOAD_COUNT, &load_count );
    }

    // read mode string
    char mode_string[9];
    ee_v_read_block( 0, (uint8_t *)mode_string, sizeof(mode_string) - 1 );
    mode_string[8] = 0;

    // check mode
    if( strncmp_P( mode_string, PSTR("Sapphire"), sizeof(mode_string) ) != 0 ){
        
        // set mode string for bootloader
        strcpy_P( mode_string, PSTR("Sapphire") );
        ee_v_write_block( 0, (uint8_t *)mode_string, sizeof(mode_string) );
    }

    // check if device id is set
    uint64_t device_id;
    cfg_i8_get( CFG_PARAM_DEVICE_ID, &device_id );

    if( device_id == 0 ){

        // default device id is mac address
        cfg_i8_get( CFG_PARAM_802_15_4_MAC_ADDRESS, &device_id ); 
        cfg_v_set( CFG_PARAM_DEVICE_ID, &device_id );       
    }

    log_v_debug_P( PSTR("Cfg size:%d free:%d"), cfg_u16_total_blocks(), cfg_u16_free_blocks() );
}

void cfg_v_write_error_log( cfg_error_log_t *log ){
    
    ee_v_write_block( CFG_FILE_ERROR_LOG_START, (uint8_t *)log, sizeof(cfg_error_log_t) );
}

void cfg_v_read_error_log( cfg_error_log_t *log ){
    
    ee_v_read_block( CFG_FILE_ERROR_LOG_START, (uint8_t *)log, sizeof(cfg_error_log_t) );
}

void cfg_v_erase_error_log( void ){
    
    cfg_error_log_t log;
    memset( &log, 0xff, sizeof(log) );

    ee_v_write_block( CFG_FILE_ERROR_LOG_START, (uint8_t *)&log, sizeof(cfg_error_log_t) );
}

uint16_t cfg_u16_error_log_size( void ){
    
    uint16_t count = 0;

    for( uint16_t i = CFG_FILE_ERROR_LOG_START; 
         i < ( CFG_FILE_ERROR_LOG_START + CFG_FILE_SIZE_ERROR_LOG ); 
         i++ ){
        
        uint8_t byte = ee_u8_read_byte( i );

        if( ( byte == 0xff ) || ( byte == 0 ) ){
            
            break;
        }

        count++;
    }
    
    return count;
}



