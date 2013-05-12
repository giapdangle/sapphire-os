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
#include "crc.h"
#include "statistics.h"
#include "random.h"

#include "at86rf230.h"
#include "wcom_mac.h"
#include "wcom_mac_sec.h"
#include "netmsg_handlers.h"
#include "wcom_neighbors.h"
#include "wcom_time.h"
#include "list.h"
#include "keyvalue.h"



typedef struct{
    uint16_t dest_addr;
    wcom_mac_msg_t msg;
    uint8_t transmit_attempts;
    uint32_t timer;
    uint8_t backoff;
} tx_state_t;


static list_t tx_q;

static bool mute;

static tx_state_t tx_state;
static uint8_t mac_sequence;

// local backoff exponent
static uint8_t local_be;
#define MIN_BE 3
#define MAX_BE 8
#define LOCAL_BE_RANGE  8
#define MIN_BE_RANGE ( MIN_BE * LOCAL_BE_RANGE )
#define MAX_BE_RANGE ( ( ( MAX_BE + 1 ) * LOCAL_BE_RANGE ) - 1 )


KV_SECTION_META kv_meta_t wcom_mac_info_kv[] = {
    { KV_GROUP_SYS_INFO, KV_ID_WCOM_MAC_BE,     SAPPHIRE_TYPE_UINT8,   KV_FLAGS_READ_ONLY,  &local_be, 0,   "wcom_mac_be" },
};


// replay cache
static wcom_mac_replay_cache_entry_t replay_cache[WCOM_MAC_REPLAY_CACHE_ENTRIES];
static uint8_t replay_cache_ptr;


PT_THREAD( wcom_mac_tx_thread( pt_t *pt, void *state ) );

static uint8_t adaptive_cca_res( void ){
    
    uint16_t res;
    cfg_i8_get( CFG_PARAM_WCOM_ADAPTIVE_CCA_RES, &res );
    
    return res;
}

static uint8_t adaptive_cca_min_be( void ){

    uint16_t be;
    uint16_t res = adaptive_cca_res();
    cfg_i8_get( CFG_PARAM_WCOM_ADAPTIVE_CCA_MIN_BE, &be );
    
    return be * res;
}

static uint8_t adaptive_cca_max_be( void ){

    uint16_t be;
    uint16_t res = adaptive_cca_res();
    cfg_i8_get( CFG_PARAM_WCOM_ADAPTIVE_CCA_MAX_BE, &be );
    
    return be * res;
}


// returns true if the frame is already in the cache.
// returns false if it was not in the cache.
static bool add_to_replay_cache( uint16_t source_addr, uint8_t sequence ){
    
    // search the cache for a match
    for( uint8_t i = 0; i < WCOM_MAC_REPLAY_CACHE_ENTRIES; i++ ){
        
        // check for match
        if( ( replay_cache[i].source_addr == source_addr ) &&
            ( replay_cache[i].sequence == sequence ) ){
            
            stats_v_increment( STAT_WCOM_MAC_REPLAY_CACHE_HITS );
            
            // return match found
            return TRUE;
        }
    }
    
    // no match
    // add entry and increment pointer
    replay_cache[replay_cache_ptr].source_addr  = source_addr;
    replay_cache[replay_cache_ptr].sequence     = sequence;
    
    replay_cache_ptr++;
    
    if( replay_cache_ptr >= WCOM_MAC_REPLAY_CACHE_ENTRIES ){
        
        replay_cache_ptr = 0;
    }
    
    return FALSE;
}

// returns length of header
uint8_t wcom_mac_u8_decode_address( const uint8_t *data, wcom_mac_addr_t *addr ){

    uint8_t len = 0;

	// get fcf
	const wcom_mac_fcf_t16 *fcf = (wcom_mac_fcf_t16 *)data;
	
	// get sequence
	const uint8_t *seq = data + sizeof(wcom_mac_fcf_t16);
	
	// get pointer to address fields
	const uint8_t *addr_fields = seq + sizeof(uint8_t);

    // get dest pan id
    addr->dest_pan_id = WCOM_MAC_PAN_ID_NOT_PRESENT;
    
    if( ( *fcf & WCOM_MAC_FCF_DEST_ADDR_MODE ) != 0 ){
        
        addr->dest_pan_id = *(uint16_t *)addr_fields;
        addr_fields += sizeof(uint16_t);
        len += sizeof(uint16_t);
    }
    
    // get dest address
    addr->dest_addr = WCOM_MAC_ADDR_NOT_PRESENT;
    addr->dest_mode = WCOM_MAC_ADDR_MODE_NONE;
        
    if( ( *fcf & WCOM_MAC_FCF_DEST_ADDR_MODE ) == WCOM_MAC_FCF_DEST_ADDR_SHORT ){
        
        addr->dest_addr = *(uint16_t *)addr_fields;
        addr_fields += sizeof(uint16_t);
        addr->dest_mode = WCOM_MAC_ADDR_MODE_SHORT;
        len += sizeof(uint16_t);
    }
    else if( ( *fcf & WCOM_MAC_FCF_DEST_ADDR_MODE ) == WCOM_MAC_FCF_DEST_ADDR_LONG ){
        
        addr->dest_addr = *(uint64_t *)addr_fields;
        addr_fields += sizeof(uint64_t);
        addr->dest_mode = WCOM_MAC_ADDR_MODE_LONG;
        len += sizeof(uint64_t);
    }
    
    // get source pan id
    addr->source_pan_id = WCOM_MAC_PAN_ID_NOT_PRESENT;
    
    if( ( *fcf & WCOM_MAC_FCF_INTRA_PAN ) == 0 ){
        
        addr->source_pan_id = *(uint16_t *)addr_fields;
        addr_fields += sizeof(uint16_t);
        len += sizeof(uint16_t);
    }
    
    // get source address
    addr->source_addr = WCOM_MAC_ADDR_NOT_PRESENT;
    addr->source_mode = WCOM_MAC_ADDR_MODE_NONE;
    
    if( ( *fcf & WCOM_MAC_FCF_SOURCE_ADDR_MODE ) == WCOM_MAC_FCF_SOURCE_ADDR_SHORT ){
        
        addr->source_addr = *(uint16_t *)addr_fields;
        addr_fields += sizeof(uint16_t);
        addr->source_mode = WCOM_MAC_ADDR_MODE_SHORT;
        len += sizeof(uint16_t);
    }
    else if( ( *fcf & WCOM_MAC_FCF_SOURCE_ADDR_MODE ) == WCOM_MAC_FCF_SOURCE_ADDR_LONG ){
        
        addr->source_addr = *(uint64_t *)addr_fields;
        addr_fields += sizeof(uint64_t);
        addr->source_mode = WCOM_MAC_ADDR_MODE_LONG;
        len += sizeof(uint64_t);
    }	

    return len;
}

void wcom_mac_v_rx_handler( rx_frame_buf_t *frame ){
	
    uint8_t len = frame->len;

	// get fcf
	wcom_mac_fcf_t16 *fcf = (wcom_mac_fcf_t16 *)&frame->data;
	
	// get sequence
	uint8_t *seq = (uint8_t *)&frame->data + sizeof(wcom_mac_fcf_t16);
	
	// get pointer to address fields
	uint8_t *ptr = seq + sizeof(uint8_t);
	
	// check type
	if( *fcf & WCOM_MAC_FCF_TYPE_DATA ){
		
		// adjust length for FCF, sequence, and FCS
		len -= ( sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint8_t) );
		
		wcom_mac_addr_t addr;
		
		// get dest pan id
		addr.dest_pan_id = WCOM_MAC_PAN_ID_NOT_PRESENT;
		
		if( ( *fcf & WCOM_MAC_FCF_DEST_ADDR_MODE ) != 0 ){
			
			addr.dest_pan_id = *(uint16_t *)ptr;
			ptr += sizeof(uint16_t);
			len -= sizeof(uint16_t);
		}
		
		// get dest address
		addr.dest_addr = WCOM_MAC_ADDR_NOT_PRESENT;
		addr.dest_mode = WCOM_MAC_ADDR_MODE_NONE;
			
		if( ( *fcf & WCOM_MAC_FCF_DEST_ADDR_MODE ) == WCOM_MAC_FCF_DEST_ADDR_SHORT ){
			
			addr.dest_addr = *(uint16_t *)ptr;
			ptr += sizeof(uint16_t);
			addr.dest_mode = WCOM_MAC_ADDR_MODE_SHORT;
			len -= sizeof(uint16_t);
		}
		else if( ( *fcf & WCOM_MAC_FCF_DEST_ADDR_MODE ) == WCOM_MAC_FCF_DEST_ADDR_LONG ){
			
			addr.dest_addr = *(uint64_t *)ptr;
			ptr += sizeof(uint64_t);
			addr.dest_mode = WCOM_MAC_ADDR_MODE_LONG;
			len -= sizeof(uint64_t);
		}
		
		// get source pan id
		addr.source_pan_id = WCOM_MAC_PAN_ID_NOT_PRESENT;
		
		if( ( *fcf & WCOM_MAC_FCF_INTRA_PAN ) == 0 ){
			
			addr.source_pan_id = *(uint16_t *)ptr;
			ptr += sizeof(uint16_t);
			len -= sizeof(uint16_t);
		}
		
		// get source address
		addr.source_addr = WCOM_MAC_ADDR_NOT_PRESENT;
		addr.source_mode = WCOM_MAC_ADDR_MODE_NONE;
		
		if( ( *fcf & WCOM_MAC_FCF_SOURCE_ADDR_MODE ) == WCOM_MAC_FCF_SOURCE_ADDR_SHORT ){
			
			addr.source_addr = *(uint16_t *)ptr;
			ptr += sizeof(uint16_t);
			addr.source_mode = WCOM_MAC_ADDR_MODE_SHORT;
			len -= sizeof(uint16_t);
		}
		else if( ( *fcf & WCOM_MAC_FCF_SOURCE_ADDR_MODE ) == WCOM_MAC_FCF_SOURCE_ADDR_LONG ){
			
			addr.source_addr = *(uint64_t *)ptr;
			ptr += sizeof(uint64_t);
			addr.source_mode = WCOM_MAC_ADDR_MODE_LONG;
			len -= sizeof(uint64_t);
		}	
        
        // filter frames with a broadcast PAN
        if( addr.dest_pan_id == WCOM_MAC_PAN_BROADCAST ){
            
            return;
        }

        // check replay cache
        if( add_to_replay_cache( (uint16_t)addr.source_addr, *seq ) == FALSE ){
           
            // get protocol control field
            wcom_mac_pcf_t8 *pcf = (wcom_mac_pcf_t8 *)ptr;
            ptr += sizeof(wcom_mac_pcf_t8);
            len -= sizeof(wcom_mac_pcf_t8);
            
            // set rx options
            wcom_mac_rx_options_t rx_options;
            rx_options.protocol         = WCOM_MAC_PROTOCOL( *pcf );
            rx_options.security_enabled = FALSE;
            rx_options.lqi              = frame->lqi;
            rx_options.ed               = frame->ed;
            rx_options.timestamp        = frame->timestamp;

            // check security
            if( *pcf & WCOM_MAC_SECURITY_AUTH ){
                
                rx_options.security_enabled = TRUE;

                // get security header
                wcom_mac_auth_header_t *auth_header = (wcom_mac_auth_header_t *)ptr;
                ptr += sizeof(wcom_mac_auth_header_t);
                len -= sizeof(wcom_mac_auth_header_t);
                
                len -= CRYPT_AUTH_TAG_SIZE;

                // get auth tag
                uint8_t *auth_tag = ptr + len;
                
                // check authentication
                if( wcom_mac_sec_i8_verify_message( addr.source_addr,
                                                    auth_header->replay_counter,
                                                    &frame->data, 
                                                    frame->len - ( CRYPT_AUTH_TAG_SIZE + sizeof(uint16_t) ),
                                                    auth_tag ) < 0 ){
                    
                    return;
                }
            }

            stats_v_increment( STAT_WCOM_MAC_DATA_FRAMES_RECEIVED );
            
            wcom_neighbors_v_received_from( addr.source_addr, &rx_options );
            
            // check protocol
            if( rx_options.protocol == WCOM_MAC_PROTOCOL_IPv4 ){
                
                // filter source address on registered neighbors
                if( wcom_neighbors_b_is_neighbor( addr.source_addr ) ){

                    // send frame to netmsg
                    netmsg_v_receive_802_15_4_mac( &addr, &rx_options, ptr, len );
                }
            }
            else if( rx_options.protocol == WCOM_MAC_PROTOCOL_NEIGHBOR ){
                
                wcom_neighbors_v_receive_msg( &addr, &rx_options, ptr, len );
            }
            else if( rx_options.protocol == WCOM_MAC_PROTOCOL_TIMESYNC ){
                
                wcom_time_v_receive_msg( &addr, &rx_options, ptr, len );
            }
        }
	} // Data Frame
}


void wcom_mac_v_init( void ){
    
    mute = FALSE;

    list_v_init( &tx_q );

    thread_t_create( wcom_mac_tx_thread,
                     PSTR("wcom_mac_transmit"),
                     0,
                     0 );
    
    local_be = MIN_BE * LOCAL_BE_RANGE;
}

uint8_t wcom_mac_u8_tx_q_size( void ){
    
    return list_u8_count( &tx_q );
}

uint8_t wcom_mac_u8_get_local_be( void ){
    
    return local_be;
}

// return size of MAC header given the address info
uint8_t wcom_mac_u8_calc_mac_header_length( wcom_mac_addr_t *addr, wcom_mac_tx_options_t *options ){
	
	uint8_t header_length = 6; // frame control, sequence, protocol control, and FCS
	
	if( addr->dest_mode == WCOM_MAC_ADDR_MODE_SHORT ){
		
		header_length += 2;
		header_length += 2; // dest PAN ID
	}
	else if( addr->dest_mode == WCOM_MAC_ADDR_MODE_LONG ){
		
		header_length += 8;
		header_length += 2; // dest PAN ID
	}
	
	if( addr->source_mode == WCOM_MAC_ADDR_MODE_SHORT ){
	
		header_length += 2;
	}
	else if( addr->source_mode == WCOM_MAC_ADDR_MODE_LONG ){
		
		header_length += 8;
	}

    // check security options
    if( options->secure_frame ){
        
        header_length += sizeof(wcom_mac_auth_header_t);
        header_length += CRYPT_AUTH_TAG_SIZE;
    }
	
	return header_length;
}


// return the maximum data length of a frame given the addressing
// modes passed in addr
uint8_t wcom_mac_u8_calc_max_data_length( wcom_mac_addr_t *addr, wcom_mac_tx_options_t *options ){
	
	return RF_MAX_FRAME_SIZE - wcom_mac_u8_calc_mac_header_length( addr, options );
}

// initialize address with shorts
void wcom_mac_v_init_addr( uint16_t dest_addr, wcom_mac_addr_t *addr ){
    
    // initialize transmit state
    memset( addr, 0, sizeof(wcom_mac_addr_t) );
    
    // set up addressing information
    
    // never send source pan ID (no inter-pan support)
    addr->source_pan_id = WCOM_MAC_PAN_ID_NOT_PRESENT;
    
    // set destination pan ID
    cfg_i8_get( CFG_PARAM_802_15_4_PAN_ID, &addr->dest_pan_id );
    
    // get local short address
    cfg_i8_get( CFG_PARAM_802_15_4_SHORT_ADDRESS, &addr->source_addr );
    addr->source_mode = WCOM_MAC_ADDR_MODE_SHORT;
    
    // set destination address
    addr->dest_addr = dest_addr;
    addr->dest_mode = WCOM_MAC_ADDR_MODE_SHORT;
}

// builds a MAC frame in buf.  Assumes buf is large enough for the entire frame
uint8_t wcom_mac_u8_build_frame( wcom_mac_addr_t *addr, 
                                 wcom_mac_tx_options_t *options, 
                                 uint16_t type,
                                 uint8_t *data, 
                                 uint8_t length,
                                 uint8_t *buf ){
    
	// bounds check data length
	ASSERT( length <= wcom_mac_u8_calc_max_data_length( addr, options ) );
	
    uint8_t frame_length = wcom_mac_u8_calc_mac_header_length( addr, options ) + length;

	// set up frame control field
	wcom_mac_fcf_t16 *fcf = (wcom_mac_fcf_t16 *)buf;
	
	*fcf = 0; // clear the fcf
	*fcf |= type; // frame type
	
	// check type and make sure settings are correct
	ASSERT( type == WCOM_MAC_FCF_TYPE_DATA );
    
	if( options->ack_request ){ // if ack requested, set ack request bit
		
		*fcf |= WCOM_MAC_FCF_ACK_REQ;
    }		
	
    *fcf |= WCOM_MAC_FCF_INTRA_PAN; // set intra pan
	
	// set sequence number
	uint8_t *sequence = buf + sizeof(wcom_mac_fcf_t16);

	mac_sequence++; // increment sequence number
	*sequence = mac_sequence; // set sequence
	
	// get pointer to first address field
	uint8_t *ptr = sequence + sizeof(uint8_t);
	
	// set up destination PAN ID
	// include this field only of destination address is included
	if( addr->dest_mode != WCOM_MAC_ADDR_MODE_NONE ){
		
		*(uint16_t *)ptr = addr->dest_pan_id;
		ptr += sizeof(uint16_t);
	}
	
	// set up destination address field
	switch( addr->dest_mode ){
		
		case WCOM_MAC_ADDR_MODE_NONE:
			
			*fcf |= WCOM_MAC_FCF_DEST_ADDR_NONE;

			break;
		
		case WCOM_MAC_ADDR_MODE_SHORT:
			
			*fcf |= WCOM_MAC_FCF_DEST_ADDR_SHORT;
			*(uint16_t *)ptr = addr->dest_addr;
			ptr += sizeof(uint16_t);
			
			break;
			
		case WCOM_MAC_ADDR_MODE_LONG:
			
			*fcf |= WCOM_MAC_FCF_DEST_ADDR_LONG;
			*(uint64_t *)ptr = addr->dest_addr;
			ptr += sizeof(uint64_t);
			
			break;
			
		default:
			ASSERT( 0 );
			break;
	}
	
	// skip source PAN ID
	
	// set up source address field
	switch( addr->source_mode ){
		
		case WCOM_MAC_ADDR_MODE_NONE:
			
			*fcf |= WCOM_MAC_FCF_SOURCE_ADDR_NONE;
		
			break;
		
		case WCOM_MAC_ADDR_MODE_SHORT:
			
			*fcf |= WCOM_MAC_FCF_SOURCE_ADDR_SHORT;
			*(uint16_t *)ptr = addr->source_addr;
			ptr += sizeof(uint16_t);
			
			break;
			
		case WCOM_MAC_ADDR_MODE_LONG:
			
			*fcf |= WCOM_MAC_FCF_SOURCE_ADDR_LONG;
			*(uint64_t *)ptr = addr->source_addr;
			ptr += sizeof(uint64_t);
			
			break;
			
		default:
			ASSERT( 0 );
			break;
	}
	
    // initialize protocol control field
    wcom_mac_pcf_t8 *pcf = (wcom_mac_pcf_t8 *)ptr;
    ptr += sizeof(wcom_mac_pcf_t8);
	
    // set protocol
    *pcf = options->protocol;
    

    // check if security is enabled
    if( options->secure_frame ){

        // we are only doing authentication right now
        *pcf |= WCOM_MAC_SECURITY_AUTH;

        // NOTE:
        // we don't set the SEC bit in the 802.15.4 header.
        // This is for several reasons:
        // 1. We use different security options than 802.15.4
        // 2. We use a frame type field to encode the higher level protocol
        //    which 802.15.4 doesn't have, so it's convenient to put our extensions there
        // 3. We don't want to interfere with hardware accelerated security processing
        //    (if any exists)

        wcom_mac_auth_header_t *auth_header = (wcom_mac_auth_header_t *)ptr;
        ptr += sizeof(wcom_mac_auth_header_t);
        
        // fill in auth header
        auth_header->replay_counter = wcom_mac_sec_u32_get_replay_counter();
    }

	// copy data (ptr now points to payload field)
	memcpy( ptr, data, length );
    
    // check if security is enabled
    if( options->secure_frame ){

        // get pointer to auth tag
        uint8_t *auth_tag = ptr + length;

        // get auth data (covers entire frame except auth tag itself and FCS)
        wcom_mac_sec_v_sign_message( buf, frame_length - ( CRYPT_AUTH_TAG_SIZE + sizeof(uint16_t) ), auth_tag );
    }

    // return transmit length
    return frame_length; 
}


bool wcom_mac_b_busy( void ){
    
    if( mute ){
        
        return TRUE;
    }

    return ( list_u8_count( &tx_q ) >= WCOM_MAC_MAX_QUEUED_FRAMES );

}

wcom_mac_msg_t wcom_mac_m_create_tx_msg( wcom_mac_addr_t *addr, 
                                         wcom_mac_tx_options_t *options, 
                                         uint16_t type,
                                         uint8_t *data, 
                                         uint8_t length,
                                         bool auto_release ){
    
    // check tx queue size
    if( wcom_mac_b_busy() ){
        
        return -1;
    }
    
    // calculate frame length
    uint8_t frame_length = wcom_mac_u8_calc_mac_header_length( addr, options ) + length;

    // allocate memory
    wcom_mac_msg_t msg = list_ln_create_node( 0, sizeof(wcom_mac_msg_state_t) + frame_length - 1 );

    if( msg < 0 ){
        
        return -1;
    }
    
    // get data pointer
    wcom_mac_msg_state_t *state = list_vp_get_data( msg );
    
    // set up state
    state->addr         = *addr;
    state->tx_options   = *options;
    state->tx_status    = WCOM_MAC_TX_STATUS_IDLE;
    state->auto_release = auto_release;
    
    // build MAC frame
    state->frame_len = wcom_mac_u8_build_frame( addr,
                                                options,
                                                type,
                                                data,
                                                length,
                                                &state->data );
    
    // add to tx queue
    list_v_insert_head( &tx_q, msg );

    return msg;
}

bool wcom_mac_b_msg_done( wcom_mac_msg_t msg ){
    
    // get state
    wcom_mac_msg_state_t *state = list_vp_get_data( msg );
    

    return ( state->tx_status != WCOM_MAC_TX_STATUS_IDLE ) &&
           ( state->tx_status != WCOM_MAC_TX_STATUS_BUSY );
}

uint16_t wcom_mac_u16_get_len( wcom_mac_msg_t msg ){
    
    return list_u16_node_size( msg ) - ( sizeof(wcom_mac_msg_state_t) - 1 );
}

void wcom_mac_v_set_tx_status( wcom_mac_msg_t msg, wcom_mac_tx_status_t8 tx_status ){
    
    // get state
    wcom_mac_msg_state_t *state = list_vp_get_data( msg );

    state->tx_status = tx_status;
}

wcom_mac_tx_status_t8 wcom_mac_u8_get_tx_status( wcom_mac_msg_t msg ){
    
    // get state
    wcom_mac_msg_state_t *state = list_vp_get_data( msg );
    
    return state->tx_status;
}

bool wcom_mac_b_is_autorelease( wcom_mac_msg_t msg ){

    // get state
    wcom_mac_msg_state_t *state = list_vp_get_data( msg );
    
    return state->auto_release;
}

void wcom_mac_v_release_msg( wcom_mac_msg_t msg ){
    
    list_v_release_node( msg );
}

// queue a MAC frame for transmission and set to auto release
int8_t wcom_mac_i8_transmit_frame( wcom_mac_addr_t *addr, 
                                   wcom_mac_tx_options_t *options, 
                                   uint16_t type,
                                   uint8_t *data, 
                                   uint8_t length ){
	
    if( mute ){

        // silently ignore transmissions in mute mode
        return 0;
    }

    // create a message and set to auto release
    wcom_mac_msg_t msg = wcom_mac_m_create_tx_msg( addr,
                                                   options,
                                                   type,
                                                   data, 
                                                   length,
                                                   TRUE );
    
    // check msg
    if( msg < 0 ){
        
        return -1;
    }
	
	return 0;
}

void wcom_mac_v_mute( void ){
    
    mute = TRUE;
}


PT_THREAD( wcom_mac_tx_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
    
    while(1){
        
        // wait for a frame to transmit
        THREAD_WAIT_WHILE( pt, list_u8_count( &tx_q ) == 0 );
        
        // get message to send
        tx_state.msg = list_ln_remove_tail( &tx_q );
        
        // get state
        wcom_mac_msg_state_t *msg_state = list_vp_get_data( tx_state.msg );

        // set dest addr
        tx_state.dest_addr = msg_state->addr.dest_addr;

        // set status to busy
        msg_state->tx_status = WCOM_MAC_TX_STATUS_BUSY;

        // if ack requested
        if( msg_state->tx_options.ack_request ){ 		
            
            uint16_t tx_attempts;
            
            // config values are 16 bits
            cfg_i8_get( CFG_PARAM_WCOM_TX_SW_TRIES, &tx_attempts );

            tx_state.transmit_attempts = tx_attempts;
        }

        // indicate traffic towards neighbor
        wcom_neighbors_v_sent_to( tx_state.dest_addr );
        
        // check if NOT in adaptive CCA mode
        if( !cfg_b_get_boolean( CFG_PARAM_WCOM_ADAPTIVE_CCA ) ){
            
            // set back off exponents
            uint16_t min_be;
            uint16_t max_be;
            cfg_i8_get( CFG_PARAM_WCOM_MIN_BE, &min_be );
            cfg_i8_get( CFG_PARAM_WCOM_MAX_BE, &max_be );
            
            rf_v_set_be( min_be, max_be );
        }

        // set TX power
        uint16_t tx_power;
        cfg_i8_get( CFG_PARAM_WCOM_MAX_TX_POWER, &tx_power );
        
        if( tx_power > 15 ){
            
            // note 15 is the lowest setting, 0 is the highest

            tx_power = 15;
            cfg_v_set( CFG_PARAM_WCOM_MAX_TX_POWER, &tx_power );
        }

        rf_v_set_power( tx_power ); 


        // start transmit timer
        tx_state.timer = tmr_u32_get_system_time_ms();

        // start transmit loop
        do{  
            
            if( cfg_b_get_boolean( CFG_PARAM_WCOM_ADAPTIVE_CCA ) ){
            
                // set backoff exponents
                rf_v_set_be( local_be / adaptive_cca_res(), 
                             local_be / adaptive_cca_res() );
            }

            // wait until we get transmit mode
            THREAD_WAIT_WHILE( pt, rf_i8_request_transmit_mode( RF_TX_MODE_AUTO_RETRY ) < 0 );
            
            // get state
            wcom_mac_msg_state_t *msg_state = list_vp_get_data( tx_state.msg );

            // load frame buffer
            rf_v_write_frame_buf( msg_state->frame_len, &msg_state->data );
            
            // transmit
            rf_v_transmit();
            
            // wait while transmitter is busy
            THREAD_WAIT_WHILE( pt, rf_u8_get_transmit_status() == RF_TRANSMIT_STATUS_BUSY );
            
            // read transmit status
            rf_tx_status_t8 status = rf_u8_get_transmit_status();
            
            if( status == RF_TRANSMIT_STATUS_OK ){

                // decrement backoff
                if( local_be > adaptive_cca_min_be() ){
                    
                    local_be--;
                }

                // indicate a transmit ack for peer
                wcom_neighbors_v_tx_ack( tx_state.dest_addr );
                
                break; // exit loop
            }
            else if( status == RF_TRANSMIT_STATUS_FAILED_CCA ){
                
                // increment backoff
                if( local_be < adaptive_cca_max_be() ){
                    
                    local_be++;
                }

                stats_v_increment( STAT_WCOM_MAC_CCA_FAILS );
            }
            else{ // ack fail
                
                // indicate transmit failure for peer
                wcom_neighbors_v_tx_failure( tx_state.dest_addr );
            }
            
            // decrement attempts
            tx_state.transmit_attempts--;
            
        } while( tx_state.transmit_attempts > 0 );
        
        // get total transmit time
        tx_state.timer = tmr_u32_elapsed_time( tx_state.timer );
        
        // signal the transmit time to the neighbor module
        wcom_neighbors_v_delay( tx_state.dest_addr, tx_state.timer );

        // check final status
        rf_tx_status_t8 status = rf_u8_get_transmit_status();

        if( status == RF_TRANSMIT_STATUS_OK ){
            
            stats_v_increment( STAT_WCOM_MAC_DATA_FRAMES_SENT );
            
            wcom_mac_v_set_tx_status( tx_state.msg, WCOM_MAC_TX_STATUS_OK );
        }
        else{
            
            stats_v_increment( STAT_WCOM_MAC_DATA_FRAMES_FAILED );
            
            wcom_mac_v_set_tx_status( tx_state.msg, WCOM_MAC_TX_STATUS_FAILED );
        }
        
        // check if auto release
        if( wcom_mac_b_is_autorelease( tx_state.msg ) ){
            
            wcom_mac_v_release_msg( tx_state.msg );
        }

        tx_state.msg = -1;
    }
	
PT_END( pt );
}


