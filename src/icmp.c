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

/*

Internet Control Messaging Protocol

This is a minimal ping and tracert only implementation

*/



#include "config.h"
#include "threading.h"
#include "system.h"
#include "netmsg.h"
#include "statistics.h"

#include "ip.h"

#include "icmp.h"

#include <string.h>

	
netmsg_t icmp_nm_create( icmp_hdr_t *icmp_hdr,
                         ip_addr_t dest_addr, 
                         uint8_t *data, 
                         uint16_t size ){
	
    // compute total packet size
    uint16_t total_pkt_size = size + sizeof(icmp_hdr_t) + sizeof(ip_hdr_t);
    
    // create netmsg object
    netmsg_t netmsg = netmsg_nm_create( 0, total_pkt_size );
    
    // check allocation
    if( netmsg < 0 ){
        
        return -1;
    }
    
    // get IP header
    ip_hdr_t *ip_hdr = netmsg_vp_get_data( netmsg );
    
    // setup the IP header
    ip_v_init_header( ip_hdr, 
                      dest_addr,
                      IP_PROTO_ICMP,
                      0,
                      size + sizeof(icmp_hdr_t) );
    
    void *ptr = ( void * )ip_hdr + sizeof(ip_hdr_t);
    
    // copy ICMP header
    memcpy( ptr, icmp_hdr, sizeof(icmp_hdr_t) );
    
    ptr += sizeof(icmp_hdr_t);
    
    // copy data
    memcpy( ptr, data, size );
    
    stats_v_increment( STAT_NETMSG_ICMP_CREATED );
    
    return netmsg;
}



void icmp_v_recv( netmsg_t netmsg ){
	
    // check security flags on netmsg
    // we only accept ICMP messages which are secured
    if( netmsg_u8_get_flags( netmsg ) & NETMSG_FLAGS_WCOM_SECURITY_DISABLE ){
        
        return;
    }

    // get the ip header
    ip_hdr_t *ip_hdr = netmsg_vp_get_data( netmsg );

	// make sure this function was called with the correct IP protocol
	ASSERT( ip_hdr->protocol == IP_PROTO_ICMP );
	
	// get icmp header
	icmp_hdr_t *icmp_hdr = ( icmp_hdr_t * )( (void *)ip_hdr + sizeof(ip_hdr_t) );
	
	// compute icmp size
	uint16_t icmp_size = HTONS(ip_hdr->total_length) - sizeof(ip_hdr_t);
	
	if( HTONS( icmp_hdr->checksum ) == icmp_u16_checksum( icmp_hdr, icmp_size ) ){
	
        stats_v_increment( STAT_NETMSG_ICMP_RECEIVED );

		if( icmp_hdr->type == ICMP_TYPE_ECHO_REQUEST ){
			
			// modify the existing packet to form the reply
			icmp_hdr->type = ICMP_TYPE_ECHO_REPLY;
			
			// recompute checksum
			icmp_hdr->checksum = HTONS( icmp_u16_checksum( icmp_hdr, icmp_size ) );
			
			uint8_t *data = ( uint8_t * )( (void *)icmp_hdr + sizeof(icmp_hdr_t) );
			
            // create netmsg
			netmsg_t netmsg = icmp_nm_create( icmp_hdr, 
                                              ip_hdr->source_addr, 
                                              data, 
                                              icmp_size - sizeof(icmp_hdr_t) );
            
            // check creation, and send if created
            if( netmsg >= 0 ){
                
                netmsg_v_add_to_transmit_q( netmsg );
            }
		}
	}	
}

	
void icmp_v_send_ttl_exceeded( ip_hdr_t *ip_hdr ){
	
	// create ICMP message
	icmp_t icmp;
	
	// create the ttl message
	icmp.ip_hdr = *ip_hdr;
	
	// get first 64 bits of data
	uint8_t *ptr = (void *)ip_hdr + sizeof(ip_hdr_t);
	
	memcpy( icmp.data, ptr, sizeof( icmp.data ) );
	
	// build ICMP header		
	icmp.hdr.type = ICMP_TYPE_TIME_EXCEEDED;
	icmp.hdr.code = ICMP_CODE_TTL_EXCEEDED;
	icmp.hdr.id = 0;
	icmp.hdr.sequence = 0;
	
	// compute checksum
	icmp.hdr.checksum = HTONS( icmp_u16_checksum( &icmp.hdr, sizeof( icmp ) ) );
	
	netmsg_t netmsg = icmp_nm_create( &icmp.hdr, 
                                      icmp.ip_hdr.source_addr, 
                                      (uint8_t *)&icmp.ip_hdr,
                                      sizeof(icmp.data) + sizeof(icmp.ip_hdr) );
    
    // check netmsg creation and set to send if successful
    if( netmsg >= 0 ){
        
        netmsg_v_add_to_transmit_q( netmsg );
    }
}


void icmp_v_send_dest_unreachable( ip_hdr_t *ip_hdr ){
	
    // create ICMP message
	icmp_t icmp;
	
	// create the ttl message
	icmp.ip_hdr = *ip_hdr;
	
	// get first 64 bits of data
	uint8_t *ptr = (void *)ip_hdr + sizeof(ip_hdr_t);
	
	memcpy( icmp.data, ptr, sizeof( icmp.data ) );
	
	// build ICMP header		
	icmp.hdr.type = ICMP_TYPE_DEST_UNREACHABLE;
	icmp.hdr.code = ICMP_CODE_DEST_UNREACHABLE;
	icmp.hdr.id = 0;
	icmp.hdr.sequence = 0;
	
	// compute checksum
	icmp.hdr.checksum = HTONS( icmp_u16_checksum( &icmp.hdr, sizeof( icmp ) ) );
	
	netmsg_t netmsg = icmp_nm_create( &icmp.hdr, 
                                      icmp.ip_hdr.source_addr, 
                                      (uint8_t *)&icmp.ip_hdr,
                                      sizeof(icmp.data) + sizeof(icmp.ip_hdr) );
    
    // check netmsg creation and set to send if successful
    if( netmsg >= 0 ){
        
        netmsg_v_add_to_transmit_q( netmsg );
    }
}


uint16_t icmp_u16_checksum( icmp_hdr_t *icmp_hdr, uint16_t total_length ){
	
	uint16_t icmp_checksum = icmp_hdr->checksum;
	
	// set checksum in header to 0
	icmp_hdr->checksum = 0;
	
	// compute checksum
	uint16_t checksum = ip_u16_checksum( icmp_hdr, total_length );
	
	// restore checksum in header
	icmp_hdr->checksum = icmp_checksum;
	
	return checksum;
}

