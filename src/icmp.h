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


#ifndef _ICMP_H
#define _ICMP_H

#include "ip.h"
#include "netmsg.h"

typedef struct{
	uint8_t type;
	uint8_t code;
	uint16_t checksum;
	uint16_t id;
	uint16_t sequence;
} icmp_hdr_t;

typedef struct{
	icmp_hdr_t hdr;
	ip_hdr_t ip_hdr;
	uint8_t data[8];
} icmp_t;


#define ICMP_TYPE_ECHO_REPLY 			0
#define ICMP_TYPE_DEST_UNREACHABLE		3
#define ICMP_TYPE_ECHO_REQUEST 			8
#define ICMP_TYPE_TIME_EXCEEDED			11

#define ICMP_CODE_TTL_EXCEEDED          0
#define ICMP_CODE_NETWORK_UNREACHABLE   0
#define ICMP_CODE_DEST_UNREACHABLE      1
#define ICMP_CODE_PROTOCOL_UNREACHABLE  2
#define ICMP_CODE_PORT_UNREACHABLE      3



netmsg_t icmp_nm_create( icmp_hdr_t *icmp_hdr,
                         ip_addr_t dest_addr, 
                         uint8_t *data, 
                         uint16_t size );

void icmp_v_recv( netmsg_t netmsg );
void icmp_v_send_ttl_exceeded( ip_hdr_t *ip_hdr );
void icmp_v_send_dest_unreachable( ip_hdr_t *ip_hdr );

uint16_t icmp_u16_checksum( icmp_hdr_t *icmp_hdr, uint16_t total_length );



#endif
