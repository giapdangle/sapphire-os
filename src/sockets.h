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

#ifndef __SOCKETS_H
#define __SOCKETS_H

#include "ip.h"
#include "threading.h"
#include "list.h"
#include "memory.h"
#include "netmsg.h"

#define SOCK_MEM_BUSY_THRESHOLD         1024

#define SOCK_EPHEMERAL_PORT_LOW         49152
#define SOCK_EPHEMERAL_PORT_HIGH        65535

#define SOCK_TIMER_TICK_MS              1000
#define SOCK_MAXIMUM_TIMEOUT            15 // in seconds

#define SOCK_UDPX_TIMER_TICK_MS         100


// enables loopback within the sockets module.
// if disabled, loopback occurs in netmsg
#define SOCK_LOOPBACK

typedef list_node_t socket_t;


typedef int8_t sock_type_t8;
//#define SOCK_RAW          0x01   // directly over IP - unsupported
//#define SOCK_STREAM       0x02  // TCP not supported
#define SOCK_DGRAM          0x04  // uses UDP
#define SOCK_UDPX_CLIENT    ( 0x10 | SOCK_DGRAM )
#define SOCK_UDPX_SERVER    ( 0x20 | SOCK_DGRAM )


#define SOCK_IS_DGRAM(type) ( type & SOCK_DGRAM )
#define SOCK_IS_UDPX_CLIENT(type) ( type & ( SOCK_UDPX_CLIENT & ~SOCK_DGRAM ) )
#define SOCK_IS_UDPX_SERVER(type) ( type & ( SOCK_UDPX_SERVER & ~SOCK_DGRAM ) )


typedef struct{
	ip_addr_t ipaddr;
	uint16_t port;
} sock_addr_t;

// UDP states
#define SOCK_UDP_STATE_IDLE             0
#define SOCK_UDP_STATE_RX_WAITING       1
#define SOCK_UDP_STATE_RX_DATA_PENDING  2
#define SOCK_UDP_STATE_RX_DATA_RECEIVED 3
#define SOCK_UDP_STATE_TIMED_OUT        4

// UDPX client states
#define SOCK_UDPX_STATE_WAIT_ACK        5





// options flags
typedef uint8_t sock_options_t8;
#define SOCK_OPTIONS_TTL_1                      0x01 // set TTL to 1, currently only works on UDP
#define SOCK_OPTIONS_NO_SECURITY                0x02 // disable packet security on wireless
#define SOCK_OPTIONS_SEND_ONLY                  0x04 // socket will not receive data
#define SOCK_OPTIONS_UDPX_NO_ACK_REQUEST        0x08 // socket will not request acks from the server (UDPX client only)
#define SOCK_OPTIONS_NO_WIRELESS                0x10 // socket will not transmit over the wireless

socket_t sock_s_create( sock_type_t8 type );
void sock_v_release( socket_t sock );
void sock_v_bind( socket_t sock, uint16_t port );
void sock_v_set_options( socket_t sock, sock_options_t8 options );

void sock_v_set_timeout( socket_t sock, uint8_t timeout );

void sock_v_get_raddr( socket_t sock, sock_addr_t *raddr );
void sock_v_set_raddr( socket_t sock, sock_addr_t *raddr );

uint16_t sock_u16_get_lport( socket_t sock );
int16_t sock_i16_get_bytes_read( socket_t sock );
void *sock_vp_get_data( socket_t sock );

bool sock_b_port_in_use( uint16_t port );

bool sock_b_busy( socket_t sock );

int8_t sock_i8_recvfrom( socket_t sock );
int16_t sock_i16_sendto( socket_t sock, void *buf, uint16_t bufsize, sock_addr_t *raddr );

void sock_v_recv_netmsg( netmsg_t netmsg );

void sock_v_init( void );
uint8_t sock_u8_count( void );


#endif

