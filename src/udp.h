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



#ifndef _UDP_H
#define _UDP_H

#include "ip.h"
#include "netmsg.h"

typedef struct{
	uint16_t source_port;
	uint16_t dest_port;
	uint16_t length;
	uint16_t checksum;
} udp_header_t;

// header used for the UDP checksum algorithm for IPv4
typedef struct{
	ip_addr_t source_addr;
	ip_addr_t dest_addr;
	uint8_t zeroes;
	uint8_t protocol;
	uint16_t udp_length;
	//udp_header_t udp_header;
} udp_checksum_header_t;


void udp_v_init_header( udp_header_t *header,
                        uint16_t source_port,
                        uint16_t dest_port,
                        uint16_t size );

netmsg_t udp_nm_create( uint16_t source_port, 
                        uint16_t dest_port,
                        ip_addr_t dest_addr, 
                        uint8_t ttl,
                        uint8_t *data,
                        uint16_t size );

uint16_t udp_u16_checksum( ip_hdr_t *ip_hdr );



#endif

