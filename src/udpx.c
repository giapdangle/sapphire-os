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

#include "threading.h"
#include "timers.h"
#include "random.h"
#include "system.h"
#include "netmsg.h"
#include "statistics.h"
#include "udp.h"

#include "udpx.h"



netmsg_t udpx_nm_create( uint8_t msg_id,
                         uint8_t flags,
                         uint16_t source_port, 
                         uint16_t dest_port,
                         ip_addr_t dest_addr,
                         uint8_t ttl,
                         uint8_t *data,
                         uint16_t size ){
	
    // compute total packet size
    uint16_t total_pkt_size = size + sizeof(udpx_header_t) + sizeof(udp_header_t) + sizeof(ip_hdr_t);
    
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
                      size + sizeof(udp_header_t) + sizeof(udpx_header_t) );
    
	// get udp header
	udp_header_t *udp_header = (udp_header_t *)( (void *)ip_hdr + sizeof(ip_hdr_t) );

	// setup the header
	udp_v_init_header( udp_header, 
                       source_port,
                       dest_port,
                       size + sizeof(udpx_header_t) );
    
    // get udpx header
    udpx_header_t *udpx_header = (udpx_header_t *)( (void *)udp_header + sizeof(udp_header_t) );
    
    // set up udpx header
    udpx_header->flags  = flags & ( UDPX_FLAGS_SVR | UDPX_FLAGS_ARQ | UDPX_FLAGS_ACK );
    udpx_header->id     = msg_id;

    // get data pointer
    void *ptr = (void *)udpx_header + sizeof(udpx_header_t);
    
    // copy data
    memcpy( ptr, data, size );
    
    // compute checksum
    udp_header->checksum = HTONS(udp_u16_checksum( ip_hdr ));
    

    stats_v_increment( STAT_NETMSG_UDP_CREATED );
	
	return netmsg;
}







