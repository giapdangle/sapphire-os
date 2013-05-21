

#include "cpu.h"

#include "config.h"
#include "threading.h"
#include "sockets.h"
#include "timers.h"
#include "random.h"
#include "gateway_services.h"
#include "wcom_time.h"

#include "devicedb.h"
#include "bridging.h"
#include "dhcp.h"
#include "sntp.h"
#include "gateway_server.h"

#include "notifications.h"

//#define NO_LOGGING
#include "logging.h"

static socket_t sock0;
static socket_t sock1;

static uint32_t token;

typedef struct{
    uint16_t index;
    uint8_t page;
    uint32_t timer;
    sock_addr_t raddr;
} device_list_sender_state_t;


PT_THREAD( gateway_server0_thread( pt_t *pt, void *state ) );
PT_THREAD( gateway_server1_thread( pt_t *pt, void *state ) );
PT_THREAD( gateway_token_thread( pt_t *pt, void *state ) );


void gate_svr_v_init( void ){
    
    // create sockets
    sock0 = sock_s_create( SOCK_DGRAM );
    sock1 = sock_s_create( SOCK_UDPX_SERVER );

    ASSERT( sock0 >= 0 );
    ASSERT( sock1 >= 0 );

    // bind to gateway services port
    sock_v_bind( sock0, GATEWAY_SERVICES_PORT );
    sock_v_bind( sock1, GATEWAY_SERVICES_UDPX_PORT );
   
    // create server threads
    thread_t_create( gateway_server0_thread,
                     PSTR("gateway_server_0"),
                     0,
                     0 );

    thread_t_create( gateway_server1_thread,
                     PSTR("gateway_server_1"),
                     0,
                     0 );

    // create token broadcast thread
    thread_t_create( gateway_token_thread,
                     PSTR("gateway_token"),
                     0,
                     0 );
}

void message_handler( socket_t sock ){
    
    // get message type
    uint8_t *type = (uint8_t *)sock_vp_get_data( sock );
    
    // check message type
    if( *type == GATEWAY_MSG_POLL_GATEWAY ){
        
        //gate_msg_poll_t *msg = (gate_msg_poll_t *)type;

        gate_msg_token_t response;
        
        // set up token message
        response.type       = GATEWAY_MSG_GATEWAY_TOKEN;
        response.token      = token;
        response.short_addr = cfg_u16_get_short_addr();
        cfg_i8_get( CFG_PARAM_DEVICE_ID, &response.device_id );
        
        // send back to device
        sock_i16_sendto( sock, &response, sizeof(response), 0 );
    }
    else if( *type == GATEWAY_MSG_REQUEST_IP_CONFIG ){
        
        gate_msg_request_ip_config_t *msg = (gate_msg_request_ip_config_t *)type;
        
        log_v_debug_P( PSTR("Config Request from:%d"), msg->short_addr );

        // look up bridge entry
        bridge_t *bridge = bridge_b_get_bridge2( msg->short_addr );
        
        // check we had the device in the bridge
        if( bridge == 0 ){
            
            // get a new entry
            bridge = bridge_b_get_new();
            
            // check if the bridge is full
            if( bridge == 0 ){
                
                log_v_warn_P( PSTR("Bridge full") );

                return;
            }
        }
        
        // update bridge entry
        bridge->short_addr = msg->short_addr;
        
        // set up remote address
        sock_addr_t raddr;
        sock_v_get_raddr( sock, &raddr );

        // check if the requesting device has a manual IP address
        if( ( msg->flags & GATEWAY_MSG_REQUEST_IP_FLAGS_MANUAL_IP ) != 0 ){
            
            log_v_debug_P( PSTR("Manual config: %d.%d.%d.%d"), 
                           msg->ip.ip3, 
                           msg->ip.ip2, 
                           msg->ip.ip1, 
                           msg->ip.ip0 );

            // manually set bridge IP
            bridge->ip = msg->ip;

            // set flags to manual config
            bridge->flags = BRIDGE_FLAGS_MANUAL_IP;
            bridge->lease = 0;

            // unicast to device
            raddr.ipaddr = msg->ip;
        }
        // check if bridge IP is NOT valid
        else if( ( bridge->flags & BRIDGE_FLAGS_IP_VALID ) == 0 ){
            
            // make sure manual flag is clear
            bridge->flags &= ~BRIDGE_FLAGS_MANUAL_IP;

            // set request flag
            bridge->flags |= BRIDGE_FLAGS_REQUEST_IP;

            // reset lease
            bridge->lease = 0;
            bridge->time_left = 0;
            
            // we're not going to reply until the bridge resolves
            // an IP address
            return;
        }
        // bridge IP valid
        else{
            
            // we need to broadcast back to device because it
            // doesn't know it's IP yet

            // set up broadcast address
            raddr.ipaddr = ip_a_addr(255,255,255,255);

            log_v_debug_P( PSTR("Assigning: %d.%d.%d.%d to:%d"), 
                           bridge->ip.ip3, 
                           bridge->ip.ip2, 
                           bridge->ip.ip1, 
                           bridge->ip.ip0,
                           msg->short_addr );
        }

        // update device database
        devdb_device_info_t info;
        
        info.short_addr = bridge->short_addr;
        info.device_id  = msg->device_id;
        info.ip         = bridge->ip;

        devdb_v_add_device( &info );

        // trigger notification
        notif_v_device_attach( info.short_addr );

        // send response
        gate_msg_ip_config_t response;
        
        response.type           = GATEWAY_MSG_IP_CONFIG;
        response.short_addr     = msg->short_addr;
        response.ip             = bridge->ip;
        response.token          = token;
        
        cfg_i8_get( CFG_PARAM_IP_SUBNET_MASK, &response.subnet );
        cfg_i8_get( CFG_PARAM_DNS_SERVER, &response.dns_server );

        // send our IP as internet gateway
        cfg_i8_get( CFG_PARAM_INTERNET_GATEWAY, &response.internet_gateway );
        
        // send response
        sock_i16_sendto( sock, &response, sizeof(response), &raddr );
    }   
    else if( *type == GATEWAY_MSG_REQUEST_TIME ){
        
        gate_msg_current_time_t response;
        
        // get current NTP time
        ntp_ts_t now = sntp_t_now();

        // set up time response message, sending
        // NTP seconds
        response.type = GATEWAY_MSG_CURRENT_TIME;
        response.time = now.seconds;

        // send response
        sock_i16_sendto( sock, &response, sizeof(response), 0 );
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
    else if( *type == GATEWAY_MSG_GET_NETWORK_TIME ){
        
        // get timestamps, disable interrupts to prevent
        // timer tick between both timestamps
        ATOMIC;

        ntp_ts_t ntp_now = sntp_t_now();
        uint32_t wcom_now = wcom_time_u32_get_network_time();
        
        END_ATOMIC;
        
        // build response message
        gate_msg_network_time_t response;

        response.type = GATEWAY_MSG_NETWORK_TIME;
        response.flags = 0;
        memcpy( &response.ntp_time, &ntp_now, sizeof(response.ntp_time) );
        response.wcom_network_time = wcom_now;
        
        // set flags
        if( wcom_time_b_sync() ){
            
            response.flags |= GATEWAY_NET_TIME_FLAGS_WCOM_NETWORK_SYNC;
        }

        if( sntp_u8_get_status() == SNTP_STATUS_SYNCHRONIZED ){
            
            response.flags |= GATEWAY_NET_TIME_FLAGS_NTP_SYNC;
        }
        
        // if both clocks are synchronized, we can set the valid flag
        if( ( response.flags & GATEWAY_NET_TIME_FLAGS_WCOM_NETWORK_SYNC ) &&
            ( response.flags & GATEWAY_NET_TIME_FLAGS_NTP_SYNC ) ){
            
            response.flags |= GATEWAY_NET_TIME_FLAGS_VALID;
        }

        // send response
        sock_i16_sendto( sock, &response, sizeof(response), 0 );
    }
}


PT_THREAD( gateway_server0_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
    
    // init token
    while( token == 0 ){
        
        token = ( (uint32_t)rnd_u16_get_int() << 16 ) | rnd_u16_get_int();
    }
    
    log_v_debug_P( PSTR("Token:%ld"), token );


    while(1){
        
        // wait for message
        THREAD_WAIT_WHILE( pt, sock_i8_recvfrom( sock0 ) < 0 );
        
        message_handler( sock0 );
    }
    
PT_END( pt );
}

PT_THREAD( gateway_server1_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
    
    while(1){
        
        // wait for message
        THREAD_WAIT_WHILE( pt, sock_i8_recvfrom( sock1 ) < 0 );
        
        message_handler( sock1 );
    }
    
PT_END( pt );
}


PT_THREAD( gateway_token_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
    
    static uint32_t timer;
    static uint32_t interval;
    
    // init timer interval
    interval = 1000;

    while( 1 ){
        
        // set timer
        timer = interval;
        TMR_WAIT( pt, timer );
        
        // increase interval
        if( interval < 60000 ){
            
            interval += 1000;
        }
        
        // set up broadcast to gateway services port
        sock_addr_t raddr;
        raddr.port      = GATEWAY_SERVICES_PORT;
        raddr.ipaddr    = ip_a_addr(255,255,255,255);

        gate_msg_token_t msg;
        
        // set up token message
        msg.type    = GATEWAY_MSG_GATEWAY_TOKEN;
        msg.token   = token;
        
        // send message
        sock_i16_sendto( sock0, &msg, sizeof(msg), &raddr );
    }
    
PT_END( pt );
}



