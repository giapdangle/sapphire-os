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
#include "config.h"
#include "timers.h"
#include "threading.h"
#include "random.h"
#include "keyvalue.h"

#include "at86rf230.h"
#include "wcom_mac.h"
#include "wcom_neighbors.h"

#include "wcom_time.h"

//#define NO_LOGGING
#include "logging.h"

typedef struct{
    uint16_t dest_addr;
} tx_thread_state_t;

static wcom_time_info_t time_info;
static bool enabled = FALSE;


static int8_t ntp_kv_handler( 
    kv_op_t8 op,
    kv_grp_t8 group,
    kv_id_t8 id,
    void *data,
    uint16_t len )
{

    if( op == KV_OP_GET ){
        
        ntp_ts_t now = wcom_time_t_get_ntp_time();

        memcpy( data, &now.seconds, len );

        return 0;
    }

    return -1;
}


KV_SECTION_META kv_meta_t wcom_time_info_kv[] = {
    { KV_GROUP_SYS_INFO, KV_ID_WCOM_TIME_FLAGS,         SAPPHIRE_TYPE_UINT8,   KV_FLAGS_READ_ONLY,  &time_info.flags,           0,  "wcom_time_flags" },
    { KV_GROUP_SYS_INFO, KV_ID_WCOM_TIME_SOURCE_ADDR,   SAPPHIRE_TYPE_UINT16,  KV_FLAGS_READ_ONLY,  &time_info.source_addr,     0,  "wcom_time_source_addr" },
    { KV_GROUP_SYS_INFO, KV_ID_WCOM_TIME_CLOCK_SOURCE,  SAPPHIRE_TYPE_UINT8,   KV_FLAGS_READ_ONLY,  &time_info.clk_source,      0,  "wcom_time_clock_source" },
    { KV_GROUP_SYS_INFO, KV_ID_WCOM_TIME_DEPTH,         SAPPHIRE_TYPE_UINT8,   KV_FLAGS_READ_ONLY,  &time_info.depth,           0,  "wcom_time_depth" },
    { KV_GROUP_SYS_INFO, KV_ID_WCOM_TIME_SEQUENCE,      SAPPHIRE_TYPE_UINT8,   KV_FLAGS_READ_ONLY,  &time_info.sequence,        0,  "wcom_time_sequence" },
    { KV_GROUP_SYS_INFO, KV_ID_WCOM_TIME_DRIFT,         SAPPHIRE_TYPE_INT32,   KV_FLAGS_READ_ONLY,  &time_info.drift,           0,  "wcom_time_drift" },
    { KV_GROUP_SYS_INFO, KV_ID_WCOM_TIME_LOCAL,         SAPPHIRE_TYPE_UINT32,  KV_FLAGS_READ_ONLY,  &time_info.local_time,      0,  "wcom_time_local_time" },
    { KV_GROUP_SYS_INFO, KV_ID_WCOM_TIME_NETWORK,       SAPPHIRE_TYPE_UINT32,  KV_FLAGS_READ_ONLY,  &time_info.network_time,    0,  "wcom_time_network_time" },
    { KV_GROUP_SYS_INFO, KV_ID_NTP_SECONDS,             SAPPHIRE_TYPE_UINT32,  KV_FLAGS_READ_ONLY,  0, ntp_kv_handler,              "ntp_seconds" },
};



PT_THREAD( wcom_time_sync_thread( pt_t *pt, void *state ) );
PT_THREAD( wcom_time_tx_thread( pt_t *pt, tx_thread_state_t *state ) );


void wcom_time_v_init( void ){
    
    if( cfg_b_get_boolean( CFG_PARAM_ENABLE_TIME_SYNC ) ){

        enabled = TRUE;

        thread_t_create( wcom_time_sync_thread,
                         PSTR("wcom_time_sync"),
                         0,
                         0 );
    }
}

uint8_t wcom_time_u8_get_flags( void ){
    
    return time_info.flags;
}

void wcom_time_v_get_info( wcom_time_info_t *info ){
    
    *info = time_info;
}

bool wcom_time_b_sync( void ){
    
    if( time_info.flags & WCOM_TIME_FLAGS_SYNC ){
        
        return TRUE;
    }

    return FALSE;
}


bool wcom_time_b_ntp_sync( void ){
    
    if( time_info.flags & WCOM_TIME_FLAGS_NTP_SYNC ){
        
        return TRUE;
    }

    return FALSE;
}

void wcom_time_v_send_ts( uint16_t dest_addr ){
    
    tx_thread_state_t state;
    state.dest_addr = dest_addr;

    thread_t_create( THREAD_CAST(wcom_time_tx_thread),
                     PSTR("wcom_time_sync_tx"),
                     &state,
                     sizeof(state) );
}

void wcom_time_v_reset( void ){
    
    time_info.flags         = 0;
    time_info.source_addr   = 0;
    time_info.clk_source    = 0;
    time_info.depth         = 0;
    time_info.sequence      = 0;
}

uint32_t wcom_time_u32_elapsed_local( uint32_t local ){

    // compute elapsed local microseconds since last sync
    uint32_t elapsed_local;
	
    if( local >= time_info.local_time ){
		
		elapsed_local = local - time_info.local_time;
	}
	else{
		
		elapsed_local = UINT32_MAX - ( time_info.local_time - local );
	}

    return elapsed_local;
}

uint32_t wcom_time_u32_compensated_network_time( uint32_t local ){

    // get elapsed local time
    uint32_t elapsed_local = wcom_time_u32_elapsed_local( local );

    // compute drift compensation factor
    int32_t drift = 0;

    if( time_info.drift != 0 ){
    
        drift = (int32_t)elapsed_local / time_info.drift;
    }

    return time_info.network_time + elapsed_local + drift;
}

// get drift compensated network time
uint32_t wcom_time_u32_get_network_time( void ){
    
    // get current microsecond system time
    uint32_t now = tmr_u32_get_system_time_us();
    
    return wcom_time_u32_compensated_network_time( now );
}

uint32_t wcom_time_u32_get_network_time_ms( void ){
    
    // get current microsecond system time
    uint32_t now = tmr_u32_get_system_time_us();
    
    return wcom_time_u32_compensated_network_time( now ) / 1000;
}

ntp_ts_t wcom_time_t_get_ntp_time( void ){

    // get compensated network time
    uint32_t compensated_net_time = wcom_time_u32_get_network_time();

    // get elapsed network time
    uint32_t elapsed_net_time;
    
    if( compensated_net_time >= time_info.network_time ){
        
        elapsed_net_time = compensated_net_time - time_info.network_time;
    }
    else{
        
        elapsed_net_time = UINT32_MAX - ( time_info.network_time - compensated_net_time );
    }

    // split seconds and fractional second
    uint32_t seconds = elapsed_net_time / 1000000;
    uint32_t microseconds = elapsed_net_time % 1000000;

    // convert microseconds to a fraction second 32 bits wide
    // 2^32 = 4,294,967,296
    // 2^32 / 1,000,000 = 4294.967296
    // Since we round this off to 4294, we'll acculumate a small amount of error
    // as we approach 1 second, around 0.02 percent.  This is ok for now.
    // If later on we push the accuracy of the time sync protocol below one millisecond,
    // we'll want to enhance the accuracy of this conversion.
    // Note that rounding to 4295 would seem to have less error, but would result in an overflow.
    uint32_t fraction = microseconds * 4294;

    ntp_ts_t ntp;

    // add our elapsed fraction to the base ntp fraction
    ntp.fraction = fraction + time_info.ntp_time.fraction;

    // check if we rolled over
    if( ntp.fraction < time_info.ntp_time.fraction ){

        // carry one second
        seconds++;
    }

    // add elapsed seconds to ntp base
    ntp.seconds = seconds + time_info.ntp_time.seconds;

    return ntp;
}

void wcom_time_v_sync( 
    uint16_t source_addr, 
    uint8_t depth, 
    uint8_t clk_source, 
    uint8_t sequence,
    uint32_t local_timestamp,
    uint32_t network_timestamp,
    ntp_ts_t ntp_time )
{

    uint32_t comp_time = wcom_time_u32_compensated_network_time( local_timestamp );

    uint32_t elapsed_local = wcom_time_u32_elapsed_local( local_timestamp );
    uint32_t est_network = elapsed_local + time_info.network_time;
    int32_t actual_elapsed_network = network_timestamp - time_info.network_time;
    int32_t est_elapsed_network = network_timestamp - est_network;

    int32_t current_drift = 0;
    
    if( est_elapsed_network != 0 ){
        
        current_drift = actual_elapsed_network / est_elapsed_network;
    }

    int32_t new_drift = 0;

    if( ( time_info.flags & WCOM_TIME_FLAGS_SYNC ) != 0 ){
        
        if( current_drift != 0 ){

            uint16_t filter = 8;
            new_drift = ( ( (int32_t)filter * (int32_t)current_drift ) / 128 ) +
                        ( ( ( (int32_t)( 128 - filter ) ) * (int32_t)time_info.drift ) / 128 );
        }
        else{
            
            new_drift = time_info.drift;
        }
    }
    else if( ( time_info.flags & WCOM_TIME_FLAGS_INITIAL_SYNC ) != 0 ){
        
        new_drift = current_drift;

        time_info.flags |= WCOM_TIME_FLAGS_SYNC;
        time_info.flags &= ~WCOM_TIME_FLAGS_INITIAL_SYNC;
    }
    else{
        
        time_info.flags |= WCOM_TIME_FLAGS_INITIAL_SYNC;
    }
    
    time_info.source_addr   = source_addr;
    time_info.drift         = new_drift;

    time_info.local_time    = local_timestamp;
    time_info.depth         = depth;
    time_info.clk_source    = clk_source;
    time_info.sequence      = sequence;
    time_info.network_time  = network_timestamp;
    time_info.ntp_time      = ntp_time;

    if( time_info.ntp_time.seconds != 0 ){

        time_info.flags |= WCOM_TIME_FLAGS_NTP_SYNC;
    }

    //log_v_debug_P( PSTR("Net:%10lu Est:%10lu Diff:%5ld Comp:%10lu CDiff:%5ld Drift:%5ld"),
    log_v_debug_P( PSTR("F:%3d Net:%10lu Diff:%5ld Comp:%10lu CDiff:%5ld Drift:%5ld"),
                   time_info.flags,
                   network_timestamp,
                   //est_network,
                   (int32_t)(network_timestamp - est_network),
                   comp_time,
                   (int32_t)(network_timestamp - comp_time),
                   new_drift );
}

void wcom_time_v_receive_msg( wcom_mac_addr_t *addr, 
                              wcom_mac_rx_options_t *options, 
                              uint8_t *data, 
                              uint8_t len ){
    if( !enabled ){
        
        return;
    }

    uint8_t *type = data;

    if( *type == WCOM_TIME_MSG_TYPE_TIMESTAMP ){
        
        wcom_time_msg_timestamp_t *msg = (wcom_time_msg_timestamp_t *)type;
        
        // check flags
        if( ( msg->flags & WCOM_TIME_FLAGS_SYNC ) == 0 ){
            
            return;
        }

        wcom_time_v_sync( addr->source_addr,
                          msg->depth,
                          msg->clk_source,
                          msg->sequence,
                          options->timestamp,
                          msg->timestamp,
                          msg->ntp_time );

    }
    else if( *type == WCOM_TIME_MSG_TYPE_REQUEST ){
        
        wcom_time_v_send_ts( addr->source_addr );
    }
}

uint8_t wcom_time_u8_build_ts_msg( uint16_t dest_addr, uint8_t *data ){
        
    wcom_time_msg_timestamp_t msg;
        
    msg.type                 = WCOM_TIME_MSG_TYPE_TIMESTAMP;
    msg.flags                = time_info.flags;
    msg.depth                = time_info.depth + 1;
    msg.clk_source           = time_info.clk_source;
    msg.sequence             = time_info.sequence;
    msg.timestamp            = wcom_time_u32_get_network_time();
    msg.ntp_time             = wcom_time_t_get_ntp_time();
    memset( msg.reserved, 0, sizeof(msg.reserved) );

    wcom_mac_addr_t addr;
    wcom_mac_v_init_addr( dest_addr, &addr );
    
    wcom_mac_tx_options_t options;
    options.ack_request     = FALSE;
    options.secure_frame    = TRUE;
    options.protocol        = WCOM_MAC_PROTOCOL_TIMESYNC;

    uint8_t len = wcom_mac_u8_build_frame( &addr,
                                           &options,
                                           WCOM_MAC_FCF_TYPE_DATA,
                                           (uint8_t *)&msg,
                                           sizeof(msg),
                                           data );
    return len;
}


int8_t wcom_time_i8_send_request( uint16_t dest_addr ){
        
    wcom_time_msg_request msg;
        
    msg.type                 = WCOM_TIME_MSG_TYPE_REQUEST;
    
    wcom_mac_addr_t addr;
    wcom_mac_v_init_addr( dest_addr, &addr );
    
    wcom_mac_tx_options_t options;
    options.ack_request     = FALSE;
    options.secure_frame    = TRUE;
    options.protocol        = WCOM_MAC_PROTOCOL_TIMESYNC;

    // send MAC frame
    if( wcom_mac_i8_transmit_frame( &addr,
                                    &options,
                                    WCOM_MAC_FCF_TYPE_DATA,
                                    (uint8_t *)&msg,
                                    sizeof(msg) ) < 0 ){
        
        return -1;
    }

    return 0;
}

static bool upstream_sync( void ){

    return ( ( wcom_neighbors_u8_get_flags( wcom_neighbors_u16_get_upstream() ) & 
               WCOM_NEIGHBOR_FLAGS_TIME_SYNC ) != 0 );
}

static bool time_sync( void ){

    return ( 
             ( upstream_sync() ) &&
             ( wcom_neighbors_u16_get_upstream() == time_info.source_addr ) &&  
             ( time_info.flags != 0 ) && 
             ( wcom_time_u32_elapsed_local( tmr_u32_get_system_time_us() ) 
                < ( (uint32_t)WCOM_TIME_SYNC_LOSS_SECONDS * 1000000 ) ) 
           );
}

PT_THREAD( wcom_time_sync_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
    
    static uint32_t timer;
    
    while(1){
        
        // reset time info
        wcom_time_v_reset();

        // wait until there is a synchronized upstream neighbor
        THREAD_WAIT_WHILE( pt, !upstream_sync() );
    
        // send initial time stamp request to upstream
        wcom_time_i8_send_request( wcom_neighbors_u16_get_upstream() );

        // first delay
        timer = ( WCOM_TIME_SYNC_DRIFT_INIT_TIME * 1000 ) + 
                ( rnd_u16_get_int() >> WCOM_TIME_SYNC_INIT_RANDOMNESS );
        TMR_WAIT( pt, timer );
        
        // check flags
        if( ( time_info.flags & WCOM_TIME_FLAGS_INITIAL_SYNC ) == 0 ){
            
            // restart
            continue;
        }
        
        // main sync loop
        while( time_sync() ){

            // send request
            wcom_time_i8_send_request( wcom_neighbors_u16_get_upstream() );
            
            // check flags to determine how long to wait
            // initial sync:
            if( ( time_info.flags & WCOM_TIME_FLAGS_INITIAL_SYNC ) != 0 ){
                
                timer = ( WCOM_TIME_SYNC_DRIFT_INIT_TIME * 1000 ) + 
                        ( rnd_u16_get_int() >> WCOM_TIME_SYNC_INIT_RANDOMNESS );
                TMR_WAIT( pt, timer );
            }
            // sync:
            else if( ( time_info.flags & WCOM_TIME_FLAGS_SYNC ) != 0 ){
                
                // set up time delay for next sync
                timer = tmr_u32_get_system_time() +
                        ( (uint32_t)WCOM_TIME_SYNC_INTERVAL_MIN * 1000 ) + 
                        ( rnd_u16_get_int() >> WCOM_TIME_SYNC_RANDOMNESS );
                
                THREAD_WAIT_WHILE( pt, ( tmr_i8_compare_time( timer ) > 0 ) &&
                                       ( time_sync() ) );
            }
        }
        
        log_v_warn_P( PSTR("Sync loss") );
    }
	
PT_END( pt );
}


PT_THREAD( wcom_time_tx_thread( pt_t *pt, tx_thread_state_t *state ) )
{
PT_BEGIN( pt );  

    // wait until we get transmit mode
    THREAD_WAIT_WHILE( pt, rf_i8_request_transmit_mode( RF_TX_MODE_BASIC ) < 0 );
    
    ATOMIC;

    // build message
    uint8_t buf[RF_MAX_FRAME_SIZE];
    uint8_t len = wcom_time_u8_build_ts_msg( state->dest_addr, buf );

    // load frame buffer
    rf_v_write_frame_buf( len, buf );
    
    // transmit
    rf_v_transmit();

    END_ATOMIC;
    
    // wait while transmitter is busy
    THREAD_WAIT_WHILE( pt, rf_u8_get_transmit_status() == RF_TRANSMIT_STATUS_BUSY );
    
    // read transmit status
    rf_tx_status_t8 status = rf_u8_get_transmit_status();
    
    if( status == RF_TRANSMIT_STATUS_OK ){
    
    }
    else{ // ack fail
        
    }
	
PT_END( pt );
}



