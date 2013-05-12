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

#include "system.h"
#include "memory.h"
#include "list.h"
#include "config.h"
#include "threading.h"
#include "timers.h"
#include "ip.h"
#include "fs.h"
#include "sockets.h"
#include "statistics.h"
#include "wcom_neighbors.h"
#include "wcom_mac.h"
#include "random.h"

#include "routing2.h"

//#define NO_LOGGING
#include "logging.h"

typedef struct{
    route_query_t query;
    uint8_t tries;
} discovery_t;

typedef struct{
    uint16_t tag;
    uint16_t source_addr;
} replay_cache_entry_t;

static socket_t sock;

static list_t route_list;
static list_t disc_list;

static replay_cache_entry_t replay_cache[ROUTE2_REPLAY_CACHE_ENTRIES];
static uint8_t replay_cache_ptr;

int8_t ( *route2_i8_proxy_routes )( route_query_t *query );

PT_THREAD( route_server_thread( pt_t *pt, void *state ) );
PT_THREAD( route_discovery_thread( pt_t *pt, void *state ) );
PT_THREAD( route_optimizer_thread( pt_t *pt, void *state ) );
PT_THREAD( route_age_thread( pt_t *pt, void *state ) );

// returns true if the tag is already in the cache.
// returns false if it was not in the cache.
static bool add_to_replay_cache( uint16_t source_addr, uint16_t tag ){
    
    // search the cache for a match
    for( uint8_t i = 0; i < ROUTE2_REPLAY_CACHE_ENTRIES; i++ ){
        
        // check for match
        if( ( replay_cache[i].source_addr == source_addr ) &&
            ( replay_cache[i].tag == tag ) ){
            
            // return match found
            return TRUE;
        }
    }
    
    // no match
    // add entry and increment pointer
    replay_cache[replay_cache_ptr].source_addr  = source_addr;
    replay_cache[replay_cache_ptr].tag          = tag;
    
    replay_cache_ptr++;
    
    if( replay_cache_ptr >= ROUTE2_REPLAY_CACHE_ENTRIES ){
        
        replay_cache_ptr = 0;
    }
    
    return FALSE;
}

static uint16_t vfile( vfile_op_t8 op, uint32_t pos, void *ptr, uint16_t len ){
    
    // flatten the route list

    // the pos and len values are already bounds checked by the FS driver
    switch( op ){
        
        case FS_VFILE_OP_READ:
            len = list_u16_flatten( &route_list, pos, ptr, len );           
            break;

        case FS_VFILE_OP_SIZE:
            len = list_u8_count( &route_list ) * sizeof(route2_t);
            break;

        default:
            len = 0;
            break;
    }

    return len;
}


static int8_t default_proxy_route( route_query_t *query ){
    
    return -1;
}


void route2_v_init( void ){
    
    // init route list
    list_v_init( &route_list );
    list_v_init( &disc_list );

    // create socket
    sock = sock_s_create( SOCK_DGRAM );
    
    // assert if socket was not created
    ASSERT( sock >= 0 );

    // set socket TTL to 1
    sock_v_set_options( sock, SOCK_OPTIONS_TTL_1 );

	// bind to port
	sock_v_bind( sock, ROUTE2_SERVER_PORT );
    
    // start server thread
    thread_t_create( route_server_thread,
                     PSTR("route_server"),
                     0,
                     0 );

    // start discovery thread
    thread_t_create( route_discovery_thread,
                     PSTR("route_discovery"),
                     0,
                     0 );

    // start optimizer
    //thread_t_create( route_optimizer_thread );

    // start aging thread
    thread_t_create( route_age_thread,
                     PSTR("route_aging"),
                     0,
                     0 );

    // create vfile
    fs_f_create_virtual( PSTR("routes"), vfile );
    
    // extra route hook defaults to 0
    route2_i8_proxy_routes = default_proxy_route;
}

bool route2_b_evaluate_query( route_query_t *query1, route_query_t *query2 ){
    
    // check IP first
    if( !ip_b_is_zeroes( query1->ip ) && ip_b_addr_compare( query1->ip, query2->ip ) ){ 
            
        return TRUE;
    }

    // check short address
    if( ( query1->short_addr != 0 ) && ( query1->short_addr == query2->short_addr ) ){

        return TRUE;
    }

    // check flags
    if( ( query1->flags & ROUTE2_DEST_FLAGS_IS_GATEWAY ) &&
        ( query2->flags & ROUTE2_DEST_FLAGS_IS_GATEWAY ) ){
        
        return TRUE;
    }
        
    return FALSE;
}

route_query_t route2_q_query_ip( ip_addr_t ip ){
    
    route_query_t query;
    memset( &query, 0, sizeof(query) );

    query.ip = ip;
    
    return query;
}

route_query_t route2_q_query_short( uint16_t short_addr ){

    route_query_t query;
    memset( &query, 0, sizeof(query) );

    query.short_addr = short_addr;
    
    return query;
}

route_query_t route2_q_query_flags( uint8_t flags ){

    route_query_t query;
    memset( &query, 0, sizeof(query) );

    query.flags = flags;
    
    return query;
}

// return a route query with our info
route_query_t route2_q_query_self( void ){
    
    route_query_t query;
    
    cfg_i8_get( CFG_PARAM_IP_ADDRESS, &query.ip );
    cfg_i8_get( CFG_PARAM_802_15_4_SHORT_ADDRESS, &query.short_addr );
    
    query.flags = 0;

    if( cfg_b_is_gateway() ){
        
        query.flags = ROUTE2_DEST_FLAGS_IS_GATEWAY;
    }
    
    return query;
}

uint8_t route_u8_count( void ){
    
    return list_u8_count( &route_list );
}

uint8_t route_u8_discovery_count( void ){
    
    return list_u8_count( &disc_list );
}

// get a route for given query.
// returns -1 if no route found.
// does not perform a discovery (searches local cache only).
// returns route meta data and first hop in route.
int8_t route2_i8_get( route_query_t *query, route2_t *route ){
    
    ASSERT( route != 0 );
    
    // initialize route to 0s
    memset( route, 0, sizeof(route2_t) );

    // set current cost to infinity
    route->cost = 65535;

    // check if query is for self
    route_query_t self_query = route2_q_query_self();

    if( route2_b_evaluate_query( query, &self_query ) ){
        
        // set loopback route
        route->dest_ip      = self_query.ip;
        route->dest_short   = self_query.short_addr;
        route->dest_flags   = self_query.flags;
        route->cost         = 0;
        route->age          = 0;
        route->hop_count    = 2;
        route->hops[0]      = cfg_u16_get_short_addr();
        route->hops[1]      = cfg_u16_get_short_addr();

        return 0;
    }

    // check if query is for a broadcast
    if( ip_b_check_broadcast( query->ip ) ){
        
        // set broadcast route
        route->dest_ip      = query->ip;
        route->dest_short   = WCOM_MAC_ADDR_BROADCAST;
        route->dest_flags   = 0;
        route->cost         = 0;
        route->age          = 0;
        route->hop_count    = 2;
        route->hops[0]      = cfg_u16_get_short_addr();
        route->hops[1]      = WCOM_MAC_ADDR_BROADCAST;

        return 0;
    }

    // init cost to maximum
    route->cost = 65535;
    
    // query list
    route2_i8_query_list( query, route );

    // query neighbors
    route2_i8_query_neighbors( query, route );
        
    // check if route was found
    if( route->cost == 65535 ){
        
        return -1;
    }
    
    return 0;
}   

// query the route list
int8_t route2_i8_query_list( route_query_t *query, route2_t *route ){
    
    ASSERT( route != 0 );

    // start iteration
    list_node_t ln = route_list.head;

    while( ln >= 0 ){
        
        route2_t *route_data = list_vp_get_data( ln );
        
        // create query from route data
        route_query_t check_query;
        check_query.ip          = route_data->dest_ip;
        check_query.short_addr  = route_data->dest_short;
        check_query.flags       = route_data->dest_flags;

        // evaluate query and compare costs
        if( ( route2_b_evaluate_query( query, &check_query ) ) &&
            ( route_data->cost < route->cost ) ){
            
            // set current route
            *route = *route_data;

            return 0;
        }

        ln = list_ln_next( ln );
    }

    return -1;
}

// query the neighbor list
int8_t route2_i8_query_neighbors( route_query_t *query, route2_t *route ){
    
    uint16_t short_addr = 0;

    // check short address
    if( ( query->short_addr != 0 ) &&
        ( wcom_neighbors_b_is_neighbor( query->short_addr ) ) ){
        
        short_addr = query->short_addr;
    }
    // check IP
    else if( !ip_b_is_zeroes( query->ip ) ){ 
        
        short_addr = wcom_neighbors_u16_get_short( query->ip );
    }
    
    // check flags
    else if( query->flags & ROUTE2_DEST_FLAGS_IS_GATEWAY ){

        short_addr = wcom_neighbors_u16_get_gateway();
    }
    
    // check if neighbor was not found
    if( short_addr == 0 ){
        
        return -1;
    }
    
    // get neighbor cost
    uint16_t neighbor_cost = wcom_neighbors_u8_get_cost( short_addr );
    
    // check if cost is not better than supplied route
    if( neighbor_cost >= route->cost ){
        
        return -1;
    }

    wcom_neighbor_t *neighbor = wcom_neighbors_p_get_neighbor( short_addr );
    ASSERT( neighbor != 0 );
    
    // set up route
    route->dest_ip          = neighbor->ip;
    route->dest_short       = neighbor->short_addr;
    route->dest_flags       = 0;
    route->cost             = neighbor_cost;
    route->age              = 0;
    route->hop_count        = 2;
    route->hops[0]          = cfg_u16_get_short_addr();
    route->hops[1]          = neighbor->short_addr;
    
    return 0;    
}

ip_addr_t route2_a_get_ip( uint16_t short_addr ){
        
    route_query_t query = route2_q_query_short( short_addr );

    route2_t route;
    
    if( route2_i8_get( &query, &route ) < 0 ){
        
        return ip_a_addr(0,0,0,0);
    }
    
    return route.dest_ip;
}   

static bool compare_route_dest( route2_t *route1, route2_t *route2 ){
    
    // check if IP address is present and compare if so
    if( ( !ip_b_is_zeroes( route1->dest_ip ) ) &&
        ( ip_b_addr_compare( route1->dest_ip, route2->dest_ip ) ) ){
        
        return TRUE;
    }
    
    // check if short address is present and destination is not
    // a proxy and compare if so
    if( ( route1->dest_short != 0 ) &&
        ( ( route1->dest_flags & ROUTE2_DEST_FLAGS_PROXY ) == 0 ) &&
        ( route1->dest_short == route2->dest_short ) ){
        
        return TRUE;
    }

    return FALSE;
}

// indicate traffic on the given route
void route2_v_traffic( route2_t *route ){
    
    // look for matching route
    list_node_t ln = route_list.head;

    while( ln >= 0 ){
        
        route2_t *route_data = list_vp_get_data( ln );
        
        if( compare_route_dest( route, route_data ) ){
            
            // reset age
            route_data->age = 0;

            return;
        }

        ln = list_ln_next( ln );
    }
}

int8_t route2_i8_add( route2_t *route ){
    
    // check for errors
    if( route2_i8_check( route ) < 0 ){
    
        return -1;
    }

    // search for duplicate
    list_node_t ln = route_list.head;

    while( ln >= 0 ){
        
        route2_t *route_data = list_vp_get_data( ln );
        
        // compare destination short and if match, compare costs.
        // if cost is equal or better, replace the route.
        // check for equal cost allows us to favor the newer route if the cost is the same.
        if( compare_route_dest( route, route_data ) ){

            // check if cost is lower
            if( route->cost <= route_data->cost ){
                
                // copy route
                *route_data = *route;

                // reset age
                route_data->age = 0;
            }

            // since we do this check when we insert into the route list,
            // we know there will never be duplicates so we can exit when we find
            // the first match
            return 0;
        }

        ln = list_ln_next( ln );
    }

    
    // check free space
    uint16_t max_routes;
    cfg_i8_get( CFG_PARAM_MAX_ROUTES, &max_routes );
    
    // check that free space is reasonable
    if( max_routes < 2 ){
        
        max_routes = 2;
        cfg_v_set( CFG_PARAM_MAX_ROUTES, &max_routes );
    }

    if( route_u8_count() >= max_routes ){
        
        log_v_warn_P( PSTR("Route list full") );

        return -1;
    }
    
    // make sure age is reset
    route->age = 0;

    // create route
    ln = list_ln_create_node( route, sizeof(route2_t) );

    // check creation
    if( ln < 0 ){
        
        return -1;
    }

    // insert
    list_v_insert_tail( &route_list, ln );

    return 0;
}

// delete a route matching query
int8_t route2_i8_delete( route_query_t *query ){
    
    // loop through routes
    list_node_t ln = route_list.head;

    while( ln >= 0 ){
        
        route2_t *route_data = list_vp_get_data( ln );

        // create query from route data
        route_query_t check_query;
        check_query.ip          = route_data->dest_ip;
        check_query.short_addr  = route_data->dest_short;
        check_query.flags       = route_data->dest_flags;

        // evaluate query
        if( route2_b_evaluate_query( query, &check_query ) ){
            
            // remove from list
            list_v_remove( &route_list, ln );

            // release node
            list_v_release_node( ln );

            return 0;
        }

        ln = list_ln_next( ln );
    }

    return -1;
}

int8_t route2_i8_check_for_loops( route2_t *route ){
    
    for( uint8_t i = 0; i < ( route->hop_count - 1 ); i++ ){
        
        for( uint8_t j = ( i + 1 ); j < route->hop_count; j++ ){
            
            if( route->hops[i] == route->hops[j] ){
                
                return -1; // loop detected
            }
        }
    }

    return 0;
}

// checks a route for errors
int8_t route2_i8_check( route2_t *route ){
    
    // check for loops
    if( route2_i8_check_for_loops( route ) < 0 ){

        log_v_debug_P( PSTR("Route to:%d has a loop"), route->dest_short );
        
        return -1;
    }

    // check for missing next hops
    if( !wcom_neighbors_b_is_neighbor( route->hops[1] ) ){
        
        log_v_debug_P( PSTR("Route to:%d missing next hop:%d"), route->dest_short, route->hops[1] );

        return -1;
    }

    return 0;
}

void route2_v_init_rreq( route_query_t *query, rreq2_t *rreq ){
    
    rreq->type           = ROUTE2_MESSAGE_TYPE_RREQ;
    rreq->version        = ROUTE2_PROTOCOL_VERSION;
    rreq->flags          = 0;
    rreq->tag            = rnd_u16_get_int();
    rreq->query          = *query;
    rreq->forward_cost   = 0;
    rreq->reverse_cost   = 0;
    rreq->hop_count      = 1;

    memset( rreq->hops, 0, sizeof(rreq->hops) );

    // set up first hop (us)
    rreq->hops[0]    = cfg_u16_get_short_addr();
}

// init a route reply from a route request
void route2_v_init_rrep( rreq2_t *rreq, rrep2_t *rrep, bool proxy ){
    
    ASSERT( rreq->hop_count >= 2 );
    
    rrep->type           = ROUTE2_MESSAGE_TYPE_RREP;
    rrep->version        = ROUTE2_PROTOCOL_VERSION;
    rrep->flags          = 0;
    rrep->tag            = rreq->tag;
    rrep->forward_cost   = 0;
    rrep->reverse_cost   = rreq->reverse_cost;
    rrep->hop_count      = rreq->hop_count;
    rrep->hop_index      = rreq->hop_count - 2;
    rrep->query.flags    = 0;
    
    // check if this is a proxy route
    if( proxy ){

        // proxy route
        rrep->query = rreq->query;

        // set proxy flag on route.
        // note if the gateway flag gets set on a proxy route,
        // it will confuse the neighbor module in gateway route selection.
        rrep->query.flags = ROUTE2_DEST_FLAGS_PROXY;

        // override destination short address
        rrep->query.short_addr = 65534;
    }
    else{
            
        cfg_i8_get( CFG_PARAM_IP_ADDRESS, &rrep->query.ip );
        cfg_i8_get( CFG_PARAM_802_15_4_SHORT_ADDRESS, &rrep->query.short_addr );

        if( cfg_b_is_gateway() ){
            
            rrep->query.flags |= ROUTE2_DEST_FLAGS_IS_GATEWAY;
        }
    }

    // copy hops to reply
    memcpy( rrep->hops, rreq->hops, sizeof(rrep->hops[0]) * rrep->hop_count );
}


void route2_v_init_rerr( rerr2_t *rerr,
                         ip_hdr_t *ip_hdr,
                         uint16_t *hops,
                         uint8_t hop_count,
                         uint16_t unreachable_hop,
                         uint8_t error ){

    memset( rerr, 0, sizeof(rerr2_t) );

    rerr->type       = ROUTE2_MESSAGE_TYPE_RERR;
    rerr->version    = ROUTE2_PROTOCOL_VERSION;
    rerr->flags      = 0;
    rerr->error      = error;
    rerr->dest_ip    = ip_hdr->dest_addr;
    rerr->origin_ip  = ip_hdr->source_addr;
    rerr->hop_count  = hop_count;
    rerr->hop_index  = 0;
    
    // set us as error IP
    cfg_i8_get( CFG_PARAM_IP_ADDRESS, &rerr->error_ip );

    // set unreachable short address
    rerr->unreachable_hop = unreachable_hop;

    // get our short address
    uint16_t our_addr = cfg_u16_get_short_addr();

    // copy hops
    memcpy( rerr->hops, hops, hop_count * sizeof(rerr->hops[0]) );
    
    // find us on the hop list
    for( uint8_t i = 0; i < rerr->hop_count; i++ ){

        // check if hop is us
        if( rerr->hops[i] == our_addr ){

            // set index to next hop (working backwards)
            rerr->hop_index = i - 1;

            break;
        }
    }
}

int8_t route2_i8_request( route_query_t *query ){
    
    // build route request
    rreq2_t rreq;
    
    route2_v_init_rreq( query, &rreq );

    // set up broadcast address
    sock_addr_t raddr;
    raddr.ipaddr = ip_a_addr(255,255,255,255);
    raddr.port = ROUTE2_SERVER_PORT;

    // send message
    if( sock_i16_sendto( sock, &rreq, sizeof(rreq), &raddr ) < 0 ){
        
        return -1;
    }
    /*
    if( !ip_b_is_zeroes( query->ip ) ){

        log_v_debug_P( PSTR("RouteDiscover: %d.%d.%d.%d"), 
                       query->ip.ip3, 
                       query->ip.ip2, 
                       query->ip.ip1, 
                       query->ip.ip0 );
    }
    else{
        
        log_v_debug_P( PSTR("RouteDiscover: %d"),
                       query->short_addr );
    }
    */

    return 0;
}

// initiate a route discovery
// this initiates a new route discovery and does not check if the query can be 
// satisfied by the local cache.
int8_t route2_i8_discover( route_query_t *query ){
    
    // check if query is already on the queue
    if( route2_b_discovery_in_progress( query ) ){
        
        return 0;
    }

    // check free space
    uint16_t max_discoveries;
    cfg_i8_get( CFG_PARAM_MAX_ROUTE_DISCOVERIES, &max_discoveries );

    // check that free space is reasonable
    if( max_discoveries < 2 ){
        
        max_discoveries = 2;
        cfg_v_set( CFG_PARAM_MAX_ROUTE_DISCOVERIES, &max_discoveries );
    }

    if( route_u8_discovery_count() >= max_discoveries ){
        
        log_v_warn_P( PSTR("DiscoveryQ full") );

        return -1;
    }
    
    // create discovery request
    discovery_t disc;
    
    disc.query = *query;
    disc.tries = ROUTE2_DISCOVERY_TRIES;

    list_node_t ln = list_ln_create_node( &disc, sizeof(discovery_t) );

    // check creation
    if( ln < 0 ){
        
        return -1;
    }

    // insert
    list_v_insert_tail( &disc_list, ln );

    log_v_debug_P( PSTR("Route discovery for:%d @ %d.%d.%d.%d"), 
                               disc.query.short_addr,
                               disc.query.ip.ip3,
                               disc.query.ip.ip2,
                               disc.query.ip.ip1,
                               disc.query.ip.ip0 );

    return 0;
}

bool route2_b_discovery_in_progress( route_query_t *query ){
    
    // check if query is already on the queue
    list_node_t ln = disc_list.head;

    while( ln >= 0 ){
        
        discovery_t *data = list_vp_get_data( ln );
        
        if( route2_b_evaluate_query( &data->query, query ) ){
            
            return TRUE;
        }
        
        ln = list_ln_next( ln );
    }

    return FALSE;
}

// cancel a route discovery in progress
void route2_v_cancel_discovery( route_query_t *query ){
    
    // search queue
    list_node_t ln = disc_list.head;

    while( ln >= 0 ){
        
        discovery_t *data = list_vp_get_data( ln );
        
        if( route2_b_evaluate_query( &data->query, query ) ){
         
            // remove from list
            list_v_remove( &disc_list, ln );

            // release node
            list_v_release_node( ln );

            return;
        }
        
        ln = list_ln_next( ln );
    }
}

int8_t route2_i8_error( ip_hdr_t *ip_hdr,
                        uint16_t *hops,
                        uint8_t hop_count,
                        uint16_t unreachable_hop,
                        uint8_t error ){
    

    // build error message
    rerr2_t rerr;
    
    route2_v_init_rerr( &rerr,
                        ip_hdr,
                        hops,
                        hop_count,
                        unreachable_hop,
                        error );

    // bounds check hop count
    if( rerr.hop_count > ROUTE2_MAXIMUM_HOPS ){
    
        return -1;
    }
    
    // get next hop short
    uint16_t next_hop = rerr.hops[rerr.hop_index];
    
    log_v_debug_P( PSTR("RouteError: Dest:%d.%d.%d.%d UnreachableHop:%d"),
                   ip_hdr->dest_addr.ip3, 
                   ip_hdr->dest_addr.ip2, 
                   ip_hdr->dest_addr.ip1, 
                   ip_hdr->dest_addr.ip0,
                   unreachable_hop );

    // set raddr for next hop
    sock_addr_t raddr;
    
    // get IP from neighbor 
    raddr.ipaddr = wcom_neighbors_a_get_ip( next_hop );
    raddr.port = ROUTE2_SERVER_PORT;
    
    // check if we still have the next hop
    if( ip_b_is_zeroes( raddr.ipaddr ) ){
        
        return -1;
    }

    // send message
    if( sock_i16_sendto( sock, &rerr, sizeof(rerr), &raddr ) ){
        
        return -1;
    }

    return 0;
}


#define PROCESS_RREQ_NO_OP      0
#define PROCESS_RREQ_DEST       1
#define PROCESS_RREQ_PROXY      2
#define PROCESS_RREQ_FORWARD    3

int8_t route2_i8_process_rreq( rreq2_t *rreq ){
    
    // set up check query
    route_query_t self_query = route2_q_query_self();

    // check if we're a target of the route query,
    // or we have a proxy route available
    int8_t proxy = route2_i8_proxy_routes( &rreq->query );

    // get our address
    uint16_t our_addr = cfg_u16_get_short_addr();

    // check if we're the target
    bool destination = ( route2_b_evaluate_query( &rreq->query, &self_query ) == TRUE ) || 
                       ( proxy == 0 );
        
    // if we're not the target and we aren't a router, we're done
    if( !destination &&
        !cfg_b_get_boolean( CFG_PARAM_ENABLE_ROUTING ) ){
        
        return PROCESS_RREQ_NO_OP;
    }
    
    // check replay cache
    if( add_to_replay_cache( rreq->hops[0], rreq->tag ) ){
        
        return PROCESS_RREQ_NO_OP;
    }

    // check hop count and make sure there is enough space to add us
    // to the list
    if( rreq->hop_count >= ROUTE2_MAXIMUM_HOPS ){
        
        return PROCESS_RREQ_NO_OP;
    }
    
    // scan through hop list and see if we've already forwarded this message
    for( uint8_t i = 0; i < rreq->hop_count; i++ ){
        
        if( rreq->hops[i] == our_addr ){
            
            return PROCESS_RREQ_NO_OP;
        }
    }

    // get last hop
    uint16_t last_hop = rreq->hops[rreq->hop_count - 1];

    // get reverse cost to last hop and add to cumulative reverse cost
    rreq->reverse_cost += wcom_neighbors_u8_get_cost( last_hop );

    // increment hop count
    rreq->hop_count++;

    // add us to hop
    rreq->hops[rreq->hop_count - 1] = our_addr;
    
    // check if proxy
    if( proxy == 0 ){
        
        return PROCESS_RREQ_PROXY;
    }

    // check if we're the destination
    if( destination ){
        
        return PROCESS_RREQ_DEST;
    }
    
    // forward
    return PROCESS_RREQ_FORWARD;
}


// process a route request message
static void process_rreq( rreq2_t *rreq ){
	
    stats_v_increment( STAT_ROUTING_RREQS_RECEIVED );
    
    int8_t op = route2_i8_process_rreq( rreq );

    if( ( op == PROCESS_RREQ_DEST ) || ( op == PROCESS_RREQ_PROXY ) ){
        
        // build a route reply
        rrep2_t rrep;
        
        route2_v_init_rrep( rreq, &rrep, ( op == PROCESS_RREQ_PROXY ) );

        // set raddr for next hop
        sock_addr_t raddr;
        
        raddr.ipaddr = wcom_neighbors_a_get_ip( rrep.hops[rrep.hop_index] );
        raddr.port = ROUTE2_SERVER_PORT;

        log_v_debug_P( PSTR("%d:%d.%d.%d.%d"),
                      rrep.hops[rrep.hop_index],
                      raddr.ipaddr.ip3,
                      raddr.ipaddr.ip2,
                      raddr.ipaddr.ip1,
                      raddr.ipaddr.ip0 );

        // send message
        sock_i16_sendto( sock, &rrep, sizeof(rrep), &raddr );


        // we could record the route to the source here if we want

        log_v_debug_P( PSTR("RouteRequest from: %d.%d.%d.%d to: %d @ %d.%d.%d.%d Hops:%d"), 
                       raddr.ipaddr.ip3, 
                       raddr.ipaddr.ip2, 
                       raddr.ipaddr.ip1, 
                       raddr.ipaddr.ip0, 
                       rreq->query.short_addr,
                       rreq->query.ip.ip3,
                       rreq->query.ip.ip2,
                       rreq->query.ip.ip1,
                       rreq->query.ip.ip0,
                       rreq->hop_count );

    }
    else if( op == PROCESS_RREQ_FORWARD ){

        // set up broadcast address
        sock_addr_t raddr;
        raddr.ipaddr = ip_a_addr(255,255,255,255);
        raddr.port = ROUTE2_SERVER_PORT;

        // send message
        sock_i16_sendto( sock, rreq, sizeof(rreq2_t), &raddr );
    }
}

#define PROCESS_RREP_NO_OP      0
#define PROCESS_RREP_DEST       1
#define PROCESS_RREP_FORWARD    2

int8_t route2_i8_process_rrep( rrep2_t *rrep ){
    
    // get our address
    uint16_t our_addr = cfg_u16_get_short_addr();
    
    // check if we're the origin
    bool origin = ( rrep->hops[0] == our_addr );

    // check if we're not the origin and not a router
    if( ( !origin ) &&
        ( !cfg_b_get_boolean( CFG_PARAM_ENABLE_ROUTING ) ) ){
        
        return PROCESS_RREP_NO_OP;
    }

    // check that current index points to us.
    // this check is in case a reply is broadcasted.
    if( rrep->hops[rrep->hop_index] != our_addr ){

        return PROCESS_RREP_NO_OP;        
    }

    // get last hop (index points to us, so we add 1)
    uint16_t last_hop = rrep->hops[rrep->hop_index + 1];

    // get forward cost to last hop and add to the total
    rrep->forward_cost += wcom_neighbors_u8_get_cost( last_hop );

    // check if origin
    if( origin ){
        
        if( rrep->hop_count < 2 ){
            
            // 0 hops is definitely an error
            // 1 hop means we'd be the only hop in the list
            return PROCESS_RREP_NO_OP;
        }

        // check if destination is a direct neighbor
        // we don't need neighbor routes in the table
        if( wcom_neighbors_b_is_neighbor( rrep->query.short_addr ) ){
            
            return PROCESS_RREP_NO_OP;
        }
        
        // reply is ok
        return PROCESS_RREP_DEST;
    }
    
    // need to forward
    
    // check that hop index will not wrap around
    if( rrep->hop_index == 0 ){
        
        return PROCESS_RREP_NO_OP;
    }

    // decrement to next hop
    rrep->hop_index--;
    
    return PROCESS_RREP_FORWARD;
}

// process a route reply message
static void process_rrep( rrep2_t *rrep ){

    stats_v_increment( STAT_ROUTING_RREPS_RECEIVED );
    
    int8_t op = route2_i8_process_rrep( rrep );

    if( op == PROCESS_RREP_DEST ){
        
        // set up route
        route2_t route;
        
        memset( &route, 0, sizeof(route) );

        route.dest_ip      = rrep->query.ip;
        route.dest_short   = rrep->query.short_addr;
        route.dest_flags   = rrep->query.flags;
        route.cost         = rrep->forward_cost;
        route.age          = 0;
        route.hop_count    = rrep->hop_count;
        
        // copy hops to route
        memcpy( route.hops, rrep->hops, route.hop_count * sizeof(route.hops[0]) );

        // add to route list
        route2_i8_add( &route );

        log_v_debug_P( PSTR("RouteReply: %d.%d.%d.%d Cost:%d Hops:%d"), 
                       rrep->query.ip.ip3, 
                       rrep->query.ip.ip2, 
                       rrep->query.ip.ip1, 
                       rrep->query.ip.ip0,
                       route.cost,
                       route.hop_count );
        
        // compute average cost per hop
        uint8_t cost_per_hop = route.cost / route.hop_count;

        // check if this route does not suck
        if( cost_per_hop < 24 ){
            
            // cancel further route requests for this route
            route2_v_cancel_discovery( &rrep->query );
        }
        else{
            
            log_v_info_P( PSTR("Marginal route:%d hops:%d cost:%d"),
                          route.dest_short,
                          route.hop_count,
                          route.cost );
        }
    }
    else if( op == PROCESS_RREP_FORWARD ){
        
        // get next hop address
        sock_addr_t raddr;
        raddr.ipaddr = wcom_neighbors_a_get_ip( rrep->hops[rrep->hop_index] );
        raddr.port = ROUTE2_SERVER_PORT;

        // check if there is no IP address for the next hop neighbor
        if( ip_b_is_zeroes( raddr.ipaddr ) ){

            // set to broadcast
            raddr.ipaddr = ip_a_addr(255,255,255,255);
        } 

        // send message
        sock_i16_sendto( sock, rrep, sizeof(rrep2_t), &raddr );
    }
}

// process a route error message
static void process_rerr( rerr2_t *rerr ){
	
    stats_v_increment( STAT_ROUTING_RERRS_RECEIVED );

    // check if we have routing enabled
    //if( cfg_b_get_boolean( CFG_PARAM_ENABLE_ROUTING ) ){
        
        // check maximum hop count
        if( rerr->hop_count > ROUTE2_MAXIMUM_HOPS ){
            
            return;
        }

        // check if we're at the end of the list
        if( rerr->hop_index == 0 ){
            
            // no hops left
            log_v_debug_P( PSTR("RouteError from:%d.%d.%d.%d Dest:%d.%d.%d.%d NextHop:(end) Error:%d"),
                           rerr->error_ip.ip3,
                           rerr->error_ip.ip2,
                           rerr->error_ip.ip1,
                           rerr->error_ip.ip0,
                           rerr->dest_ip.ip3,
                           rerr->dest_ip.ip2,
                           rerr->dest_ip.ip1,
                           rerr->dest_ip.ip0,
                           rerr->error );
            
            // create an IP query for this route
            route_query_t query = route2_q_query_ip( rerr->dest_ip );

            // delete it, if we have it
            // since we're not doing alternate routes right now,
            // we don't need to check the actual hop list
            if( route2_i8_delete( &query ) == 0 ){
                
                log_v_info_P( PSTR("RouteError:%d Purging route to:%d.%d.%d.%d"),
                              rerr->error,
                              rerr->dest_ip.ip3,
                              rerr->dest_ip.ip2,
                              rerr->dest_ip.ip1,
                              rerr->dest_ip.ip0 );
            }
            
            return;
        }
        
        // decrement index to point to next hop
        rerr->hop_index--;

        // get next hop
        uint16_t next_hop = rerr->hops[rerr->hop_index];

        log_v_debug_P( PSTR("RouteError from:%d.%d.%d.%d Dest:%d.%d.%d.%d NextHop:%d Error:%d"),
                       rerr->error_ip.ip3,
                       rerr->error_ip.ip2,
                       rerr->error_ip.ip1,
                       rerr->error_ip.ip0,
                       rerr->dest_ip.ip3,
                       rerr->dest_ip.ip2,
                       rerr->dest_ip.ip1,
                       rerr->dest_ip.ip0,
                       next_hop,
                       rerr->error );
    
        // get next hop address
        sock_addr_t raddr;
        raddr.ipaddr = wcom_neighbors_a_get_ip( next_hop );
        raddr.port = ROUTE2_SERVER_PORT;

        // check if we have next hop
        if( ip_b_is_zeroes( raddr.ipaddr ) ){
            
            log_v_debug_P( PSTR("No next hop:%d"), next_hop );
            
            return;
        }

        // send message
        sock_i16_sendto( sock, rerr, sizeof(rerr2_t), &raddr );
    //}
}


PT_THREAD( route_server_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
	
	while(1){
	
		// wait for a message
        THREAD_WAIT_WHILE( pt, sock_i8_recvfrom( sock ) < 0 );
        
		// get message
		uint8_t *type = sock_vp_get_data( sock );
	    
        // get version field
        uint8_t *version = type + 1;
       
        // check protocol version
        if( *version != ROUTE2_PROTOCOL_VERSION ){
            
            continue;
        }
        
		// process message
		// RREQ
		if( *type == ROUTE2_MESSAGE_TYPE_RREQ ){
			
			rreq2_t rreq;
            
            // make a copy, this way we can't overrun the buffer if
            // we received less data than the message is supposed to be
            memcpy( &rreq, type, sizeof(rreq) );
			
			process_rreq( &rreq );
		}	
		// RREP
		else if( *type == ROUTE2_MESSAGE_TYPE_RREP ){
			
			rrep2_t rrep;
            memcpy( &rrep, type, sizeof(rrep) );
			
			process_rrep( &rrep );
			
		}
		// RERR
		else if( *type == ROUTE2_MESSAGE_TYPE_RERR ){
			
			rerr2_t rerr;
            memcpy( &rerr, type, sizeof(rerr) );
			
			process_rerr( &rerr );
		}
		else{
			
			// invalid message type
		}
	}
	
PT_END( pt );
}



PT_THREAD( route_discovery_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
	
    static uint32_t timer;
    
	while(1){
        
        THREAD_WAIT_WHILE( pt, route_u8_discovery_count() == 0 );

        list_node_t ln = disc_list.head;

        while( ln >= 0 ){
            
            discovery_t *data = list_vp_get_data( ln );
            list_node_t next = list_ln_next( ln );
            
            // check tries
            if( data->tries > 0 ){
                
                // send a route request
                if( route2_i8_request( &data->query ) == 0 ){
                    
                    data->tries--;
                }
            }
            
            if( data->tries == 0 ){
                
                log_v_debug_P( PSTR("Could not find a route for:%d @ %d.%d.%d.%d"), 
                               data->query.short_addr,
                               data->query.ip.ip3,
                               data->query.ip.ip2,
                               data->query.ip.ip1,
                               data->query.ip.ip0 );

                // remove from list
                list_v_remove( &disc_list, ln );

                // release node
                list_v_release_node( ln );
            }

            ln = next;
        }

        // delay 128 to 640 ms (random)
        timer = ( rnd_u16_get_int() >> 7 ) + 128;
        TMR_WAIT( pt, timer );
    }
	
PT_END( pt );
}



PT_THREAD( route_optimizer_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
	
    static uint32_t timer;
    //static uint8_t i;

	while(1){
	    
        timer = 8000;
        TMR_WAIT( pt, timer );
        /*
        // check neighbor routes
        for( i = 0; i < WCOM_MAX_NEIGHBORS; i++ ){
            
            wcom_neighbor_t *neighbor = wcom_neighbors_p_get_neighbor_by_index( i );
            
            // check if neighbor exists
            if( neighbor == 0 ){
                
                continue;
            }

            // check for low link quality on a link that has some traffic
            if( ( ( neighbor->etx > 48 ) && ( neighbor->traffic_avg > 0 ) ) ||
                ( ( neighbor->rssi < 2 ) && ( neighbor->traffic_avg > 0 ) ) ){
                
                route_query_t query = route2_q_query_short( neighbor->short_addr );
                route2_t route;

                // check if we have an alternate route,
                // if so, we don't need to optimize here
                if( route2_i8_query_list( &query, &route ) < 0 ){
                    // no alt. route

                    log_v_info_P( PSTR("Weak route:%d etx:%d rssi:%d traffic:%d"), 
                                  neighbor->short_addr, 
                                  neighbor->etx, 
                                  neighbor->rssi,
                                  neighbor->traffic_avg );

                    // build route query
                    route_query_t query = route2_q_query_short( neighbor->short_addr );

                    // request discovery
                    route2_i8_discover( &query );
                    
                    timer = 1000;
                    TMR_WAIT( pt, timer );
                }
            }
        }

        // check longer routes
        for( i = 0; i < ROUTE2_MAXIMUM_ROUTES; i++ ){
            
            // check if route exists
            if( routes[i].hop_count ==  0 ){
                
                continue;
            }

            // compute average cost per hop
            uint8_t cost_per_hop = routes[i].cost / routes[i].hop_count;

            // get link quality of next hop
            uint8_t next_hop_cost = wcom_neighbors_u8_get_cost( routes[i].hops[0] );

            if( ( cost_per_hop > 48 ) ||
                ( next_hop_cost > 48 ) ){
                
                log_v_info_P( PSTR("Long route:%d cost:%d hops:%d"), routes[i].dest_short, routes[i].cost, routes[i].hop_count );

                // build route query
                route_query_t query = route2_q_query_short( routes[i].dest_short );
                route2_i8_discover( &query );
                
                timer = 1000;
                TMR_WAIT( pt, timer );
            }
        }*/
	}
	
PT_END( pt );
}


PT_THREAD( route_age_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
	
    static uint32_t timer;

	while(1){
	    
        timer = 1000;
        TMR_WAIT( pt, timer );
        
        // loop through routes
        list_node_t ln = route_list.head;

        while( ln >= 0 ){
            
            route2_t *route = list_vp_get_data( ln );
            list_node_t next = list_ln_next( ln );
                
            if( route->age < ROUTE2_MAXIMUM_AGE ){

                // increment age
                route->age++;
            }
            // check for expiration
            else if( route->age > ROUTE2_MAXIMUM_AGE ){
                
                log_v_info_P( PSTR("Purging route to:%d"), route->dest_short );

                // remove from list
                list_v_remove( &route_list, ln );

                // release node
                list_v_release_node( ln );
            }
            
            // check for errors
            if( route2_i8_check( route ) < 0 ){

                // remove from list
                list_v_remove( &route_list, ln );

                // release node
                list_v_release_node( ln );
            }

            ln = next;
        }
	}
	
PT_END( pt );
}


