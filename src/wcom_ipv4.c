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

#include <avr/io.h>
#include <util/delay.h>

#include "system.h"
#include "config.h"
#include "timers.h"
#include "threading.h"
#include "ip.h"
#include "icmp.h"
#include "netmsg.h"
#include "netmsg_handlers.h"

#include "at86rf230.h"
#include "wcom_mac.h"
#include "wcom_mac_sec.h"
#include "wcom_ipv4.h"
#include "wcom_neighbors.h"
#include "routing2.h"
#include "statistics.h"
#include "list.h"

#include <string.h>
#include <stdio.h>


#define NO_LOGGING
#include "logging.h"

/*

IPv4 over 802.15.4

Auth header
Route header
IPv4 datagram


*/

typedef struct{
    wcom_mac_addr_t addr;
    wcom_mac_tx_options_t tx_options;
    wcom_ipv4_msg_t msg;
    wcom_mac_msg_t mac_msg;
} tx_state_t;

static list_t tx_q;
static list_t route_list;

static list_t rx_list;

static uint8_t next_tag;
static tx_state_t tx_state;

static uint8_t mac_frame_data_length;

static wcom_ipv4_replay_cache_entry_t replay_cache[WCOM_IPv4_REPLAY_CACHE_ENTRIES];



void process_fragment( void *data, uint8_t len, uint16_t source_addr, wcom_mac_rx_options_t *options );

PT_THREAD( wcom_ipv4_timeout_thread( pt_t *pt, void *state ) );
PT_THREAD( ipv4_route_thread( pt_t *pt, void *state ) );
PT_THREAD( ipv4_tx_thread( pt_t *pt, void *state ) );


void wcom_ipv4_v_parse_header( uint32_t header, wcom_ipv4_frag_header_t *parsed_header ){
	
	uint8_t *temp = (uint8_t *)&header;	

	parsed_header->flags = temp[0] & 0xf0;	
	parsed_header->tag = ( ( temp[0] & 0x0f ) << 4 ) | ( ( temp[1] & 0xf0 ) >> 4 );
	parsed_header->size = ( (uint16_t)( temp[1] & 0x0f ) << 6 ) | ( ( temp[2] & 0xfc ) >> 2 );
	parsed_header->offset = ( (uint16_t)( temp[2] & 0x03 ) << 8 ) | temp[3];
}

uint32_t wcom_ipv4_u32_build_header( uint8_t flags, uint8_t tag, uint16_t size, uint16_t offset ){

	uint32_t header;
	uint8_t *temp = (uint8_t *)&header;	

	temp[0] = ( flags & 0xf0 ) | ( ( tag & 0xf0 ) >> 4 );
	temp[1] = ( ( tag & 0x0f ) << 4 ) | ( ( size >> 6 ) & 0x0f );
	temp[2] = ( ( size << 2 ) & 0xfc ) | ( ( offset >> 8 ) & 0x03 );
	temp[3] = offset & 0xff;
	
	return header;
}


void wcom_ipv4_v_init( void ){

    list_v_init( &tx_q );
    list_v_init( &route_list );
    list_v_init( &rx_list );

	thread_t_create( wcom_ipv4_timeout_thread,
                     PSTR("wcom_ipv4_timeout"),
                     0,
                     0 );

	thread_t_create( ipv4_route_thread,
                     PSTR("wcom_ipv4_route"),
                     0,
                     0 );

	thread_t_create( ipv4_tx_thread,
                     PSTR("wcom_ipv4_transmit"),
                     0,
                     0 );

    // get mac frame data length based on the frame configuration we'll use for 
    // ipv4 fragmentation.
    wcom_mac_tx_options_t tx_options;
    tx_options.secure_frame = FALSE;
    tx_options.ack_request = TRUE;
    tx_options.protocol = WCOM_MAC_PROTOCOL_IPv4;
    
    wcom_mac_addr_t addr;
    addr.dest_mode = WCOM_MAC_ADDR_MODE_SHORT;
    addr.source_mode = WCOM_MAC_ADDR_MODE_SHORT;
    
    mac_frame_data_length = wcom_mac_u8_calc_max_data_length( &addr, &tx_options );
}


uint16_t wcom_ipv4_u16_get_len( wcom_ipv4_msg_t msg ){
    
    return list_u16_node_size( msg ) - ( sizeof(wcom_ipv4_msg_state_t) - 1 );
}

uint16_t wcom_ipv4_u16_get_offset( wcom_ipv4_msg_t msg ){
    
    wcom_ipv4_msg_state_t *state = list_vp_get_data( msg );

    return state->offset;
}

uint8_t wcom_ipv4_u8_get_flags( wcom_ipv4_msg_t msg ){
    
    wcom_ipv4_msg_state_t *state = list_vp_get_data( msg );

    return state->flags;
}

void *wcom_ipv4_vp_get_msg_data( wcom_ipv4_msg_t msg ){
    
    wcom_ipv4_msg_state_t *state = list_vp_get_data( msg );

    return &state->data;
}


wcom_ipv4_auth_header_t *wcom_ipv4_p_get_auth_header( wcom_ipv4_msg_t msg ){
        
    wcom_ipv4_msg_state_t *state = list_vp_get_data( msg );
    
    // check if msg has an auth header
    if( !( state->flags & WCOM_FRAME_FLAGS_AUTH ) ){
        
        return 0;
    }

    return (wcom_ipv4_auth_header_t *)&state->data;
}

wcom_ipv4_source_route_header_t *wcom_ipv4_p_get_route_header( wcom_ipv4_msg_t msg ){
    
    wcom_ipv4_msg_state_t *state = list_vp_get_data( msg );
    
    // check if msg has a route header
    if( !( state->flags & WCOM_FRAME_FLAGS_SOURCE_ROUTE ) ){
        
        return 0;
    }

    void *ptr = &state->data;

    // get auth header
    wcom_ipv4_auth_header_t *auth_header = wcom_ipv4_p_get_auth_header( msg );
    
    if( auth_header != 0 ){
        
        ptr += sizeof(wcom_ipv4_auth_header_t);
    }
    
    return (wcom_ipv4_source_route_header_t *)ptr;
}

ip_hdr_t *wcom_ipv4_p_get_ip_header( wcom_ipv4_msg_t msg ){
    
    wcom_ipv4_msg_state_t *state = list_vp_get_data( msg );
    
    // get route header
    wcom_ipv4_source_route_header_t *route_header = wcom_ipv4_p_get_route_header( msg );
    
    void *ptr = &state->data;

    if( route_header != 0 ){
        
        ptr += WCOM_IPv4_ROUTE_HEADER_SIZE( route_header->hop_count );
    }

    // get auth header
    wcom_ipv4_auth_header_t *auth_header = wcom_ipv4_p_get_auth_header( msg );

    if( auth_header != 0 ){
        
        ptr += sizeof(wcom_ipv4_auth_header_t);
    }

    return (ip_hdr_t *)ptr;
}

uint16_t wcom_ipv4_u16_get_ip_data_len( wcom_ipv4_msg_t msg ){
    
    wcom_ipv4_msg_state_t *state = list_vp_get_data( msg );
    
    uint16_t ip_len = wcom_ipv4_u16_get_len( msg );

    // check if auth header is present
    if( state->flags & WCOM_FRAME_FLAGS_AUTH ){
    
        ip_len -= sizeof(wcom_ipv4_auth_header_t);
    }

    // check source route header
    wcom_ipv4_source_route_header_t *route_header = wcom_ipv4_p_get_route_header( msg );

    if( route_header != 0 ){
        
        ip_len -= WCOM_IPv4_ROUTE_HEADER_SIZE( route_header->hop_count );
    }
    
    return ip_len;
}

void wcom_ipv4_v_sign_msg( wcom_ipv4_msg_t msg ){

    // get auth header
    wcom_ipv4_auth_header_t *auth_header = wcom_ipv4_p_get_auth_header( msg );

    // check of auth header present
    if( auth_header == 0 ){
        
        return;
    }
    
    // get replay counter
    auth_header->replay_counter = wcom_mac_sec_u32_get_replay_counter();
    
    // get session iv
    uint8_t iv[CRYPT_KEY_SIZE];
    wcom_mac_sec_v_get_session_iv( iv );
    
    // copy first 96 bits of IV into auth header where the tag would go
    memcpy( auth_header->auth_tag, iv, sizeof(auth_header->auth_tag) );
    
    // get key
    uint8_t key[CRYPT_KEY_SIZE];
    cfg_v_get_security_key( CFG_KEY_WCOM_AUTH, key );
    
    // compute auth tag
    uint8_t auth_tag[CRYPT_AUTH_TAG_SIZE];
    crypt_v_aes_xcbc_mac_96( key, 
                             wcom_ipv4_vp_get_msg_data( msg ), 
                             wcom_ipv4_u16_get_len( msg ), 
                             auth_tag );
    
    // copy auth tag into header
    memcpy( auth_header->auth_tag, auth_tag, sizeof(auth_header->auth_tag) );
    
    // we do it this way so we can attach the IV inline with the message, which avoids needing
    // to copy the entire thing to a new buffer just to attach the IV which isn't being sent.
}


int8_t wcom_ipv4_i8_verify_msg( wcom_ipv4_msg_t msg ){

    wcom_ipv4_msg_state_t *state = list_vp_get_data( msg );

    // get auth header
    wcom_ipv4_auth_header_t *auth_header = wcom_ipv4_p_get_auth_header( msg );

    // check of auth header present
    if( auth_header == 0 ){
        
        // indicate auth check fails if no header present
        return -1;
    }
    
    wcom_neighbor_t *neighbor = wcom_neighbors_p_get_neighbor( state->source_addr );
    
    // check if neighbor was found
    if( neighbor == 0 ){
        
        return -2;
    }

    // check replay
    if( auth_header->replay_counter <= neighbor->replay_counter ){
        
        return -3;
    }
    
    // copy auth tag from header
    uint8_t message_auth_tag[CRYPT_AUTH_TAG_SIZE];
    memcpy( message_auth_tag, auth_header->auth_tag, sizeof(message_auth_tag) );
    
    // copy first 96 bits of neighbor's IV into auth header where the tag was
    memcpy( auth_header->auth_tag, neighbor->iv, sizeof(auth_header->auth_tag) );

    // get key
    uint8_t key[CRYPT_KEY_SIZE];
    cfg_v_get_security_key( CFG_KEY_WCOM_AUTH, key );
    
    // compute auth tag
    uint8_t auth_tag[CRYPT_AUTH_TAG_SIZE];
    crypt_v_aes_xcbc_mac_96( key, 
                             wcom_ipv4_vp_get_msg_data( msg ), 
                             wcom_ipv4_u16_get_len( msg ), 
                             auth_tag );

    // compare tags
    if( memcmp( message_auth_tag, auth_tag, CRYPT_AUTH_TAG_SIZE ) != 0 ){
        
        return -4;
    }

    // set replay counter
    neighbor->replay_counter = auth_header->replay_counter;

    return 0;
}

void wcom_ipv4_v_route_msg( netmsg_t netmsg ){
    
    // get ip header
    ip_hdr_t *ip_hdr = netmsg_vp_get_data( netmsg );

    // sanity check on dest. IP address
    if( ip_b_is_zeroes( ip_hdr->dest_addr ) ){

        // we can't discover a route for an invalid address

        return;
    }

    // initiate discovery
    route_query_t query = route2_q_query_ip( ip_hdr->dest_addr );

    if( route2_i8_discover( &query ) < 0 ){
        
        // if we didn't get a discovery, there is no point in
        // queueing the message

        return;
    }

    // check space in route list
    if( list_u8_count( &route_list ) >= WCOM_IPv4_MAX_TX_ROUTES ){
        
        return;
    }
    
    // make a copy of the netmsg
    netmsg_t new_msg = netmsg_nm_create( netmsg_vp_get_data( netmsg ),
                                         netmsg_u16_get_len( netmsg ) );
    
    if( new_msg < 0 ){
        
        return;
    }
    
    // add to list
    list_v_insert_head( &route_list, new_msg );
}

// create a blank wcom msg
wcom_ipv4_msg_t wcom_ipv4_m_create( uint16_t size ){
    
    // allocate memory
    wcom_ipv4_msg_t msg = list_ln_create_node( 0, sizeof(wcom_ipv4_msg_state_t) + size - 1 );

    if( msg < 0 ){
        
        return -1;
    }
    
    // get data pointer
    wcom_ipv4_msg_state_t *state = list_vp_get_data( msg );
    
    // initialize all the things to 0s
    memset( state, 0, sizeof(wcom_ipv4_msg_state_t) );
    
    return msg;
}


wcom_ipv4_msg_t wcom_ipv4_m_create_from_netmsg( netmsg_t netmsg ){
    
    // get ip header
    ip_hdr_t *ip_hdr = netmsg_vp_get_data( netmsg );

    // check if destination has a route
    route2_t route;
    route_query_t query = route2_q_query_ip( ip_hdr->dest_addr );
    
    if( route2_i8_get( &query, &route ) < 0 ){
        
        // queue for routing
        wcom_ipv4_v_route_msg( netmsg );
        
        // no route to destination
        return -1;
    }
    
    bool secure_frame = TRUE;

    if( netmsg_u8_get_flags( netmsg ) & NETMSG_FLAGS_WCOM_SECURITY_DISABLE ){
        
        secure_frame = FALSE;
    }

    // calculate frame size
    uint16_t ip_pkt_len = netmsg_u16_get_len( netmsg );
    uint16_t frame_size = ip_pkt_len;
    
    // check if we need a route header (routes further than 1 hop)
    // note that source + dest are counted on the hop list, so 2 hops
    // from the route is a direct neighbor.
    bool source_route = FALSE;
   
    if( route.hop_count > 2 ){
        
        source_route = TRUE;
    }

    uint16_t route_hdr_size = 0;

    if( source_route ){

        route_hdr_size = WCOM_IPv4_ROUTE_HEADER_SIZE( route.hop_count );
        frame_size += route_hdr_size;
    }

    // check if security enabled
    if( secure_frame ){
        
        frame_size += sizeof(wcom_ipv4_auth_header_t);
    }

    // allocate memory for full frame
    // this is done in a list node so we can queue it
    wcom_ipv4_msg_t msg = wcom_ipv4_m_create( frame_size );

    if( msg < 0 ){
        
        return -1;
    }
    
    // get data pointer
    wcom_ipv4_msg_state_t *state = list_vp_get_data( msg );
    void *ptr = &state->data;

    // set flags
    state->flags = WCOM_FRAME_FLAGS_IPv4;

    if( source_route ){
        
        state->flags |= WCOM_FRAME_FLAGS_SOURCE_ROUTE;
    }

    if( secure_frame ){

        state->flags |= WCOM_FRAME_FLAGS_AUTH;
    }

    // check if security enabled
    if( secure_frame ){

        ptr += sizeof(wcom_ipv4_auth_header_t);
    }
    
    // set next hop
    state->next_hop = route.hops[1]; // we're hop 0

    // check if we need the route header
    if( source_route ){

        // build route header
        wcom_ipv4_source_route_header_t *route_header = (wcom_ipv4_source_route_header_t *)ptr;
        route_header->forward_cost   = wcom_neighbors_u8_get_cost( state->next_hop );
        route_header->hop_count      = route.hop_count;
        route_header->next_hop       = 1;
        
        ptr += sizeof(wcom_ipv4_source_route_header_t);

        // copy hops
        memcpy( ptr, route.hops, sizeof(uint16_t) * route_header->hop_count );
        ptr += sizeof(uint16_t) * route_header->hop_count;

        // indicate traffic on this route
        route2_v_traffic( &route );
    }

    // copy data
    memcpy( ptr, ip_hdr, ip_pkt_len );
    ptr += ip_pkt_len;
    
    // set tag
    state->tag = next_tag++;

    // sign message (if the security header is not present, this will be a no op)
    wcom_ipv4_v_sign_msg( msg );

    return msg;
}

// append raw data to end of ms
void wcom_ipv4_v_append_data( wcom_ipv4_msg_t msg, uint8_t *data, uint16_t len ){
    
    wcom_ipv4_msg_state_t *state = list_vp_get_data( msg );
    
    void *ptr = &state->data + state->offset;
    
    // bounds check
    uint16_t max_size = wcom_ipv4_u16_get_len( msg );
    uint16_t space_remaining = max_size - state->offset;

    if( len > space_remaining ){
        
        len = space_remaining;
    }
    
    // copy data
    memcpy( ptr, data, len );
    
    // advance offset
    state->offset += len;

    ASSERT( state->offset <= max_size );
}   


uint16_t wcom_ipv4_u16_get_next_hop( wcom_ipv4_msg_t msg ){

    wcom_ipv4_msg_state_t *state = list_vp_get_data( msg );

    return state->next_hop;
}

void wcom_ipv4_v_release_msg( wcom_ipv4_msg_t msg ){
    
    list_v_release_node( msg );
}


// adds a packet to the replay cache
// returns TRUE if packet was already in the cache
bool wcom_ipv4_b_add_to_replay_cache( ip_addr_t source_addr, uint8_t tag ){
	
	uint8_t oldest = 0;
	
	// find oldest entry in cache
	for( uint8_t i = 0; i < WCOM_IPv4_REPLAY_CACHE_ENTRIES; i++ ){
		
		// update oldest
		if( replay_cache[i].age > replay_cache[oldest].age ){
			
			oldest = i;
		}
		
		// check if packet matches a cache entry
		if( ( ip_b_addr_compare( source_addr, replay_cache[i].source_addr ) ) 
		 && ( tag == replay_cache[i].tag ) ){
				
			// cache hit, reset age and return TRUE
			replay_cache[i].age = 0;
			
            stats_v_increment( STAT_WCOM_IPV4_REPLY_CACHE_HITS );
            
			return TRUE;
		}
	}
	
	// add packet in oldest index
	replay_cache[oldest].age = 0;
	replay_cache[oldest].source_addr = source_addr;
	replay_cache[oldest].tag = tag;
		
	return FALSE; // packet was not already in the cache
}

// data frame callback from MAC layer
void wcom_ipv4_v_received_mac_frame( wcom_mac_addr_t *addr, 
                                     wcom_mac_rx_options_t *options,
                                     uint8_t *data, 
                                     uint8_t len ){
	
    process_fragment( data, len, addr->source_addr, options );	
}


// search given list for a message with the matching source address and tag
wcom_ipv4_msg_t wcom_ipv4_m_get_msg( list_t *list, 
                                     uint16_t source_addr, 
                                     uint8_t tag ){
    
    // get head node
    wcom_ipv4_msg_t msg = list->head;
    
    // start iteration
    while( msg >= 0 ){
        
        wcom_ipv4_msg_state_t *state = list_vp_get_data( msg );
        
        // check source and tag
        if( ( state->source_addr == source_addr ) &&
            ( state->tag == tag ) ){
            
            return msg;
        }

        // get next item
        msg = list_ln_next( msg );
    }    

    // no match found
    return -1;
}


void process_fragment( void *data, 
                       uint8_t len, 
                       uint16_t source_addr, 
                       wcom_mac_rx_options_t *options ){
    
    //log_v_info_P(PSTR("%d"), len);

	wcom_ipv4_frag_header_t parsed_header;
    uint32_t *hdr = (uint32_t *)data;

	// parse the header	
	wcom_ipv4_v_parse_header( *hdr, &parsed_header );
	
    // check ipv4 flag
    if( !( parsed_header.flags & WCOM_FRAME_FLAGS_IPv4 ) ){
        
        return;
    }

    // advance data pointer past frag header
    data += sizeof(uint32_t);
	len -= sizeof(uint32_t);

    wcom_ipv4_msg_t msg = wcom_ipv4_m_get_msg( &rx_list, source_addr, parsed_header.tag );
    
    if( msg < 0 ){
        
        // message not found
        // check if this fragment is offset 0, if not, bail out
        if( parsed_header.offset != 0 ){
            
            return;
        }
        
        // create a new message
        msg = wcom_ipv4_m_create( parsed_header.size );

        // check message creation
        if( msg < 0 ){
            
            return;
        }
        
        // get state
        wcom_ipv4_msg_state_t *state = list_vp_get_data( msg );
        
        // init state from header
        state->flags        = parsed_header.flags;
        state->tag          = parsed_header.tag;
        state->source_addr  = source_addr;
        
        // add to receive list
        list_v_insert_head( &rx_list, msg );
    }


    ASSERT( msg >= 0 );
        
    // message found
    // check size
    if( wcom_ipv4_u16_get_len( msg ) != parsed_header.size ){
        
        // bail out
        return;
    }
    
    // get state
    wcom_ipv4_msg_state_t *state = list_vp_get_data( msg );
    
    // check offset
    if( parsed_header.offset != state->offset ){
        
        // this check requires that message fragments are received in order.
        // this shouldn't be a problem, since we transmit them in order.

        return;
    }

    // check flags
    if( parsed_header.flags != state->flags ){
        
        return;
    }
    
    // append data
    wcom_ipv4_v_append_data( msg, data, len );

    stats_v_increment( STAT_WCOM_IPV4_FRAGMENTS_RECEIVED );
    
    // check if we have received everything
    if( state->offset != wcom_ipv4_u16_get_len( msg ) ){
        
        return;
    }
    
    //
    // final message processing follows
    //

    // remove from receive list
    list_v_remove( &rx_list, msg );
    
    // get ip header
    ip_hdr_t *ip_hdr = wcom_ipv4_p_get_ip_header( msg );
/*
    // check proto and log ICMP
    if( ip_hdr->protocol == IP_PROTO_ICMP ){
        
        icmp_hdr_t *icmp_hdr = (icmp_hdr_t *)( ip_hdr + 1 );
        
        log_v_debug_P( PSTR("ICMP seq:%d from:%d.%d.%d.%d to:%d.%d.%d.%d"), 
                       icmp_hdr->sequence,
                       ip_hdr->source_addr.ip3, 
                       ip_hdr->source_addr.ip2, 
                       ip_hdr->source_addr.ip1, 
                       ip_hdr->source_addr.ip0,
                       ip_hdr->dest_addr.ip3, 
                       ip_hdr->dest_addr.ip2, 
                       ip_hdr->dest_addr.ip1, 
                       ip_hdr->dest_addr.ip0 );
    }
*/
    // add to replay cache and check if packet was already in the cache
    if( wcom_ipv4_b_add_to_replay_cache( ip_hdr->source_addr, parsed_header.tag ) == TRUE ){
        
        log_v_debug_P( PSTR("Cache hit from source:%d.%d.%d.%d"),
                       ip_hdr->source_addr.ip3,
                       ip_hdr->source_addr.ip2,
                       ip_hdr->source_addr.ip1,
                       ip_hdr->source_addr.ip0 );

        goto cleanup;
    }

    // verify message, if auth header present
    if( state->flags & WCOM_FRAME_FLAGS_AUTH ){
    
        if( wcom_ipv4_i8_verify_msg( msg ) < 0 ){
            
            // auth verify failed
            log_v_debug_P( PSTR("Auth fail") );

            goto cleanup;
        }
    }
    
    // check if we are the intended destination
    //bool ip_broadcast = ip_b_check_broadcast( ip_hdr->dest_addr );
    //bool ip_receive = ip_b_check_dest( ip_hdr->dest_addr );

    //if( ip_broadcast || ip_receive ){
        
    // Create a netmsg with the IPv4 datagram and send it to netmsg.
    // It kind of sucks to do the packet copying and then leaving netmsg
    // to throw away messages not bound for this node, but currently
    // there is no way around this without breaking the existing gateway
    // intercepts.

    uint16_t ipv4_len = wcom_ipv4_u16_get_ip_data_len( msg );

    // copy ip packet to netmsg
    netmsg_t netmsg = netmsg_nm_create( ip_hdr, ipv4_len );
    
    if( netmsg >= 0 ){
        
        if( !(state->flags & WCOM_FRAME_FLAGS_AUTH ) ){
            
            // if no auth header, set security disable flag in netmsg
            netmsg_v_set_flags( netmsg, NETMSG_FLAGS_WCOM_SECURITY_DISABLE );
        }

        stats_v_increment( STAT_WCOM_IPV4_PACKETS_RECEIVED );
        
        // add to netmsg receive queue
        netmsg_v_add_to_receive_q( netmsg );
    }
    
    bool broadcast = ip_b_check_broadcast( ip_hdr->dest_addr );

    // check if there is a route header
    wcom_ipv4_source_route_header_t *route_header = wcom_ipv4_p_get_route_header( msg );
    
    // check if we're broadcasting
    if( !broadcast ){
        
        // if we're not broadcasting, check if there is a route header

        // no route header, we're all done
        if( route_header == 0 ){
            
            goto cleanup;
        }

        // check if there are hops left
        if( ( route_header->next_hop + 1 ) == route_header->hop_count ){

            // no hops left, we're done
            goto cleanup;
        }
    }

    
    // check if not broadcasting (unicast routing)
    if( !broadcast ){

        //
        // route header stuff
        //
        
        // advance hop counter
        route_header->next_hop++;
        
        // get pointer to hops
        uint16_t *hops = (uint16_t *)( route_header + 1 );

        // set next hop in meta data, for the transmit routine
        state->next_hop = hops[route_header->next_hop];
        
        // add next hop forward cost to route header
        route_header->forward_cost += wcom_neighbors_u8_get_cost( state->next_hop );

        // check if routing is enabled
        if( !cfg_b_get_boolean( CFG_PARAM_ENABLE_ROUTING ) ){
             
            log_v_debug_P( PSTR("RouteError (not a router): Dest:%d.%d.%d.%d NextHop:%d"),
                           ip_hdr->dest_addr.ip3, 
                           ip_hdr->dest_addr.ip2, 
                           ip_hdr->dest_addr.ip1, 
                           ip_hdr->dest_addr.ip0,
                           state->next_hop );

            // we are not a router, send a route error message
            route2_i8_error( ip_hdr, 
                             hops, 
                             route_header->hop_count,
                             state->next_hop,
                             ROUTE2_ERROR_NOT_A_ROUTER );
            
            goto cleanup;
        }
        // check next hop status
        else if( !wcom_neighbors_b_is_neighbor( state->next_hop ) ){
            
            log_v_debug_P( PSTR("RouteError: Dest:%d.%d.%d.%d NextHop:%d"),
                           ip_hdr->dest_addr.ip3, 
                           ip_hdr->dest_addr.ip2, 
                           ip_hdr->dest_addr.ip1, 
                           ip_hdr->dest_addr.ip0,
                           state->next_hop );
                   
            // we don't have the next hop, send a route error
            route2_i8_error( ip_hdr, 
                             hops, 
                             route_header->hop_count,
                             state->next_hop,
                             ROUTE2_ERROR_NEXT_HOP_UNAVAILABLE );

            goto cleanup;
        }
    }
    else{

        // set broadcast next hop
        state->next_hop = WCOM_MAC_ADDR_BROADCAST;
    }

    // fun IP header TTL stuff (originally lived in netmsg)
    // *technically* we could live without doing this,
    // but for now it is nice to see the ttl decrement on pings and
    // it makes traceroute work.

    // decrement ttl
    ip_hdr->ttl--;
    
    // check if ttl expired
    if( ip_hdr->ttl == 0 ){
        
        // check protocol
        if( ip_hdr->protocol == IP_PROTO_ICMP ){
            // we only send the ttl exceeded message in response
            // to an ICMP message.
            // otherwise we'd be sending TTL exceeded all the time
            // because Sapphire uses a lot of protocols that set the TTL to 1.

            // adjust the ttl back to 1
            // this is done so the checksum on the original
            // ip packet is still valid (since the ICMP message will
            // include a copy of the original IP header)
            ip_hdr->ttl = 1;
        
            // send icmp ttl exceeded message
            icmp_v_send_ttl_exceeded( ip_hdr );
        }
        
        stats_v_increment( STAT_NETMSG_IPV4_TTL_EXPIRED );

        goto cleanup;
    }
        
    // recompute checksum
    ip_hdr->header_checksum = HTONS( ip_u16_ip_hdr_checksum( ip_hdr ) );


    log_v_debug_P( PSTR("Routing from:%d.%d.%d.%d to:%d.%d.%d.%d NextHop:%d"), 
                   ip_hdr->source_addr.ip3, 
                   ip_hdr->source_addr.ip2, 
                   ip_hdr->source_addr.ip1, 
                   ip_hdr->source_addr.ip0,
                   ip_hdr->dest_addr.ip3, 
                   ip_hdr->dest_addr.ip2, 
                   ip_hdr->dest_addr.ip1, 
                   ip_hdr->dest_addr.ip0,
                   state->next_hop );
    
    // reset offset
    state->offset = 0;

    // sign message
    wcom_ipv4_v_sign_msg( msg );
    
    // add to transmit queue
    list_v_insert_head( &tx_q, msg );
    
    // done!
    return;


cleanup:

    // release message
    wcom_ipv4_v_release_msg( msg );
}


PT_THREAD( wcom_ipv4_timeout_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
	
	while(1){
		
		static uint32_t time_wait;
		
		time_wait = WCOM_IPv4_TIMEOUT_TICK_RATE_MS;
		
		TMR_WAIT( pt, time_wait );
	    
        // increment ages of all rx streams
        wcom_ipv4_msg_t msg = rx_list.head;

        while( msg >= 0 ){
            
            wcom_ipv4_msg_state_t *state = list_vp_get_data( msg );
            
            // increment age
            state->age++;

            // check if expired
            if( state->age > WCOM_IPv4_RX_FRAGMENT_TIMEOUT ){
                
                // remove from list
                list_v_remove( &rx_list, msg );
               
                // release message
                wcom_ipv4_v_release_msg( msg );

                // since we've interfered with the list,
                // we'll break here and check the others the next time around
                break;
            }
            
            msg = list_ln_next( msg );
        }


		// increment ages in replay cache
		for( uint8_t i = 0; i < WCOM_IPv4_REPLAY_CACHE_ENTRIES; i++ ){
			
			if( replay_cache[i].age < WCOM_IPv4_REPLAY_MAX_AGE_TICKS ){
				
				replay_cache[i].age++;
				
				// entry is too old
				if( replay_cache[i].age >= WCOM_IPv4_REPLAY_MAX_AGE_TICKS ){
					
					// clear entry
					replay_cache[i].source_addr = ip_a_addr(0,0,0,0);
				}
			}
		}
	}
	
PT_END( pt );
}

bool wcom_ipv4_b_busy( void ){
	
    return ( list_u8_count( &tx_q ) >= WCOM_IPv4_MAX_TX_MESSAGES ); 
}


// send a packet contained in a memory handle.
// THIS DOES NOT RELEASE THE NETMSG
int8_t wcom_ipv4_i8_send_packet( netmsg_t netmsg ){

	// check if there are available queue slots
	if( wcom_ipv4_b_busy() ){
		
		return -1;
	}
    
    wcom_ipv4_msg_t msg = wcom_ipv4_m_create_from_netmsg( netmsg );
    
    if( msg < 0 ){
        
        return -1;
    }
    
    // queue the packet
    list_v_insert_head( &tx_q, msg );

	return 0;
}

// buf is assumed to have enough space for an entire RF frame
uint8_t wcom_ipv4_u8_get_mac_frame( wcom_ipv4_msg_t msg, uint8_t *buf ){
        
    wcom_ipv4_msg_state_t *state = list_vp_get_data( msg );
    
    uint16_t data_len = wcom_ipv4_u16_get_len( msg );

    // set up header information
    uint32_t *header = (uint32_t *)buf;		
    *header = wcom_ipv4_u32_build_header( state->flags, 
                                          state->tag, 
                                          data_len, 
                                          state->offset );
                             
    // compute amount of data to transmit in this frame
    uint8_t copy_len = mac_frame_data_length - sizeof(uint32_t);
    
    // bounds check against data in netmsg
    uint16_t length_remaining = data_len - state->offset;

    if( copy_len > length_remaining ){
        
        copy_len = length_remaining;
    }	

    // copy data
    memcpy( &buf[sizeof(uint32_t)], (void *)&state->data + state->offset, copy_len );
    
    // advance offset
    state->offset += copy_len;

    return copy_len + sizeof(uint32_t);
}

void wcom_ipv4_v_init_addr( uint16_t dest_addr, wcom_mac_addr_t *addr ){
    
    wcom_mac_v_init_addr( dest_addr, addr );
}

void wcom_ipv4_v_init_tx_state( wcom_ipv4_msg_t msg, tx_state_t *tx_state ){
    
    // get state
    wcom_ipv4_msg_state_t *state = list_vp_get_data( msg );

    // get ip header
    ip_hdr_t *ip_hdr = wcom_ipv4_p_get_ip_header( msg );
    
    // add to replay cache
    wcom_ipv4_b_add_to_replay_cache( ip_hdr->source_addr, state->tag );
    
    // set up addressing information
    wcom_ipv4_v_init_addr( wcom_ipv4_u16_get_next_hop( msg ), &tx_state->addr );      

    // set up transmit options
    // check if broadcasting
    if( tx_state->addr.dest_addr == WCOM_MAC_ADDR_BROADCAST ){
        
        tx_state->tx_options.ack_request = FALSE;
    }
    else if( cfg_b_get_boolean( CFG_PARAM_ENABLE_WCOM_ACK_REQUEST ) ){
        
        tx_state->tx_options.ack_request = TRUE;
    }
    else{
        
        tx_state->tx_options.ack_request = FALSE;
    }
    
    // this is for the MAC level frame security (which we are not using for IPv4)
    tx_state->tx_options.secure_frame = FALSE;

    // set protocol
    tx_state->tx_options.protocol = WCOM_MAC_PROTOCOL_IPv4;
}


PT_THREAD( ipv4_route_thread( pt_t *pt, void *state ) )
{ 
PT_BEGIN( pt );  

    while(1){
       
        // wait while idle
        THREAD_WAIT_WHILE( pt, list_b_is_empty( &route_list ) );
        
        // get message
        netmsg_t netmsg = list_ln_remove_tail( &route_list );
        
        // get ip header
        ip_hdr_t *ip_hdr = netmsg_vp_get_data( netmsg );

        // check for route
        route_query_t query = route2_q_query_ip( ip_hdr->dest_addr );
        route2_t route;
        
        if( route2_i8_get( &query, &route ) == 0 ){

            // route found, send message
            wcom_ipv4_i8_send_packet( netmsg );
            netmsg_v_release( netmsg );
        }
        else{
            
            // check if discovery is still in progress
            if( route2_b_discovery_in_progress( &query ) ){
                
                // requeue
                list_v_insert_tail( &route_list, netmsg );
            }
            else{
                
                // discovery expired
                
                // send host unreachable
                icmp_v_send_dest_unreachable( ip_hdr );
                
                // release message
                netmsg_v_release( netmsg );
            }
        }

        THREAD_YIELD( pt );
    }

PT_END( pt );
}



PT_THREAD( ipv4_tx_thread( pt_t *pt, void *state ) )
{ 
PT_BEGIN( pt );  

    while(1){
        
        // wait while idle
        THREAD_WAIT_WHILE( pt, list_b_is_empty( &tx_q ) );
        
        // get message
        tx_state.msg = list_ln_remove_tail( &tx_q );
        
        // initialize transmit state
        wcom_ipv4_v_init_tx_state( tx_state.msg, &tx_state );
/*
        // get ip header
        ip_hdr_t *ip_hdr = wcom_ipv4_p_get_ip_header( tx_state.msg );

        // check proto and log ICMP
        if( ip_hdr->protocol == IP_PROTO_ICMP ){
            
            icmp_hdr_t *icmp_hdr = (icmp_hdr_t *)( ip_hdr + 1 );

            log_v_debug_P( PSTR("ICMP Seq:%d to:%d.%d.%d.%d NextHop:%d"), 
                           icmp_hdr->sequence,
                           ip_hdr->dest_addr.ip3, 
                           ip_hdr->dest_addr.ip2, 
                           ip_hdr->dest_addr.ip1, 
                           ip_hdr->dest_addr.ip0,
                           tx_state.addr.dest_addr );
        }
*/
        // transmit loop
        do{
            
            // wait until we can transmit
            THREAD_WAIT_WHILE( pt, wcom_mac_b_busy() );

            // create a temporary buffer to construct the frame
            uint8_t buf[RF_MAX_FRAME_SIZE];
            
            uint8_t buf_size = wcom_ipv4_u8_get_mac_frame( tx_state.msg, buf );
            
            // create mac message, no auto release (we want to check status)
            tx_state.mac_msg = wcom_mac_m_create_tx_msg( &tx_state.addr, 
                                                         &tx_state.tx_options, 
                                                         WCOM_MAC_FCF_TYPE_DATA,
                                                         buf,
                                                         buf_size,
                                                         FALSE );
            
            // check if mac message was created, if not, 
            // we are probably out of memory
            if( tx_state.mac_msg < 0 ){
                
                goto clean_up;
            }

            // wait for transmit to complete
            THREAD_WAIT_WHILE( pt, !wcom_mac_b_msg_done( tx_state.mac_msg ) );
            
            // get the status
            wcom_mac_tx_status_t8 mac_status = wcom_mac_u8_get_tx_status( tx_state.mac_msg );
            
            // release mac message
            wcom_mac_v_release_msg( tx_state.mac_msg );
            tx_state.mac_msg = -1;

            if( mac_status == WCOM_MAC_TX_STATUS_OK ){
                
                stats_v_increment( STAT_WCOM_IPV4_FRAGMENTS_SENT );
            }
            else{
/*                 
                // logging:

                // get ip header
                ip_hdr_t *ip_hdr = wcom_ipv4_p_get_ip_header( tx_state.msg );

                // check proto and log ICMP
                if( ip_hdr->protocol == IP_PROTO_ICMP ){
                    
                    icmp_hdr_t *icmp_hdr = (icmp_hdr_t *)( ip_hdr + 1 );
                    
                    log_v_debug_P( PSTR("ICMP seq:%d to:%d.%d.%d.%d failed NextHop:%d"), 
                                   icmp_hdr->sequence,
                                   ip_hdr->dest_addr.ip3, 
                                   ip_hdr->dest_addr.ip2, 
                                   ip_hdr->dest_addr.ip1, 
                                   ip_hdr->dest_addr.ip0,
                                   tx_state.addr.dest_addr );
                }
*/
                stats_v_increment( STAT_WCOM_IPV4_TX_FAILURES );
                
                goto clean_up;
            }	
        } while( wcom_ipv4_u16_get_offset( tx_state.msg ) < wcom_ipv4_u16_get_len( tx_state.msg ) );


    clean_up:		
        
        // release data
        wcom_ipv4_v_release_msg( tx_state.msg );
    }

PT_END( pt );
}

