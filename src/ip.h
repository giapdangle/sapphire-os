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



#ifndef _IP_H
#define _IP_H

#include "memory.h"

#define IP_VERSION 4
#define IP_HEADER_LENGTH 5 // IP options are not supported, so the header length must be 5

#define IP_HEADER_LENGTH_BYTES ( IP_HEADER_LENGTH * 4 )

#define IP_DEFAULT_TTL 64

#define MAX_IP_PACKET_SIZE 576 // this can be changed, but MUST be at least 576!

#define MIN_IP_PACKET_SIZE 20 // NEVER CHANGE THIS! EVER!

// protocols
#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP 6
#define IP_PROTO_UDP 17


// convert between network byte order and host
#define HTONS(n) (uint16_t)((((uint16_t) (n)) << 8) | (((uint16_t) (n)) >> 8))
#define HTONL(n) (uint32_t)(((((uint32_t) (n)) << 24) & 0xff000000) | ((((uint32_t) (n)) >> 24) & 0x000000ff) | ((((uint32_t) (n)) << 8) & 0x00ff0000) | ((((uint32_t) (n)) >> 8) & 0x0000ff00))

typedef struct{
	uint8_t ip3;
	uint8_t ip2;
	uint8_t ip1;
	uint8_t ip0;
} ip_addr_t;

typedef struct{
	uint8_t vhl; // version and header length
	uint8_t ds; // differentiated services (unused)
	uint16_t total_length; // total length of IP packet
	uint16_t id; // packet ID (unused)
	uint16_t flags_offset; // flags (3 bits) and offset (13 bits)
	uint8_t ttl; // time to live
	uint8_t protocol; // next protocol
	uint16_t header_checksum; // self-explanatory
	ip_addr_t source_addr;
	ip_addr_t dest_addr;
} ip_hdr_t;


void ip_v_init( void );

void ip_v_init_header( ip_hdr_t *ip_hdr, 
                       ip_addr_t dest_addr,
                       uint8_t protocol,
                       uint8_t ttl,
                       uint16_t len );

bool ip_b_verify_header( ip_hdr_t *ip_hdr );
uint16_t ip_u16_ip_hdr_checksum( ip_hdr_t *ip_hdr );
uint16_t ip_u16_checksum( void *data, uint16_t len );
ip_addr_t ip_a_addr( uint8_t ip3, uint8_t ip2, uint8_t ip1, uint8_t ip0 );
bool ip_b_is_zeroes( ip_addr_t addr );
bool ip_b_mask_compare( ip_addr_t subnet_addr, 
						ip_addr_t subnet_mask, 
						ip_addr_t dest_addr );
bool ip_b_addr_compare( ip_addr_t addr1, ip_addr_t addr2 );
bool ip_b_check_broadcast( ip_addr_t dest_addr );
bool ip_b_check_dest( ip_addr_t dest_addr );
bool ip_b_check_loopback( ip_addr_t dest_addr );
uint32_t ip_u32_to_int( ip_addr_t ip );
ip_addr_t ip_a_from_int( uint32_t i );

#endif


