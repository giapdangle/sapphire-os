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

#ifndef _ROUTING2_H
#define _ROUTING2_H

#include "ip.h"

#define ROUTE2_REPLAY_CACHE_ENTRIES  8

#define ROUTE2_MAXIMUM_AGE           120 // in seconds

#define ROUTE2_DISCOVERY_TRIES       3

#define ROUTE2_PROTOCOL_VERSION      1

#define ROUTE2_MAXIMUM_HOPS          8

#define ROUTE2_SERVER_PORT           24002

#define ROUTE2_DEST_FLAGS_IS_GATEWAY 0x01
#define ROUTE2_DEST_FLAGS_PROXY      0x40

typedef struct{
    ip_addr_t ip;
    uint16_t short_addr;
    uint8_t flags;
} route_query_t;

typedef struct{
    ip_addr_t dest_ip;
    uint16_t dest_short;
    uint8_t dest_flags;
    uint16_t cost;
    uint8_t age;
    uint8_t hop_count;
    uint16_t hops[ROUTE2_MAXIMUM_HOPS];
} route2_t;

// message definitions:
#define ROUTE2_MESSAGE_TYPE_RREQ 	1
typedef struct{ // route request
	uint8_t type; // 1
    uint8_t version;
    uint8_t flags;
    uint16_t tag;
    route_query_t query;
    uint16_t forward_cost;
    uint16_t reverse_cost;
    uint8_t hop_count;
    uint16_t hops[ROUTE2_MAXIMUM_HOPS];
} rreq2_t;

#define ROUTE2_MESSAGE_TYPE_RREP 	2
typedef struct{ // route reply
	uint8_t type; // 2
    uint8_t version;
    uint8_t flags;
    uint16_t tag;
    route_query_t query;
    uint16_t forward_cost;
    uint16_t reverse_cost;
    uint8_t hop_count;
    uint8_t hop_index;
    uint16_t hops[ROUTE2_MAXIMUM_HOPS];
} rrep2_t;

#define ROUTE2_MESSAGE_TYPE_RERR 	3
typedef struct{ // route error
	uint8_t type; // 3
	uint8_t version;
    uint8_t flags;
	uint8_t error;
	ip_addr_t dest_ip;
	ip_addr_t origin_ip;
    ip_addr_t error_ip;
    uint16_t unreachable_hop;
    uint8_t hop_count;
    uint8_t hop_index;
    uint16_t hops[ROUTE2_MAXIMUM_HOPS];
} rerr2_t;

// route error codes
#define ROUTE2_ERROR_NOT_A_ROUTER           1
#define ROUTE2_ERROR_NEXT_HOP_UNAVAILABLE   2


extern int8_t ( *route2_i8_proxy_routes )( route_query_t *query );


void route2_v_init( void );

bool route2_b_evaluate_query( route_query_t *query1, route_query_t *query2 );
route_query_t route2_q_query_ip( ip_addr_t ip );
route_query_t route2_q_query_short( uint16_t short_addr );
route_query_t route2_q_query_flags( uint8_t flags );
route_query_t route2_q_query_self( void );

uint8_t route_u8_count( void );
uint8_t route_u8_discovery_count( void );

int8_t route2_i8_get( route_query_t *query, route2_t *route );
int8_t route2_i8_query_list( route_query_t *query, route2_t *route );
int8_t route2_i8_query_neighbors( route_query_t *query, route2_t *route );

ip_addr_t route2_a_get_ip( uint16_t short_addr );

void route2_v_traffic( route2_t *route );

int8_t route2_i8_add( route2_t *route );
int8_t route2_i8_delete( route_query_t *query );

int8_t route2_i8_check_for_loops( route2_t *route );
int8_t route2_i8_check( route2_t *route );

int8_t route2_i8_request( route_query_t *query );
int8_t route2_i8_discover( route_query_t *query );
bool route2_b_discovery_in_progress( route_query_t *query );
void route2_v_cancel_discovery( route_query_t *query );

int8_t route2_i8_error( ip_hdr_t *ip_hdr,
                        uint16_t *hops,
                        uint8_t hop_count,
                        uint16_t unreachable_hop,
                        uint8_t error );



#endif

