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
#include "config.h"
#include "threading.h"
#include "memory.h"
#include "system.h"
#include "statistics.h"
#include "list.h"
#include "icmp.h"
#include "udp.h"
#include "sockets.h"
#include "at86rf230.h"
#include "wcom_ipv4.h"
#include "wcom_mac.h"
#include "random.h"

#include "netmsg.h"

//#define NO_LOGGING
#include "logging.h"


PT_THREAD( tx_processor_thread( pt_t *pt, void *state ) );
PT_THREAD( rx_processor_thread( pt_t *pt, void *state ) );

static void default_receive_handler( netmsg_t netmsg );
static void default_mac_receive_handler( wcom_mac_addr_t *addr, 
                                         wcom_mac_rx_options_t *options,
                                         uint8_t *data, 
                                         uint8_t len );

int8_t ( *netmsg_i8_transmit_msg )( netmsg_t msg );
int8_t ( *netmsg_i8_send_802_15_4_mac )( wcom_mac_addr_t *addr, 
                                         wcom_mac_tx_options_t *options, 
                                         uint16_t type,
                                         uint8_t *data, 
                                         uint8_t length );
void ( *netmsg_v_receive_msg )( netmsg_t msg );
void ( *netmsg_v_receive_802_15_4_raw )( rx_frame_buf_t *frame );
void ( *netmsg_v_receive_802_15_4_mac )( wcom_mac_addr_t *addr, 
                                         wcom_mac_rx_options_t *options,
                                         uint8_t *data, 
                                         uint8_t len );

static list_t tx_q;
static list_t rx_q;

// initialize netmsg
void netmsg_v_init( void ){
    
    list_v_init( &tx_q );
    list_v_init( &rx_q );
    
    // set default handlers
    netmsg_i8_transmit_msg          = wcom_ipv4_i8_send_packet;
    netmsg_i8_send_802_15_4_mac     = wcom_mac_i8_transmit_frame;
    netmsg_v_receive_msg            = default_receive_handler;
    netmsg_v_receive_802_15_4_raw   = wcom_mac_v_rx_handler;
    netmsg_v_receive_802_15_4_mac   = default_mac_receive_handler;

    // create process threads
    thread_t_create( tx_processor_thread,
                     PSTR("netmsg_transmit"),
                     0,
                     0 );

    thread_t_create( rx_processor_thread,
                     PSTR("netmsg_receive"),
                     0,
                     0 );
}

uint8_t netmsg_u8_count( void ){
    
    return list_u8_count( &tx_q ) + list_u8_count( &rx_q );
}

netmsg_t netmsg_nm_create( void *data, uint16_t len ){
    
    // check message count
    if( netmsg_u8_count() >= NETMSG_MAX_MESSAGES ){
        
        sys_v_set_warnings( SYS_WARN_NETMSG_FULL );
        
        // can't queue any more messages
        return -1;
    }

    // create list node
    list_node_t n = list_ln_create_node( 0, ( sizeof(netmsg_state_t) - 1 ) + len );

    // check handle
    if( n < 0 ){
    
        stats_v_increment( STAT_NETMSG_CREATE_OBJECT_FAILED );
        
		return -1;
    }

    // get state pointer and initialize
	netmsg_state_t *msg = list_vp_get_data( n );
	
    // check if data is provided
    if( data != 0 ){
        
        // copy data into netmsg object
        memcpy( &msg->data, data, len );
    }
	
    // init flags
    msg->flags = 0;

    stats_v_increment( STAT_NETMSG_OBJECT_CREATED );
    
	return n;
}

void netmsg_v_release( netmsg_t netmsg ){
    
    list_v_release_node( netmsg );
}

void netmsg_v_set_flags( netmsg_t netmsg, netmsg_flags_t flags ){
    
    // get state pointer
    netmsg_state_t *msg = list_vp_get_data( netmsg );   

    msg->flags = flags;
}

netmsg_flags_t netmsg_u8_get_flags( netmsg_t netmsg ){
    
    // get state pointer
    netmsg_state_t *msg = list_vp_get_data( netmsg );   

    return msg->flags;
}

void netmsg_v_add_to_transmit_q( netmsg_t netmsg ){
    
    list_v_insert_head( &tx_q, netmsg );
}

void netmsg_v_add_to_receive_q( netmsg_t netmsg ){

    list_v_insert_head( &rx_q, netmsg );
}

netmsg_t netmsg_nm_remove_from_transmit_q( void ){
    
    return list_ln_remove_tail( &tx_q );
}

netmsg_t netmsg_nm_remove_from_receive_q( void ){
    
    return list_ln_remove_tail( &rx_q );
}


#ifdef ENABLE_EXTENDED_VERIFY
void *_netmsg_vp_get_data( netmsg_t netmsg, FLASH_STRING_T file, int line ){
    
    // check validity of handle and assert if there is a failure.
    // this overrides the system based assert so we can insert the file and line
    // from the caller.
    if( mem2_b_verify_handle( netmsg ) == FALSE ){
        
        assert( 0, file, line );
    }

#else
void *netmsg_vp_get_data( netmsg_t netmsg ){
#endif
    // get state pointer
    netmsg_state_t *msg = list_vp_get_data( netmsg );   
    
    return (void *)&msg->data;
}

uint16_t netmsg_u16_get_len( netmsg_t netmsg ){
    
    return list_u16_node_size( netmsg ) - ( sizeof(netmsg_state_t) - 1 );
}


PT_THREAD( tx_processor_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
    
    while(1){
        
        // wait while transmit queue is empty
        THREAD_WAIT_WHILE( pt, list_b_is_empty( &tx_q ) );
        
        // get netmsg
        netmsg_t msg = netmsg_nm_remove_from_transmit_q();
        
        // get the ip header
        ip_hdr_t *ip_hdr = netmsg_vp_get_data( msg );
        
        // check for loopback
        if( ip_b_check_loopback( ip_hdr->dest_addr ) ){
            
            // run local receive
            netmsg_v_local_receive( msg );

            // release message
            netmsg_v_release( msg );
        }
        else{

            // attempt to transmit
            netmsg_i8_transmit_msg( msg );
            
            // release message
            netmsg_v_release( msg );
        }
        /*
        // attempt to transmit message
        else if( netmsg_i8_transmit_msg( msg ) == 0 ){
            
            // release message
            netmsg_v_release( msg );
        }
        else{

            #define DROP_PROBABILITY 32768

            // random drop
            if( rnd_u16_get_int() < DROP_PROBABILITY ){
                
                // release message
                netmsg_v_release( msg );
            }
            // don't drop
            else{
                
                // could not transmit, requeue
                list_v_insert_tail( &tx_q, msg ); 
            }
        }*/

        THREAD_YIELD( pt );
    }

PT_END( pt );
}


PT_THREAD( rx_processor_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
    
    while(1){

        // wait while receive queue is empty
        THREAD_WAIT_WHILE( pt, list_b_is_empty( &rx_q ) );
        
        // get netmsg
        netmsg_t msg = netmsg_nm_remove_from_receive_q();
        
        netmsg_v_receive_msg( msg );
        
        THREAD_YIELD( pt );
    }

PT_END( pt );
}

static void default_receive_handler( netmsg_t netmsg ){
    
    netmsg_v_local_receive( netmsg );

    netmsg_v_release( netmsg );
}

static void default_mac_receive_handler( wcom_mac_addr_t *addr, 
                                         wcom_mac_rx_options_t *options,
                                         uint8_t *data, 
                                         uint8_t len ){
    
    if( options->protocol == WCOM_MAC_PROTOCOL_IPv4 ){
        
        wcom_ipv4_v_received_mac_frame( addr, options, data, len );
    }
}

void netmsg_v_local_receive( netmsg_t netmsg ){
    
    // get the ip header
    ip_hdr_t *ip_hdr = netmsg_vp_get_data( netmsg );
    
    // check the IP header
    if( ip_b_verify_header( ip_hdr ) != TRUE ){
        
        // IPv4 header failed an integrity check
        stats_v_increment( STAT_NETMSG_IPV4_CHECKSUMS_FAILED );
        
        return;
    }
    
    bool ip_broadcast = ip_b_check_broadcast( ip_hdr->dest_addr );
    bool ip_receive = ip_b_check_dest( ip_hdr->dest_addr );
    
    // check destination address
    // check if packet is for this node
    if( ( ip_receive == TRUE ) || ( ip_broadcast == TRUE ) ){
        
        // set the netmsg protocol for the message router
        if( ip_hdr->protocol == IP_PROTO_ICMP ){
        
            // call the icmp receive handler
            icmp_v_recv( netmsg );
        }
        else if( ip_hdr->protocol == IP_PROTO_UDP ){
            
            stats_v_increment( STAT_NETMSG_UDP_RECEIVED );
            
            // call the sockets receive handler
            sock_v_recv_netmsg( netmsg );
        }
    }
}


