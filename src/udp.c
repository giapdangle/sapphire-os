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

User Datagram Protocol Packet Processing Routines

*/

#include <avr/io.h>

#include "system.h"

#include "netmsg.h"
#include "statistics.h"

#include "ip.h"
#include "udp.h"

void udp_v_init_header( udp_header_t *header,
                        uint16_t source_port,
                        uint16_t dest_port,
                        uint16_t size ){
    
	// setup the header
	header->source_port = HTONS(source_port);
	header->dest_port = HTONS(dest_port);
	header->length = HTONS(size + sizeof(udp_header_t));
	header->checksum = 0;
}                        

netmsg_t udp_nm_create( uint16_t source_port, 
                        uint16_t dest_port,
                        ip_addr_t dest_addr,
                        uint8_t ttl,
                        uint8_t *data,
                        uint16_t size ){
	
    // compute total packet size
    uint16_t total_pkt_size = size + sizeof(udp_header_t) + sizeof(ip_hdr_t);
    
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
                      IP_PROTO_UDP,
                      ttl,
                      size + sizeof(udp_header_t) );
    
	// get udp header
	udp_header_t *udp_header = (udp_header_t *)( (void *)ip_hdr + sizeof(ip_hdr_t) );
    

	// setup the header
	udp_v_init_header( udp_header, 
                       source_port,
                       dest_port,
                       size );

    // get data pointer
    void *ptr = (void *)udp_header + sizeof(udp_header_t);
    
    // copy data
    memcpy( ptr, data, size );
    
    // compute checksum
    udp_header->checksum = HTONS(udp_u16_checksum( ip_hdr ));
    
    stats_v_increment( STAT_NETMSG_UDP_CREATED );
	
	return netmsg;
}


uint16_t udp_u16_checksum( ip_hdr_t *ip_hdr ){
	
	// get the udp header
	udp_header_t *udp_hdr = (udp_header_t *)( (void*)ip_hdr + sizeof(ip_hdr_t) );
	
    uint16_t original_checksum = udp_hdr->checksum;
    udp_hdr->checksum = 0;
    
	// create the udp/ipv4 pseudoheader
	udp_checksum_header_t hdr;
	
	hdr.source_addr = ip_hdr->source_addr;
	hdr.dest_addr = ip_hdr->dest_addr;
	hdr.zeroes = 0;
	hdr.protocol = IP_PROTO_UDP;
	hdr.udp_length = udp_hdr->length;
	
	// run the checksum over the pseudoheader
	uint16_t checksum = 0;
	uint8_t *ptr = (uint8_t *)&hdr;
	uint16_t len = sizeof(udp_checksum_header_t);
	
	while( len > 1 ){
		
		uint16_t temp = ( ptr[0] << 8 ) + ptr[1];
	
		checksum += temp;
		
		if( checksum < temp ){
			
			checksum++; // add carry
		}
            
		ptr += 2;
		len -= 2;
	}
	
	// run the checksum over the rest of the datagram
	ptr = (uint8_t *)udp_hdr;
	len = HTONS(udp_hdr->length);
	
	while( len > 1 ){
		
		uint16_t temp = ( ptr[0] << 8 ) + ptr[1];
	
		checksum += temp;
		
		if( checksum < temp ){
			
			checksum++; // add carry
		}
		
		ptr += 2;
		len -= 2;
	}
	
	// check if last byte
	if( len == 1 ){
		
		uint16_t temp = ( ptr[0] << 8 );
		
		checksum += temp;
		
		if( checksum < temp ){
			
			checksum++; // add carry
		}
	}
	
    udp_hdr->checksum = original_checksum;
    
	// return one's complement of checksum
	return ~checksum;
}


