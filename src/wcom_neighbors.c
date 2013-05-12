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

#include "system.h"
#include "config.h"
#include "timers.h"
#include "threading.h"
#include "at86rf230.h"
#include "ip.h"
#include "fs.h"
#include "random.h"
#include "wcom_mac_sec.h"
#include "wcom_mac.h"
#include "wcom_time.h"
#include "list.h"
#include "keyvalue.h"

#include "wcom_neighbors.h"


//#define NO_LOGGING
#include "logging.h"

static uint8_t beacon_interval;
static thread_t beacon_thread;

static list_t neighbor_list;

static uint8_t mode;
#define MODE_CHANNEL_SCAN   0
#define MODE_PARKED         1

static uint16_t upstream;
static uint8_t depth;
static uint8_t channel_reset_countdown;

static uint16_t beacon_flags;
static uint32_t beacon_timer;
    

KV_SECTION_META kv_meta_t wcom_nei_info_kv[] = {
    { KV_GROUP_SYS_INFO, KV_ID_WCOM_NEI_UPSTREAM,           SAPPHIRE_TYPE_UINT16,  KV_FLAGS_READ_ONLY,  &upstream,          0,  "wcom_nei_upstream" },
    { KV_GROUP_SYS_INFO, KV_ID_WCOM_NEI_DEPTH,              SAPPHIRE_TYPE_UINT8,   KV_FLAGS_READ_ONLY,  &depth,             0,  "wcom_nei_depth" },
    { KV_GROUP_SYS_INFO, KV_ID_WCOM_NEI_BEACON_INTERVAL,    SAPPHIRE_TYPE_UINT8,   KV_FLAGS_READ_ONLY,  &beacon_interval,   0,  "wcom_nei_beacon_interval" },
};


#define PROVISIONAL_STATE_EMPTY        0
#define PROVISIONAL_STATE_WAIT_FLASH   1
#define PROVISIONAL_STATE_WAIT_THUNDER 2

typedef struct{
    uint8_t state;
    uint16_t short_addr;
    uint8_t flags;
    ip_addr_t ip;
    uint8_t channel;
    uint16_t upstream;
    uint8_t depth;
    uint64_t challenge;
    uint32_t timer;
} provisional_neighbor_t;

static list_t prov_list;


typedef struct{
    wcom_neighbor_t *neighbor;
    uint8_t score;
} worst_neighbor_t;


PT_THREAD( beacon_sender_thread( pt_t *pt, void *state ) );
PT_THREAD( neighbor_age_thread( pt_t *pt, void *state ) );


void evict( uint16_t short_addr );
int8_t send_evict( uint16_t dest_addr );


static uint8_t neighbor_list_size( void ){
    
    return list_u8_count( &neighbor_list );
}

static bool neighbor_list_full( void ){
    
    uint16_t max;
    cfg_i8_get( CFG_PARAM_WCOM_MAX_NEIGHBORS, &max );
    
    #define MINIMUM_NEIGHBORS 1

    if( max < MINIMUM_NEIGHBORS ){
        
        max = MINIMUM_NEIGHBORS;
        cfg_v_set( CFG_PARAM_WCOM_MAX_NEIGHBORS, &max );
    }
    
    return neighbor_list_size() >= max;
}

static uint16_t vfile( vfile_op_t8 op, uint32_t pos, void *ptr, uint16_t len ){
    
    // the pos and len values are already bounds checked by the FS driver
    switch( op ){
        
        case FS_VFILE_OP_READ:
            len = list_u16_flatten( &neighbor_list, pos, ptr, len );           
            break;

        case FS_VFILE_OP_SIZE:
            len = neighbor_list_size() * sizeof(wcom_neighbor_t);
            break;

        default:
            len = 0;
            break;
    }

    return len;
}

// provisional neighbor list
static uint8_t prov_list_size( void ){
    
    return list_u8_count( &prov_list );
}

static provisional_neighbor_t *get_prov( uint16_t short_addr ){
    
    list_node_t ln = prov_list.head;
    
    while( ln >= 0 ){
        
        // get state for current list node
        provisional_neighbor_t *prov = list_vp_get_data( ln );
        
        if( prov->short_addr == short_addr ){
            
            return prov;
        }
        
        ln = list_ln_next( ln );
    }
    
    // not found
    return 0;
}

static provisional_neighbor_t *add_prov( uint16_t short_addr ){
    
    uint16_t max_prov;
    cfg_i8_get( CFG_PARAM_WCOM_MAX_PROV_LIST, &max_prov );
    
    // prevent configuration of a prov list of size 0, that would prevent
    // the node from joining the network.
    if( max_prov < 1 ){
        
        max_prov = 1;
        cfg_v_set( CFG_PARAM_WCOM_MAX_PROV_LIST, &max_prov );
    }

    uint16_t max_nei;
    cfg_i8_get( CFG_PARAM_WCOM_MAX_NEIGHBORS, &max_nei );
    
    // Set max prov to lesser of configured size and allowed neighbor list
    // size.  This prevents a situation where multiple joins get queued up
    // and do not have enough slots in the neighbor list for all of them.
    // That would cause a lot of churn and make it difficult for the node
    // to obtain an upstream connection, particularly if the neighbor list
    // size is set to a very small value, like 1.
    if( max_prov > max_nei ){

        max_prov = max_nei;
    }

    // check current list size
    if( prov_list_size() >= max_prov ){
        
        log_v_info_P( PSTR("prov list full") );
        return 0;
    }
    
    // check if the requested node is already in the list
    if( get_prov( short_addr ) != 0 ){
        
        log_v_info_P( PSTR("prov:%d already on list"), short_addr );
        return 0;
    }
    
    // create list node
    list_node_t ln = list_ln_create_node( 0, sizeof(provisional_neighbor_t) );
    
    // check if node was created
    if( ln < 0 ){
        
        log_v_info_P( PSTR("memory full") );
        return 0;
    }

    // get pointer
    provisional_neighbor_t *prov = list_vp_get_data( ln );

    // initialize timer
    prov->timer         = tmr_u32_get_system_time();

    // add to list and return pointer
    list_v_insert_tail( &prov_list, ln );

    return prov;
}

static void remove_prov( uint16_t short_addr ){
    
    list_node_t ln = prov_list.head;
    
    while( ln >= 0 ){
        
        // get state for current list node
        provisional_neighbor_t *prov = list_vp_get_data( ln );
        
        if( prov->short_addr == short_addr ){
            
            // remove from list
            list_v_remove( &prov_list, ln );

            // release node
            list_v_release_node( ln );

            return;
        }
        
        ln = list_ln_next( ln );
    }
}


// beacon interval control
void reset_beacon_interval( void ){
    
    beacon_interval = WCOM_BEACON_INTERVAL_MIN;
    
    // restart the thread
    if( beacon_thread >= 0 ){
        
        thread_v_restart( beacon_thread );
    }
}


wcom_neighbor_t *add_neighbor( uint16_t short_addr ){
    
    uint16_t max;
    cfg_i8_get( CFG_PARAM_WCOM_MAX_NEIGHBORS, &max );
    
    // check current list size
    if( neighbor_list_size() >= max ){
        
        return 0;
    }
    
    // check if the requested node is already in the list
    if( wcom_neighbors_p_get_neighbor( short_addr ) != 0 ){
        
        return 0;
    }
    
    // create list node
    list_node_t ln = list_ln_create_node( 0, sizeof(wcom_neighbor_t) );
    
    // check if node was created
    if( ln < 0 ){
        
        return 0;
    }

    // get state for current list node
    wcom_neighbor_t *ptr = list_vp_get_data( ln );
    
    memset( ptr, 0, sizeof(wcom_neighbor_t) );

    ptr->short_addr = short_addr;
    
    // add to list and return pointer
    list_v_insert_tail( &neighbor_list, ln );

    return ptr;
}

void reset_upstream( void ){

    wcom_neighbor_t *ptr = wcom_neighbors_p_get_neighbor( upstream );
    
    if( ptr != 0 ){
        
        ptr->flags &= ~WCOM_NEIGHBOR_FLAGS_UPSTREAM;

        log_v_debug_P( PSTR("Lost upstream: %d"), upstream );

        reset_beacon_interval();

        // set channel reset countdown
        channel_reset_countdown = WCOM_CHANNEL_RESET_WAIT;
    }

    // reset upstream
    upstream = 0;
    depth = 0;
}

void remove_neighbor( uint16_t short_addr ){
    
    list_node_t ln = neighbor_list.head;
    
    while( ln >= 0 ){
        
        // get state for current list node
        wcom_neighbor_t *ptr = list_vp_get_data( ln );
        
        if( ptr->short_addr == short_addr ){
            
            // is neighbor upstream?
            if( short_addr == upstream ){
                
                reset_upstream();
            }

            // remove from list
            list_v_remove( &neighbor_list, ln );

            // release node
            list_v_release_node( ln );

            return;
        }
        
        ln = list_ln_next( ln );
    }
}

// run neighbor drop policy
// returns short address of eligible neighbor
// or 0 if no eligible neighbors were found
uint16_t drop_neighbor( void ){

    list_node_t ln = neighbor_list.head;
    
    while( ln >= 0 ){
        
        // get state for current list node
        wcom_neighbor_t *ptr = list_vp_get_data( ln );
        
        // check that neighbor is neither upstream nor downstream.
        // "new" neighbors are also immune.
        if( ( ptr->short_addr != upstream ) &&
            ( ( ptr->flags & WCOM_NEIGHBOR_FLAGS_DOWNSTREAM ) == 0 ) &&
            ( ( ptr->flags & WCOM_NEIGHBOR_FLAGS_NEW ) == 0 ) ){
            
            return ptr->short_addr;
        }
        
        ln = list_ln_next( ln );
    }

    return 0;
}

void evict( uint16_t short_addr ){
    
    send_evict( short_addr );
    
    remove_neighbor( short_addr );

    reset_beacon_interval();
}


wcom_neighbor_t *wcom_neighbors_p_get_neighbor( uint16_t short_addr ){

    list_node_t ln = neighbor_list.head;
    
    while( ln >= 0 ){
        
        // get state for current list node
        wcom_neighbor_t *ptr = list_vp_get_data( ln );
        
        if( ptr->short_addr == short_addr ){
            
            return ptr;
        }
        
        ln = list_ln_next( ln );
    }
    
    // not found
    return 0;
}

bool wcom_neighbors_b_is_neighbor( uint16_t short_addr ){
	
    wcom_neighbor_t *neighbor = wcom_neighbors_p_get_neighbor( short_addr );
	
    if( neighbor != 0 ){

	    return TRUE;
    }

    return FALSE;
}

ip_addr_t wcom_neighbors_a_get_ip( uint16_t short_addr ){
    
    wcom_neighbor_t *neighbor = wcom_neighbors_p_get_neighbor( short_addr );
    
    if( neighbor != 0 ){
        
        return neighbor->ip;
    }
	
	return ip_a_addr(0,0,0,0);
}

uint16_t wcom_neighbors_u16_get_short( ip_addr_t ip ){
    
    list_node_t ln = neighbor_list.head;
    
    while( ln >= 0 ){
        
        // get state for current list node
        wcom_neighbor_t *ptr = list_vp_get_data( ln );
        
        if( ip_b_addr_compare( ip, ptr->ip ) ){
            
            return ptr->short_addr;
        }
        
        ln = list_ln_next( ln );
    }
	
	return 0;
}

uint16_t wcom_neighbors_u16_get_upstream( void ){
    
    return upstream;
}

uint8_t wcom_neighbors_u8_get_depth( void ){
    
    return depth;
}

uint8_t wcom_neighbors_u8_get_beacon_interval( void ){
    
    return beacon_interval;
}

uint8_t wcom_neighbors_u8_get_flags( uint16_t short_addr ){
    
    wcom_neighbor_t *neighbor = wcom_neighbors_p_get_neighbor( short_addr );
    
    if( neighbor == 0 ){
        
        return 0;
    }
    
    return neighbor->flags;
}

uint16_t wcom_neighbors_u16_get_gateway( void ){
    
    list_node_t ln = neighbor_list.head;
    
    while( ln >= 0 ){
        
        // get state for current list node
        wcom_neighbor_t *ptr = list_vp_get_data( ln );
        
        if( ptr->flags & WCOM_NEIGHBOR_FLAGS_GATEWAY ){
            
            return ptr->short_addr;
        }
        
        ln = list_ln_next( ln );
    }
    
    // not found
    return 0;
}

uint8_t wcom_neighbors_u8_get_channel( uint16_t short_addr ){

    wcom_neighbor_t *neighbor = wcom_neighbors_p_get_neighbor( short_addr );
    
    if( neighbor != 0 ){
    
        //return neighbor->channel;
    }
	
	return rf_u8_get_channel();
}

uint8_t wcom_neighbors_u8_get_etx( uint16_t short_addr ){ 
    
    wcom_neighbor_t *neighbor = wcom_neighbors_p_get_neighbor( short_addr );
    
    if( neighbor == 0 ){
        
        return 255;
    }

    return neighbor->etx;
}

// API to get cost.
// right now this is etx, but this makes it easy to change the cost
// estimation without changing APIs in other modules
uint8_t wcom_neighbors_u8_get_cost( uint16_t short_addr ){
    
    uint8_t cost = wcom_neighbors_u8_get_etx( short_addr );

    // cost cannot be 0
    if( cost == 0 ){
        
        cost = 32;
    }

    return cost;
}


uint8_t ewma_filter( uint8_t filter, uint8_t current_value, uint8_t avg ){
    
    uint8_t new_value = ( ( (uint16_t)filter * (uint16_t)current_value ) / 128 ) +
                        ( ( ( (uint16_t)( 128 - filter ) ) * (uint16_t)avg ) / 128 );
    
    return new_value;
}

void wcom_neighbors_v_received_from( uint16_t short_addr, wcom_mac_rx_options_t *options ){
    
    // look up neighbor
    wcom_neighbor_t *neighbor = wcom_neighbors_p_get_neighbor( short_addr );
    
    // check if neighbor was found
    if( neighbor != 0 ){
        
        // apply lqi and rssi filters

        uint16_t filter;
        cfg_i8_get( CFG_PARAM_WCOM_RSSI_FILTER, &filter );
        
        if( neighbor->lqi != 0 ){
        
            neighbor->lqi = ewma_filter( filter, options->lqi, neighbor->lqi );
        }
        else{
            
            neighbor->lqi = options->lqi;
        }

        if( neighbor->rssi != 0 ){
            
            neighbor->rssi = ewma_filter( filter, options->ed, neighbor->rssi );
        }
        else{
            
            neighbor->rssi = options->ed;
        }
    }
}

// indicate successful frame transmission.
// this is separate from the ack because it indicates actual frame traffic,
// not retransmits from failed frames.  The traffic indicator is attempting
// to estimate an ideal, "desired" traffic, independent of link quality.
void wcom_neighbors_v_sent_to( uint16_t short_addr ){
    
    wcom_neighbor_t *neighbor = wcom_neighbors_p_get_neighbor( short_addr );
    
    if( neighbor != 0 ){
        
        if( neighbor->traffic_accumulator < 255 ){

            neighbor->traffic_accumulator++;
        }
    }
}

void etx_estimator( wcom_neighbor_t *neighbor, bool ack ){
    
    uint8_t ack_value = 0;

    if( ack ){

        ack_value = 128;
    }

    uint16_t filter;
    cfg_i8_get( CFG_PARAM_WCOM_ETX_FILTER, &filter );

    neighbor->prr = ewma_filter( filter, ack_value, neighbor->prr );

    if( neighbor->prr > 8 ){

        neighbor->etx = 2048 / neighbor->prr; // eventually we can optimize this with a lookup table
    }
    else{
        
        neighbor->etx = 255;
    }
}

void wcom_neighbors_v_tx_ack( uint16_t short_addr ){
    
    wcom_neighbor_t *neighbor = wcom_neighbors_p_get_neighbor( short_addr );
    
    if( neighbor > 0 ){

        etx_estimator( neighbor, TRUE );
    }
}

void wcom_neighbors_v_tx_failure( uint16_t short_addr ){
    
    wcom_neighbor_t *neighbor = wcom_neighbors_p_get_neighbor( short_addr );
    
    if( neighbor > 0 ){
        
        etx_estimator( neighbor, FALSE );
    }
}

void wcom_neighbors_v_delay( uint16_t short_addr, uint16_t delay ){
    
    wcom_neighbor_t *neighbor = wcom_neighbors_p_get_neighbor( short_addr );
    
    if( neighbor > 0 ){
        
        neighbor->delay = ewma_filter( 32, delay, neighbor->delay );
    }
}

// internal stuff:

bool white_bit( uint8_t rssi, uint8_t etx ){
    /*
    if( ( rssi > WCOM_NEIGHBOR_WHITE_BIT_RSSI ) &&
        ( etx < WCOM_NEIGHBOR_WHITE_BIT_ETX ) ){
      */
    if( rssi > WCOM_NEIGHBOR_WHITE_BIT_RSSI ){

        return TRUE;
    } 
    
    return FALSE;
}

// helper function to get flags for this device
uint16_t my_flags( void ){
    
    uint16_t flags = 0;
    
    if( cfg_b_get_boolean( CFG_PARAM_ENABLE_ROUTING ) ){
        
        flags |= WCOM_NEIGHBOR_FLAGS_ROUTER;
    }

    if( cfg_b_is_gateway() ){
        
        flags |= WCOM_NEIGHBOR_FLAGS_GATEWAY;
    }

    if( neighbor_list_full() ){
        
        flags |= WCOM_NEIGHBOR_FLAGS_FULL;
    }

    if( ( neighbor_list_full() ) &&
        ( drop_neighbor() == 0 ) ){

        flags |= WCOM_NEIGHBOR_FLAGS_NO_JOIN;
    }

    if( ( wcom_time_u8_get_flags() & WCOM_TIME_FLAGS_SYNC ) != 0 ){
        
        flags |= WCOM_NEIGHBOR_FLAGS_TIME_SYNC;
    }

    return flags;
}

int8_t send_beacon( uint16_t dest_addr, bool join ){
    
    // get flags
    beacon_flags = my_flags();

    // create message
    wcom_msg_beacon_t beacon;
    beacon.type             = WCOM_NEIGHBOR_MSG_TYPE_BEACON;
    beacon.version          = WCOM_NEIGHBOR_PROTOCOL_VERSION;
    beacon.flags            = beacon_flags;
    beacon.upstream         = upstream;
    beacon.depth            = depth;
    
    memset( beacon.reserved, 0, sizeof(beacon.reserved) );

    // check join flag
    if( join ){
        
        beacon.flags |= WCOM_NEIGHBOR_FLAGS_JOIN;

        // no join flag is incompatible with join flag
        beacon.flags &= ~WCOM_NEIGHBOR_FLAGS_NO_JOIN;
    }
    
    // get our IP address
    cfg_i8_get( CFG_PARAM_IP_ADDRESS, &beacon.ip );
    
    // set replay counter
    beacon.counter          = wcom_mac_sec_u32_get_replay_counter();
   
    // compute auth tag
    wcom_mac_sec_v_sign_message( &beacon, 
                                 sizeof(beacon) - CRYPT_AUTH_TAG_SIZE, 
                                 beacon.auth_tag );
    
    // set up addressing
    wcom_mac_addr_t addr;
    wcom_mac_v_init_addr( dest_addr, &addr );

    // set up options
    wcom_mac_tx_options_t options;

    // check if broadcasting:
    if( dest_addr == WCOM_MAC_ADDR_BROADCAST ){

        options.ack_request = FALSE;
    }
    else{
        
        options.ack_request = TRUE;
    }

    options.secure_frame    = FALSE;
    options.protocol        = WCOM_MAC_PROTOCOL_NEIGHBOR;

    // send MAC frame
    if( wcom_mac_i8_transmit_frame( &addr,
                                    &options,
                                    WCOM_MAC_FCF_TYPE_DATA,
                                    (uint8_t *)&beacon,
                                    sizeof(beacon) ) < 0 ){
        
        // message queueing failed
        
        log_v_info_P( PSTR("Beacon msg failed at mac Q") );

        return -1;
    }

    return 0;
}


int8_t send_flash( uint16_t dest_addr, uint64_t challenge ){
    
    //log_v_debug_P( PSTR("Sending flash to:%d"), dest_addr );

    // create message
    wcom_msg_flash_t flash;
    flash.type        = WCOM_NEIGHBOR_MSG_TYPE_FLASH;
    flash.version     = WCOM_NEIGHBOR_PROTOCOL_VERSION;
    flash.challenge   = challenge;
    wcom_mac_sec_v_get_session_iv( flash.iv );

    // compute auth tag
    wcom_mac_sec_v_sign_message( &flash, 
                                 sizeof(flash) - CRYPT_AUTH_TAG_SIZE, 
                                 flash.auth_tag );

    // set up addressing
    wcom_mac_addr_t addr;
    wcom_mac_v_init_addr( dest_addr, &addr );

    // set up options
    wcom_mac_tx_options_t options;
    options.ack_request     = TRUE;
    options.secure_frame    = FALSE;
    options.protocol        = WCOM_MAC_PROTOCOL_NEIGHBOR;

    // send MAC frame
    if( wcom_mac_i8_transmit_frame( &addr,
                                    &options,
                                    WCOM_MAC_FCF_TYPE_DATA,
                                    (uint8_t *)&flash,
                                    sizeof(flash) ) < 0 ){
        
        // message queueing failed
        
        log_v_info_P( PSTR("Flash msg failed at mac Q") );

        return -1;
    }
    
    return 0;
}


int8_t send_thunder( uint16_t dest_addr, uint64_t challenge ){
    
    //log_v_debug_P( PSTR("Sending thunder to:%d"), dest_addr );

    // create message
    wcom_msg_thunder_t thunder;
    thunder.type          = WCOM_NEIGHBOR_MSG_TYPE_THUNDER;
    thunder.version       = WCOM_NEIGHBOR_PROTOCOL_VERSION;
    thunder.response      = challenge + 1;
    thunder.counter       = wcom_mac_sec_u32_get_replay_counter();
    wcom_mac_sec_v_get_session_iv( thunder.iv );
    
    // compute auth tag
    wcom_mac_sec_v_sign_message( &thunder, 
                                 sizeof(thunder) - CRYPT_AUTH_TAG_SIZE, 
                                 thunder.auth_tag );

    // set up addressing
    wcom_mac_addr_t addr;
    wcom_mac_v_init_addr( dest_addr, &addr );

    // set up options
    wcom_mac_tx_options_t options;
    options.ack_request     = TRUE;
    options.secure_frame    = FALSE;
    options.protocol        = WCOM_MAC_PROTOCOL_NEIGHBOR;

    // send MAC frame
    if( wcom_mac_i8_transmit_frame( &addr,
                                    &options,
                                    WCOM_MAC_FCF_TYPE_DATA,
                                    (uint8_t *)&thunder,
                                    sizeof(thunder) ) < 0 ){
        
        // message queueing failed
        
        log_v_info_P( PSTR("Thunder msg failed at mac Q") );

        return -1;
    }
    
    return 0;
}

int8_t send_evict( uint16_t dest_addr ){
    
    log_v_debug_P( PSTR("Sending eviction to:%d"), dest_addr );

    // create message
    wcom_msg_evict_t msg;
    msg.type          = WCOM_NEIGHBOR_MSG_TYPE_EVICT;
    msg.version       = WCOM_NEIGHBOR_PROTOCOL_VERSION;
    msg.counter       = wcom_mac_sec_u32_get_replay_counter();
    
    // compute auth tag
    wcom_mac_sec_v_sign_message( &msg, 
                                 sizeof(msg) - CRYPT_AUTH_TAG_SIZE, 
                                 msg.auth_tag );

    // set up addressing
    wcom_mac_addr_t addr;
    wcom_mac_v_init_addr( dest_addr, &addr );

    // set up options
    wcom_mac_tx_options_t options;
    options.ack_request     = TRUE;
    options.secure_frame    = FALSE;
    options.protocol        = WCOM_MAC_PROTOCOL_NEIGHBOR;

    // send MAC frame
    if( wcom_mac_i8_transmit_frame( &addr,
                                    &options,
                                    WCOM_MAC_FCF_TYPE_DATA,
                                    (uint8_t *)&msg,
                                    sizeof(msg) ) < 0 ){
        
        // message queueing failed
        
        log_v_info_P( PSTR("Evict msg failed at mac Q") );

        return -1;
    }
    
    return 0;
}

// initializes state and transmits join request beacon.
int8_t initiate_join( uint16_t source_addr, wcom_msg_beacon_t *beacon ){
     
    provisional_neighbor_t *prov = add_prov( source_addr );

    // check if prov was added
    if( prov == 0 ){
           
        return -1;
    }

    // send a beacon with a join request to the neighbor
    if( send_beacon( source_addr, TRUE ) < 0 ){
        
        // release prov
        remove_prov( source_addr );

        // couldn't send the beacon
        return -1;
    }
    
    // setup provisional neighbor
    prov->state         = PROVISIONAL_STATE_WAIT_FLASH;
    prov->short_addr    = source_addr;
    prov->flags         = beacon->flags;
    prov->ip            = beacon->ip;
    prov->upstream      = beacon->upstream;
    prov->depth         = beacon->depth;

    log_v_debug_P( PSTR("Initiate join with:%d"), source_addr );

    return 0;
}

// accept a join request
int8_t accept_join( uint16_t source_addr, wcom_msg_beacon_t *beacon ){
        
    provisional_neighbor_t *prov = add_prov( source_addr );

    // check if prov was added
    if( prov == 0 ){
        
        return -1;
    }

    log_v_debug_P( PSTR("Accept join from:%d"), source_addr );

    // set up random challenge
    rnd_v_fill( (uint8_t *)&prov->challenge, sizeof(prov->challenge) );

    // send flash
    if( send_flash( source_addr, prov->challenge ) < 0 ){
        
        // release prov
        remove_prov( source_addr );

        return -1;
    }

    // setup provisional neighbor
    prov->state         = PROVISIONAL_STATE_WAIT_THUNDER;
    prov->short_addr    = source_addr;
    prov->flags         = beacon->flags;
    prov->ip            = beacon->ip;
    prov->upstream      = beacon->upstream;
    prov->depth         = beacon->depth;

    return 0;
}

int8_t prov_received_flash( uint16_t source_addr ){
    
    // look for matching provisional neighbor
    provisional_neighbor_t *prov = get_prov( source_addr );

    if( prov == 0 ){
        
        return -1;
    }
    
    // check state
    if( prov->state != PROVISIONAL_STATE_WAIT_FLASH ){
        
        return -1;
    }

    // set up random challenge
    rnd_v_fill( (uint8_t *)&prov->challenge, sizeof(prov->challenge) );
    
    // send flash
    if( send_flash( source_addr, prov->challenge ) == 0 ){
        
        // wait for thunder
        prov->state = PROVISIONAL_STATE_WAIT_THUNDER;
    }
    else{
        // flash failed
    }

    return 0;
}

int8_t prov_received_thunder( uint16_t source_addr, wcom_msg_thunder_t *msg ){
    
    // look for matching provisional neighbor
    provisional_neighbor_t *prov = get_prov( source_addr );

    if( prov == 0 ){
        
        return -1;
    }
    
    // check state
    if( prov->state != PROVISIONAL_STATE_WAIT_THUNDER ){
        
        return -1;
    }
        
    if( msg->response != prov->challenge + 1 ){
        
        log_v_info_P( PSTR("Invalid response:%d"), source_addr );

        return -1;
    }
    
    // insert into neighbor table
    // first check if we already have an entry for this neighbor
    wcom_neighbor_t *neighbor = wcom_neighbors_p_get_neighbor( source_addr );
    
    if( neighbor == 0 ){
        
        // check if neighbor list is full
        if( neighbor_list_full() ){
            
            // run neighbor drop policy
            uint16_t dropped_neighbor = drop_neighbor();
            
            if( dropped_neighbor != 0 ){
                
                evict( dropped_neighbor );

                log_v_info_P( PSTR("Evicting:%d in favor of: %d"), 
                              dropped_neighbor, 
                              source_addr );
            }
        }

        // create neighbor
        neighbor = add_neighbor( source_addr );

        if( neighbor < 0 ){
            
            log_v_critical_P( PSTR("Insufficient memory for neighbor list") );

            return -1;
        }

        log_v_info_P( PSTR("Adding neighbor:%d"), source_addr );
    }
    else{

        log_v_info_P( PSTR("Rejoined neighbor:%d"), source_addr );
    }
    
    // init neighbor
    neighbor->flags          = prov->flags | WCOM_NEIGHBOR_FLAGS_NEW;
    neighbor->short_addr     = source_addr;
    neighbor->age            = 0;
    neighbor->replay_counter = msg->counter;
    neighbor->ip             = prov->ip;

    memcpy( neighbor->iv, msg->iv, sizeof(neighbor->iv) );
    
    // init link states so we default with the white bit set
    neighbor->rssi           = 10;
    neighbor->lqi            = 230;
    neighbor->prr            = 128;
    neighbor->etx            = 16;

    // check upstream status
    if( ( ( upstream == 0 ) || ( upstream == source_addr ) ) &&
        ( prov->upstream != 0 ) &&
        ( prov->depth < WCOM_NEIGHBOR_MAX_DEPTH ) ){
        
        upstream = source_addr;
        depth = prov->depth + 1;

        log_v_debug_P( PSTR("Upstream: %d Depth: %d"), upstream, depth );

        neighbor->flags |= WCOM_NEIGHBOR_FLAGS_UPSTREAM;
    }

    // reset beacon interval when adding new neighbor
    reset_beacon_interval();

    // prov neighbor is now free
    remove_prov( source_addr );

    return 0;
}

// check a beacon against the current set of neighbors and
// see if we should pair with this neighbor.
bool should_pair( wcom_msg_beacon_t *beacon, uint16_t source_addr, uint8_t rssi ){
    
    // check if beacon is not accepting joins
    if( ( beacon->flags & WCOM_NEIGHBOR_FLAGS_NO_JOIN ) != 0 ){
        
        return FALSE;
    }
    
    // check if we do not have an upstream route, and the beacon does,
    // or is a gateway
    if( ( upstream == 0 ) &&
        ( ( beacon->upstream != 0 ) ||
          ( ( beacon->flags & WCOM_NEIGHBOR_FLAGS_GATEWAY ) != 0 ) ) ){
        
        if( ( beacon->flags & WCOM_NEIGHBOR_FLAGS_GATEWAY ) != 0 ){
            
            log_v_debug_P( PSTR("should pair %d: gateway"), source_addr );
        }
        else{
        
            log_v_debug_P( PSTR("should pair %d: root"), source_addr );
        }

        return TRUE;
    }

    // check if we're in channel scan mode
    if( mode == MODE_CHANNEL_SCAN ){

        // ignore all other join conditions
        return FALSE;
    }

    // check if our neighbor table has space available,
    // and the beacon has space (or is requesting a join)
    if( ( !neighbor_list_full() ) &&
        ( ( ( beacon->flags & WCOM_NEIGHBOR_FLAGS_FULL ) == 0 ) ||
          ( ( beacon->flags & WCOM_NEIGHBOR_FLAGS_JOIN ) != 0 ) ) ){
        
        log_v_debug_P( PSTR("should pair %d: space"), source_addr );

        return TRUE;
    }

    // check if beacon is for an orphan (no route to root),
    // and we have a neighbor eligible to drop
    if( ( beacon->upstream == 0 ) &&
        ( drop_neighbor() != 0 ) ){

        log_v_debug_P( PSTR("should pair %d: drop"), source_addr );
        
        return TRUE;
    }

    return FALSE;
}


void process_beacon( wcom_msg_beacon_t *beacon, 
                     wcom_mac_addr_t *addr, 
                     wcom_mac_rx_options_t *options ){
        
    // look for a matching neighbor
    wcom_neighbor_t *neighbor = wcom_neighbors_p_get_neighbor( addr->source_addr );
    
    // case 0:
    // no session with neighbor
    if( neighbor == 0 ){
        
        // check if we should pair
        if( should_pair( beacon, addr->source_addr, options->ed ) ){
    
            // check if beacon is a join request
            if( ( beacon->flags & WCOM_NEIGHBOR_FLAGS_JOIN ) == 0 ){
                // NOT a join request

                // initiate join
                initiate_join( addr->source_addr, beacon );
            }
            else{
                // join request

                // accept join
                accept_join( addr->source_addr, beacon );
            }
        }
    }
    // case 1:
    // session with neighbor
    else{
        
        // case 1-A:
        // join request (rejoin)
        if( ( beacon->flags & WCOM_NEIGHBOR_FLAGS_JOIN ) != 0 ){
            
            log_v_debug_P( PSTR("Re-pairing:%d"), addr->source_addr );
            
            // accept join
            accept_join( addr->source_addr, beacon );
        }
        else{
            
            // authentication check
            uint8_t auth_tag[CRYPT_AUTH_TAG_SIZE];
            wcom_mac_sec_v_compute_auth_tag( neighbor->iv, 
                                             beacon, 
                                             sizeof(wcom_msg_beacon_t) - CRYPT_AUTH_TAG_SIZE,
                                             auth_tag );
            
            // case 1-B:
            // Authentication OK
            // compare computed tag to tag in message, and check replay counter
            if( ( memcmp( auth_tag, beacon->auth_tag, sizeof(auth_tag) ) == 0 ) &&
                ( beacon->counter > neighbor->replay_counter ) ){

                // update state information
                neighbor->ip                = beacon->ip;
                neighbor->replay_counter    = beacon->counter;
                
                // reset age
                neighbor->age               = 0;
                
                // check new flag
                if( neighbor->flags & WCOM_NEIGHBOR_FLAGS_NEW ){
                    
                    log_v_debug_P( PSTR("Clearing new flag on:%d"), addr->source_addr );

                    // clear new flag
                    neighbor->flags &= ~WCOM_NEIGHBOR_FLAGS_NEW;
                }

                // mask off beacon flags
                neighbor->flags &= ~( WCOM_NEIGHBOR_FLAGS_ROUTER | 
                                      WCOM_NEIGHBOR_FLAGS_GATEWAY |
                                      WCOM_NEIGHBOR_FLAGS_TIME_SYNC |
                                      WCOM_NEIGHBOR_FLAGS_FULL |
                                      WCOM_NEIGHBOR_FLAGS_NO_JOIN |
                                      WCOM_NEIGHBOR_FLAGS_DOWNSTREAM );
                
                // check beacon flags
                if( beacon->flags & WCOM_NEIGHBOR_FLAGS_ROUTER ){
                    
                    neighbor->flags |= WCOM_NEIGHBOR_FLAGS_ROUTER;
                }
                
                if( beacon->flags & WCOM_NEIGHBOR_FLAGS_GATEWAY ){
                    
                    neighbor->flags |= WCOM_NEIGHBOR_FLAGS_GATEWAY;
                }
                
                if( beacon->flags & WCOM_NEIGHBOR_FLAGS_TIME_SYNC ){
                    
                    neighbor->flags |= WCOM_NEIGHBOR_FLAGS_TIME_SYNC;
                }
                
                if( beacon->flags & WCOM_NEIGHBOR_FLAGS_FULL ){
                    
                    neighbor->flags |= WCOM_NEIGHBOR_FLAGS_FULL;
                }
                
                if( beacon->flags & WCOM_NEIGHBOR_FLAGS_NO_JOIN ){
                    
                    neighbor->flags |= WCOM_NEIGHBOR_FLAGS_NO_JOIN;
                }
                
                // check if we are upstream on this neighbor
                if( beacon->upstream == cfg_u16_get_short_addr() ){
                    
                    neighbor->flags |= WCOM_NEIGHBOR_FLAGS_DOWNSTREAM;
                }
                
                // check if this neighbor is our upstream
                if( addr->source_addr == upstream ){
                    
                    // check if upstream link lost
                    if( beacon->upstream == 0 ){
                    
                        reset_upstream();
                    }
                    // upstream marked valid
                    else{
                        
                        neighbor->flags |= WCOM_NEIGHBOR_FLAGS_UPSTREAM;

                        // update depth
                        depth = beacon->depth + 1;

                        // check depth
                        if( depth > WCOM_NEIGHBOR_MAX_DEPTH ){
                            
                            log_v_debug_P( PSTR("Upstream depth invalid") );

                            reset_upstream();
                        }
                    }
                }
                // check if we don't have an upstream, and this neighbor does,
                // and we're within the hop limit
                else if( ( upstream == 0 ) &&
                         ( beacon->upstream != 0 ) &&
                         ( beacon->depth < WCOM_NEIGHBOR_MAX_DEPTH ) ){
                    
                    upstream = addr->source_addr;
                    depth = beacon->depth + 1;

                    log_v_debug_P( PSTR("Upstream: %d Depth: %d"), upstream, depth );

                    neighbor->flags |= WCOM_NEIGHBOR_FLAGS_UPSTREAM;
                }
                // check if this neighbor has a better upstream connection
                else if( ( beacon->upstream != 0 ) &&
                       ( ( beacon->depth + 1 ) < depth ) ){
                    
                    reset_upstream();
                    
                    upstream = addr->source_addr;
                    depth = beacon->depth + 1;

                    log_v_debug_P( PSTR("Upstream: %d Depth: %d"), upstream, depth );

                    neighbor->flags |= WCOM_NEIGHBOR_FLAGS_UPSTREAM;
                }
            }
            // case 1-C:
            // Authentication fail
            else{
                // do nothing for now.
                // if a neighbor rebooted, they will probably be sending a new join request,
                // otherwise, they'll eventually time out
                log_v_debug_P( PSTR("Beacon auth fail on active session for:%d"), addr->source_addr );
                
                // initiate join
                initiate_join( addr->source_addr, beacon );
            }
        }
    }
}   

void process_flash( wcom_msg_flash_t *flash, 
                    wcom_mac_addr_t *addr, 
                    wcom_mac_rx_options_t *options ){
    
    //log_v_debug_P( PSTR("Received flash from:%d"), addr->source_addr );
    
    // verify message
    uint8_t auth_tag[CRYPT_AUTH_TAG_SIZE];
    wcom_mac_sec_v_compute_auth_tag( flash->iv, 
                                     flash, 
                                     sizeof(wcom_msg_flash_t) - CRYPT_AUTH_TAG_SIZE, 
                                     auth_tag ); 
    
    // check auth
    if( memcmp( auth_tag, flash->auth_tag, sizeof(auth_tag) ) != 0 ){
        
        // auth fail
        log_v_debug_P( PSTR("Flash auth fail") );

        return;
    }
    
    // send response
    if( send_thunder( addr->source_addr, flash->challenge ) < 0 ){
        
        // could not queue message
    }

    // check if we had a provisional neighbor waiting for a flash
    prov_received_flash( addr->source_addr );
}

void process_thunder( wcom_msg_thunder_t *thunder, 
                      wcom_mac_addr_t *addr, 
                      wcom_mac_rx_options_t *options ){

    //log_v_debug_P( PSTR("Received thunder from:%d"), addr->source_addr );

    // verify message
    uint8_t auth_tag[CRYPT_AUTH_TAG_SIZE];
    wcom_mac_sec_v_compute_auth_tag( thunder->iv, 
                                     thunder, 
                                     sizeof(wcom_msg_thunder_t) - CRYPT_AUTH_TAG_SIZE, 
                                     auth_tag ); 
    
    // check message
    if( !memcmp( auth_tag, thunder->auth_tag, sizeof(auth_tag) ) == 0 ){
        
        log_v_debug_P( PSTR("Thunder auth fail") );

        return;
    }
    
    // send to state machine
    prov_received_thunder( addr->source_addr, thunder );
}

void process_evict( wcom_msg_evict_t *msg, 
                    wcom_mac_addr_t *addr, 
                    wcom_mac_rx_options_t *options ){
    
    // get neighbor
    wcom_neighbor_t *neighbor = wcom_neighbors_p_get_neighbor( addr->source_addr );
    
    // check if we have the neighbor
    if( neighbor < 0 ){
        
        log_v_debug_P( PSTR("Evict from unknown neighbor: %s"), addr->source_addr );

        return;
    }

    //log_v_debug_P( PSTR("Received evict from:%d"), addr->source_addr );
    
    // verify message
    uint8_t auth_tag[CRYPT_AUTH_TAG_SIZE];
    wcom_mac_sec_v_compute_auth_tag( neighbor->iv, 
                                     msg, 
                                     sizeof(wcom_msg_evict_t) - CRYPT_AUTH_TAG_SIZE, 
                                     auth_tag ); 
    
    // check message
    if( !memcmp( auth_tag, msg->auth_tag, sizeof(auth_tag) ) == 0 ){
        
        log_v_debug_P( PSTR("Evict auth fail") );

        return;
    }
    
    // not paired, we got evicted
    log_v_debug_P( PSTR("Evicted by:%d IP:%d.%d.%d.%d"), 
                   (uint16_t)addr->source_addr,
                   neighbor->ip.ip3,
                   neighbor->ip.ip2,
                   neighbor->ip.ip1,
                   neighbor->ip.ip0 );
    
    // remove neighbor
    remove_neighbor( neighbor->short_addr );

    // reset beacon interval
    reset_beacon_interval();
}

void wcom_neighbors_v_receive_msg( wcom_mac_addr_t *addr, 
                                   wcom_mac_rx_options_t *options, 
                                   uint8_t *data, 
                                   uint8_t len ){

    // get message
    uint8_t *type = data;
    
    // get version field
    uint8_t *version = type + 1;
   
    // check protocol version
    if( *version != WCOM_NEIGHBOR_PROTOCOL_VERSION ){
        
        return;
    }
    
    // process message
    if( *type == WCOM_NEIGHBOR_MSG_TYPE_BEACON ){

        process_beacon( (wcom_msg_beacon_t *)type, addr, options );
    }	
    else if( *type == WCOM_NEIGHBOR_MSG_TYPE_FLASH ){
        
        process_flash( (wcom_msg_flash_t *)type, addr, options );
    }
    else if( *type == WCOM_NEIGHBOR_MSG_TYPE_THUNDER ){
        
        process_thunder( (wcom_msg_thunder_t *)type, addr, options );
    }
    else if( *type == WCOM_NEIGHBOR_MSG_TYPE_EVICT ){
        
        process_evict( (wcom_msg_evict_t *)type, addr, options );
    }
}



PT_THREAD( beacon_init_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );

    // init upstream if gateway
    if( cfg_b_is_gateway() ){
        
        upstream = cfg_u16_get_short_addr();
        depth = 0;
        mode = MODE_PARKED;
    }
    
    // random delay on start up, 0 to 1023 ms
    beacon_timer = rnd_u16_get_int() >> 6;
    TMR_WAIT( pt, beacon_timer );

    beacon_thread = thread_t_create( beacon_sender_thread,
                                     PSTR("wcom_neighbor_beacon_sender"),
                                     0,
                                     0 );
    
    // check if beacon thread was not created
    if( beacon_thread < 0 ){
        
        // restart this thread until we successfully get a beacon thread
        // also, log this as a critical error, since something is very very
        // wrong if the thread call fails
        log_v_critical_P( PSTR("Beacon init fail") );        

        THREAD_RESTART( pt );
    }

PT_END( pt );
}


PT_THREAD( beacon_sender_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );
    
    while(1){
        // check if we have an upstream
        if( upstream != 0 ){

            // check if changing mode
            if( mode == MODE_CHANNEL_SCAN ){

                log_v_debug_P( PSTR("Parked on channel: %d"), rf_u8_get_channel() );    
            }

            // park
            mode = MODE_PARKED;
        }        
        
        // channel scan mode
        while( mode == MODE_CHANNEL_SCAN ){
            
            // get current channel
            uint8_t channel = rf_u8_get_channel();

            // increment channel and bounds check
            channel++;

            if( channel >= RF_HIGHEST_CHANNEL ){

                channel = RF_LOWEST_CHANNEL;
            }

            // set channel
            rf_v_set_channel( channel );

            //log_v_debug_P( PSTR("Channel: %d"), rf_u8_get_channel() );

            // transmit a beacon
            send_beacon( WCOM_MAC_ADDR_BROADCAST, FALSE );

            // wait
            beacon_timer = WCOM_CHANNEL_SCAN_BEACON_WAIT;
            TMR_WAIT( pt, beacon_timer );               

            // wait until any provs have expired
            // this gives joins a chance to complete before switching to the next channel
            THREAD_WAIT_WHILE( pt, prov_list_size() != 0 ); 

            // how do we get out of this loop?
            // reset_beacon_interval() is called on acquisition of an upstream, which
            // restarts this entire thread.
        }

        while( mode == MODE_PARKED ){

            // broadcast beacon
            send_beacon( WCOM_MAC_ADDR_BROADCAST, FALSE );

            // timer is beacon interval in seconds with an additional
            // random value between 0 and 1.023 seconds
            beacon_timer = ( beacon_interval * 1000 ) + ( rnd_u16_get_int() >> 6 );
            TMR_WAIT( pt, beacon_timer );
            
            // increment interval
            beacon_interval *= 2;

            // bounds check
            if( beacon_interval > WCOM_BEACON_INTERVAL_MAX ){
                
                beacon_interval = WCOM_BEACON_INTERVAL_MAX;
            }
        }
    }
    
PT_END( pt );
}


PT_THREAD( join_timeout_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
    
    static uint32_t timer;

    while(1){
        
        timer = 200;
        TMR_WAIT( pt, timer );

        list_node_t ln = prov_list.head;
        
        while( ln >= 0 ){
            
            // get state for current list node
            provisional_neighbor_t *prov = list_vp_get_data( ln );

            // get next pointer
            ln = list_ln_next( ln );
            
            // check timeout
            if( tmr_i8_compare_time( prov->timer + 1000 ) < 0 ){
                
                log_v_info_P( PSTR("Join timeout:%d"), prov->short_addr );
                
                remove_prov( prov->short_addr );
            }
        }
    }

PT_END( pt );
}


PT_THREAD( neighbor_monitor_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
    
    static uint32_t timer;
    static uint8_t i;

    i = 0;
        
    while(1){
        
        timer = 1000;
        TMR_WAIT( pt, timer );

        i++;
        
        // scan neighbor list

        list_node_t ln = neighbor_list.head;
        
        while( ln >= 0 ){
            
            // get state for current list node
            wcom_neighbor_t *ptr = list_vp_get_data( ln );

            // get next pointer
            ln = list_ln_next( ln );
            
            // increment age
            ptr->age++;
            
            // check for timeout
            if( ( ptr->age >= WCOM_NEIGHBOR_MAX_AGE ) ||
                ( ( ( ptr->flags & WCOM_NEIGHBOR_FLAGS_NEW ) != 0 ) &&
                    ( ptr->age > WCOM_NEIGHBOR_MAX_AGE_NEW ) ) ){
                
                log_v_debug_P( PSTR("Haven't heard from:%d"), ptr->short_addr );

                evict( ptr->short_addr );
            }
            // check for poor link quality
            else if( ptr->etx >= WCOM_NEIGHBOR_DROP_ETX ){
            
                log_v_debug_P( PSTR("Dropping:%d due to poor link"), ptr->short_addr );

                evict( ptr->short_addr );
            }
            else if( ( i % 8 ) == 0 ){

                uint16_t filter;
                cfg_i8_get( CFG_PARAM_WCOM_TRAFFIC_FILTER, &filter );

                // mix current traffic into average
                uint8_t traffic = ewma_filter( filter, 
                                               ptr->traffic_accumulator, 
                                              ptr->traffic_avg );

                // reset accumulator
                ptr->traffic_accumulator = 0;

                // assign new average
                ptr->traffic_avg = traffic;
            }
        }

        // check if time sync status changed
        if( ( ( ( beacon_flags & WCOM_NEIGHBOR_FLAGS_TIME_SYNC ) == 0 ) &&
              ( ( wcom_time_u8_get_flags() & WCOM_TIME_FLAGS_SYNC ) != 0 ) ) ||
            ( ( ( beacon_flags & WCOM_NEIGHBOR_FLAGS_TIME_SYNC ) != 0 ) &&
              ( ( wcom_time_u8_get_flags() & WCOM_TIME_FLAGS_SYNC ) == 0 ) ) ){
            
            // reset beacon interval to notify neighbors of change in time
            // sync status
            reset_beacon_interval();       
        }

        // check channel reset countdown, and no upstream
        if( ( channel_reset_countdown > 0 ) && ( upstream == 0 ) ){

            channel_reset_countdown--;

            // check if countdown ended
            if( channel_reset_countdown == 0 ){

                log_v_debug_P( PSTR("Channel reset") );

                // reset mode to channel scan
                mode = MODE_CHANNEL_SCAN;

                reset_beacon_interval();

                // flush neighbors
                wcom_neighbors_v_flush();
            }
        }
    }

PT_END( pt );
}


// reset the neighbor table and broadcast an eviction notice
void wcom_neighbors_v_flush( void ){
    
    // broadcast eviction three times
    send_evict( WCOM_MAC_ADDR_BROADCAST );
    send_evict( WCOM_MAC_ADDR_BROADCAST );
    send_evict( WCOM_MAC_ADDR_BROADCAST );
    
    list_v_destroy( &prov_list );
    list_v_destroy( &neighbor_list );
}


void wcom_neighbors_v_init( void ){
    
    mode = MODE_CHANNEL_SCAN;

    // init lists
    list_v_init( &prov_list );
    list_v_init( &neighbor_list );
    
    beacon_thread = -1;

    // create threads
    thread_t_create( beacon_init_thread,
                     PSTR("wcom_neighbor_beacon_init"),
                     0,
                     0 );

    thread_t_create( join_timeout_thread,
                     PSTR("wcom_neighbor_join_timeout_monitor"),
                     0,
                     0 );

    thread_t_create( neighbor_monitor_thread,
                     PSTR("wcom_neighbor_monitor"),
                     0,
                     0 );

    // create vfile
    fs_f_create_virtual( PSTR("neighbors"), vfile );
}

