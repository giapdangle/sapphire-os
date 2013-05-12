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
#include "timers.h"
#include "sockets.h"
#include "routing2.h"
#include "system.h"
#include "datetime.h"
#include "random.h"
#include "wcom_time.h"

#include "gateway_services.h"

//#define NO_LOGGING
#include "logging.h"

static socket_t sock;

static bool configured;
static uint32_t token;


PT_THREAD( gate_svc_client_thread( pt_t *pt, void *state ) );
PT_THREAD( gate_svc_receiver_thread( pt_t *pt, void *state ) );


void gate_svc_v_init( void ){
    
    configured = FALSE;

    thread_t_create( gate_svc_client_thread,
                     PSTR("gateway_services_client"),
                     0,
                     0 );
}

int8_t send_msg_to_gateway( void *msg, uint8_t len ){
    
    // get gateway route so we can get the IP address
    route_query_t query = route2_q_query_flags( ROUTE2_DEST_FLAGS_IS_GATEWAY );

    route2_t route;
    
    if( route2_i8_get( &query, &route ) < 0 ){
        
        // no route
        return -1;
    }
    
    // set up remote address
    sock_addr_t raddr;
    raddr.ipaddr = route.dest_ip;
    raddr.port = GATEWAY_SERVICES_PORT;

    // send message
    if( sock_i16_sendto( sock, msg, len, &raddr ) < 0 ){
        
        // message queuing failed

        return -2;
    }
    
    return 0;
}

int8_t poll_gateway( void ){
    
    gate_msg_poll_t msg;
    
    msg.type        = GATEWAY_MSG_POLL_GATEWAY;
    msg.short_addr  = cfg_u16_get_short_addr();

    return send_msg_to_gateway( &msg, sizeof(msg) );
}

int8_t send_ip_config_request( void ){
    
    log_v_debug_P( PSTR("IP config request") );

    // build config request message
    gate_msg_request_ip_config_t msg;

    msg.type        = GATEWAY_MSG_REQUEST_IP_CONFIG;
    msg.flags       = 0;
    msg.short_addr  = cfg_u16_get_short_addr();
    msg.ip          = ip_a_addr(0,0,0,0);
    cfg_i8_get( CFG_PARAM_DEVICE_ID, &msg.device_id );
    
    // check if manual IP address
    if( cfg_b_manual_ip() ){
        
        msg.flags |= GATEWAY_MSG_REQUEST_IP_FLAGS_MANUAL_IP;

        cfg_i8_get( CFG_PARAM_IP_ADDRESS, &msg.ip );
    }
    
    return send_msg_to_gateway( &msg, sizeof(msg) );
}

PT_THREAD( gate_svc_client_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
    
    static uint32_t timer;
    
    // check if we're a gateway
    // we check this in the thread, not the init because the gateway won't
    // set the gateway flag until after sapphire inits.
    if( cfg_b_is_gateway() ){
        
        THREAD_EXIT( pt );
    }
    
    // start server thread
    thread_t t = thread_t_create( gate_svc_receiver_thread,
                                  PSTR("gateway_server"),
                                  0,
                                  0 );
        
    ASSERT( t >= 0 );

    // create socket
    sock = sock_s_create( SOCK_DGRAM );
    
    // assert if we can't get a socket, since this module is kind of important
    ASSERT( sock >= 0 );
    
    // bind socket
    sock_v_bind( sock, GATEWAY_SERVICES_PORT );

    // main loop
    while( 1 ){

        // gateway route query loop
        while( 1 ){
            
            timer = 2000;
            TMR_WAIT( pt, timer );

            // get route query to gateway
            route_query_t query = route2_q_query_flags( ROUTE2_DEST_FLAGS_IS_GATEWAY );
            
            route2_t route;

            // check if we have a route
            if( route2_i8_get( &query, &route ) == 0 ){
                
                // we have a route, break loop
                break;
            }

            // initiate a discovery
            route2_i8_discover( &query );
        }
        
        // we have a route to the gateway
        
        // IP config loop
        while( !configured ){
            
            // send config request
            if( send_ip_config_request() < 0 ){
                
                break;
            }

            // want to add some randomization here at some point
            timer = 4000;
            TMR_WAIT( pt, timer );
        }
        
        // wait while gateway config is valid
        THREAD_WAIT_WHILE( pt, configured );

        /*
        // gateway polling loop
        while( configured ){
            
            // want to add some randomization here at some point
            timer = 60000;
            TMR_WAIT( pt, timer );
          */  
            /*
            
            The idea of polling the gateway is to see if it's token changed (reboot), so
            we can re-register our IP config.  This is required to make the gateway bridge work.
            The gateway should also broadcast a new token on start up.

            Polling is an additional fail safe in case we didn't get the broadcast.
            
            */
/*
            poll_gateway();
        }
        */
    }

PT_END( pt );
}


PT_THREAD( gate_svc_receiver_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
    
    while( 1 ){
       
        // wait for received message
        THREAD_WAIT_WHILE( pt, sock_i8_recvfrom( sock ) < 0 );
        
        // get message type
        uint8_t *type = (uint8_t *)sock_vp_get_data( sock );
        
        // check message type
        if( *type == GATEWAY_MSG_GATEWAY_TOKEN ){
            
            gate_msg_token_t *msg = (gate_msg_token_t *)type;
            
            // check if token is mismatched
            if( msg->token != token ){
                
                log_v_info_P( PSTR("Gateway config reset") );

                // reset config
                configured = FALSE;

                // reset network time keeping
                wcom_time_v_reset();
            }
        }
        else if( *type == GATEWAY_MSG_IP_CONFIG ){
            
            gate_msg_ip_config_t *msg = (gate_msg_ip_config_t *)type;
            
            // verify short address
            if( msg->short_addr != cfg_u16_get_short_addr() ){
                
                continue;
            }
            
            // check if not manual config
            if( !cfg_b_manual_ip() ){

                // apply IP configuration
                cfg_ip_config_t ip_config;
                ip_config.ip                 = msg->ip;
                ip_config.subnet             = msg->subnet;
                ip_config.dns_server         = msg->dns_server;
                ip_config.internet_gateway   = msg->internet_gateway;
                
                cfg_v_set_ip_config( &ip_config );
                
                // debug log
                log_v_debug_P( PSTR("IPConfig: IP:%d.%d.%d.%d Subnet:%d.%d.%d.%d DNS:%d.%d.%d.%d Internet:%d.%d.%d.%d"),
                               ip_config.ip.ip3,
                               ip_config.ip.ip2,
                               ip_config.ip.ip1,
                               ip_config.ip.ip0,
                               ip_config.subnet.ip3,
                               ip_config.subnet.ip2,
                               ip_config.subnet.ip1,
                               ip_config.subnet.ip0,
                               ip_config.dns_server.ip3,
                               ip_config.dns_server.ip2,
                               ip_config.dns_server.ip1,
                               ip_config.dns_server.ip0,
                               ip_config.internet_gateway.ip3,
                               ip_config.internet_gateway.ip2,
                               ip_config.internet_gateway.ip1,
                               ip_config.internet_gateway.ip0 );
            }

            // set gateway token
            token = msg->token;
            
            // debug
            log_v_debug_P( PSTR("GatewayToken:%ld"), token );

            // IP configured
            configured = TRUE;
        }
        else if( *type == GATEWAY_MSG_RESET_IP_CONFIG ){
            
            gate_msg_reset_ip_cfg_t *msg = (gate_msg_reset_ip_cfg_t *)type;
            
            // check short address
            if( msg->short_addr == cfg_u16_get_short_addr() ){
                
                log_v_info_P( PSTR("IP config reset") );

                ip_addr_t zeroes = ip_a_addr(0,0,0,0);

                // reset IP configuration
                cfg_v_set( CFG_PARAM_MANUAL_IP_ADDRESS, &zeroes );
                cfg_v_set( CFG_PARAM_MANUAL_SUBNET_MASK, &zeroes );
                cfg_v_set( CFG_PARAM_MANUAL_DNS_SERVER, &zeroes );
                cfg_v_set( CFG_PARAM_MANUAL_INTERNET_GATEWAY, &zeroes );

                gate_msg_reset_ip_confirm_t response;
                response.type = GATEWAY_MSG_RESET_IP_CONFIRM;

                // send response
                sock_i16_sendto( sock, &response, sizeof(response), 0 );

                // reboot
                sys_v_reboot_delay( SYS_MODE_NORMAL );
            }
        }
    }

PT_END( pt );
}

