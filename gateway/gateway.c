
#include "system.h"
	
#include "threading.h"
#include "config.h"
#include "arp.h"
#include "enc28j60.h"
#include "arp.h"
#include "netmsg.h"
#include "routing2.h"
#include "bridging.h"
#include "wcom_ipv4.h"
#include "netmsg_handlers.h"
#include "dhcp.h"
#include "udp.h"
#include "timers.h"
#include "icmp.h"
#include "statistics.h"
#include "devicedb.h"
#include "gateway_server.h"
#include "sntp.h"
#include "time_source.h"
#include "at86rf230.h"

#include "appcfg.h"
#include "gateway.h"

//#define NO_LOGGING
#include "logging.h"


typedef struct{
    netmsg_t netmsg;
    uint32_t timer;
    uint8_t arp_tries; 
    ip_addr_t arp_ip;
    ip_hdr_t ip_hdr;
    eth_mac_addr_t mac;
} eth_send_state_t;

PT_THREAD( eth_tx_thread( pt_t *pt, eth_send_state_t *state ) );
	
// list of ports that are allowed through to the wireless side
// when broadcasting.
static PROGMEM uint16_t broadcast_whitelist[] = {
//    DHCP_CLIENT_PORT,
};

static uint16_t broadcast_ports[4];
static bool broadcast_locals[4];

/*
static bool port_allowed( ip_hdr_t *ip_hdr ){
    
    // make sure protocol is UDP
    if( ip_hdr->protocol == IP_PROTO_UDP ){
        
        // get udp header
        udp_header_t *udp_header = (udp_header_t *)( (void *)ip_hdr + sizeof(ip_hdr_t) );
        
        uint16_t port;
        
        // check whitelist
        for( uint8_t i = 0; i < cnt_of_array(broadcast_whitelist); i++ ){
            
            memcpy_P( &port, &broadcast_whitelist[i], sizeof(port) );
            
            if( HTONS(udp_header->dest_port) == port ){
                
                return TRUE;
            }
        }
    }

    return FALSE;
}
*/

void gateway_v_send_ethernet( netmsg_t netmsg ){
    
    log_v_icmp( netmsg );

    // make a copy for the ethernet
    netmsg_t eth_netmsg = netmsg_nm_create( netmsg_vp_get_data( netmsg ), 
                                            netmsg_u16_get_len( netmsg ) );
        
    // check if message was created
    if( eth_netmsg >= 0 ){
        
        eth_send_state_t state;
        
        // send the copy of the netmsg to the ethernet thread
        state.netmsg = eth_netmsg;

        // create ethernet transmit thread
        thread_t thread = thread_t_create( THREAD_CAST(eth_tx_thread), 
                                           PSTR("ethernet_tx"),
                                           &state, 
                                           sizeof(state) );
        
        // check if thread was not created
        if( thread < 0 ){
            
            // if the thread wasn't created, release the msg
            netmsg_v_release( eth_netmsg );
        }
    }
}


PT_THREAD( eth_tx_thread( pt_t *pt, eth_send_state_t *state ) )
{
PT_BEGIN( pt );
    
    log_v_icmp( state->netmsg );

    // get the ip header
    ip_hdr_t *ip_hdr = ( ip_hdr_t * )netmsg_vp_get_data( state->netmsg );
    memcpy( &state->ip_hdr, ip_hdr, sizeof(state->ip_hdr) );
    
    // get our subnet (our ip address);
    ip_addr_t subnet_ip;
    cfg_i8_get( CFG_PARAM_IP_ADDRESS, &subnet_ip );
    
    // get subnet mask
    ip_addr_t subnet_mask;
    cfg_i8_get( CFG_PARAM_IP_SUBNET_MASK, &subnet_mask );
    
    // check if the IP address is on our subnet, or is a broadcast
    if( ( ip_b_mask_compare( subnet_ip, subnet_mask, state->ip_hdr.dest_addr ) == TRUE ) ||
        ( ip_b_check_broadcast( state->ip_hdr.dest_addr ) == TRUE ) ){
        
        // set ARP address to resolve as the destination IP address
        state->arp_ip = state->ip_hdr.dest_addr;
    }
    else{

        // Destination is not on our subnet.  We want to get the gateway MAC and send it to that
        // to be routed further up the network.
        
        // we don't actually request the gateway from DHCP, so we'll use the DHCP server IP for now
        // and assume they are the same.
        
        cfg_i8_get( CFG_PARAM_INTERNET_GATEWAY, &state->arp_ip );
    }
    
    // check ARP
    if( arp_b_get_address( &state->arp_ip, &state->mac ) == FALSE ){
        
        log_v_icmp( state->netmsg );
        
        state->arp_tries = MAX_ARP_TRIES;
        
        while( state->arp_tries > 0 ){
            
            // wait busy
            THREAD_WAIT_WHILE( pt, eth_b_tx_busy() );
            
            // send ARP request
            arp_v_send_request( &state->arp_ip );
            
            // start timer
            state->timer = tmr_u32_get_system_time() + ARP_TIMEOUT;
            
            // wait for reply or timeout
            THREAD_WAIT_WHILE( pt, ( arp_b_get_address( &state->arp_ip, &state->mac ) == FALSE ) &&
                                   ( tmr_i8_compare_time( state->timer ) > 0 ) );
            
            // check if the reply came back
            if( arp_b_get_address( &state->arp_ip, &state->mac ) == TRUE ){
                    
                break;
            }
            
            state->arp_tries--;
        }
    }
    
    // wait busy
    THREAD_WAIT_WHILE( pt, eth_b_tx_busy() );
    
    // send packet
    eth_v_send_packet( &state->mac, 
                       ETH_TYPE_IPv4, 
                       netmsg_vp_get_data( state->netmsg ), 
                       netmsg_u16_get_len( state->netmsg ) );
    
    // release the netmsg
    netmsg_v_release( state->netmsg );
    
PT_END( pt );
}


int8_t send_message( netmsg_t netmsg ){
   
    log_v_icmp( netmsg );

    // get the ip header
    ip_hdr_t *ip_hdr = (ip_hdr_t *)netmsg_vp_get_data( netmsg );

    // check bridging table
    bridge_t *bridge = bridge_b_get_bridge( ip_hdr->dest_addr );
    
    // check broadcast
    if( ip_b_check_broadcast( ip_hdr->dest_addr ) ){
        
        // check flags
        if( ( netmsg_u8_get_flags( netmsg ) & NETMSG_FLAGS_NO_WCOM ) == 0 ){

            if( wcom_ipv4_i8_send_packet( netmsg ) < 0 ){
                
                return -1;
            }
        }
        
        // this will create a copy of the netmsg
        gateway_v_send_ethernet( netmsg );
    }
    // send on wireless
    else if( bridge != 0 ){
        
        // note, if we can't get on the wireless queue, we return -1 so netmsg will requeue.
        // we don't do this with the ethernet part of the handler, since it would make it too complicated
        // to determine which interface the message hasn't been sent on yet, and the ethernet controller
        // is much faster and has a much larger transmit buffer than the wireless interface.
        
        // check flags
        if( ( netmsg_u8_get_flags( netmsg ) & NETMSG_FLAGS_NO_WCOM ) == 0 ){

            if( wcom_ipv4_i8_send_packet( netmsg ) < 0 ){
                
                return -1;
            }
        }
    }
    // if ethernet
    else if( bridge == 0 ){

        // this will create a copy of the netmsg
        gateway_v_send_ethernet( netmsg );
    }
    
    return 0;
}

// decrements ttl on given packet header.
// if it hits 0, sends an ICMP ttl exceeded message and returns -1.
// if non 0, recomputes checksum and returns 0
int8_t process_ttl( ip_hdr_t *ip_hdr ){
    
    // decrement ttl
    ip_hdr->ttl--;
    
    // check if ttl expired
    if( ip_hdr->ttl == 0 ){
        
        // check protocol
        if( ip_hdr->protocol == IP_PROTO_ICMP ){

            // adjust the ttl back to 1
            // this is done so the checksum on the original
            // ip packet is still valid (since the ICMP message will
            // include a copy of the original IP header)
            ip_hdr->ttl = 1;
            
            // send icmp ttl exceeded message
            icmp_v_send_ttl_exceeded( ip_hdr );
            
            stats_v_increment( STAT_NETMSG_IPV4_TTL_EXPIRED );
        }

        return -1;
    }
    
    // compute checksum
    ip_hdr->header_checksum = HTONS( ip_u16_ip_hdr_checksum( ip_hdr ) );

    return 0;
}



int8_t gateway_routes( route_query_t *query ){
    
    // evaluate route query
    
    // check for IP only
    if( !ip_b_is_zeroes( query->ip ) &&
        ( bridge_b_get_bridge( query->ip ) == 0 ) ){
        
        // proxy through us
        return 0;
    }

    // no routes available
    return -1;
}


void received_wcom_message( netmsg_t netmsg ){
    
    log_v_icmp( netmsg );

    // send to local receive handler
    netmsg_v_local_receive( netmsg );

    // get the ip header
    ip_hdr_t *ip_hdr = (ip_hdr_t *)netmsg_vp_get_data( netmsg );
    
    // check if we are (not) the destination
    if( !ip_b_check_dest( ip_hdr->dest_addr ) ){
        
        // check if destination is on ethernet
        bridge_t *bridge = bridge_b_get_bridge( ip_hdr->dest_addr );
        
        // if ethernet  
        if( bridge == 0 ){
            
            if( process_ttl( ip_hdr ) >= 0 ){ 
            
                // we want to route this packet
                gateway_v_send_ethernet( netmsg );
                /*
                log_v_info_P( PSTR("Bridging from:%d.%d.%d.%d to:%d.%d.%d.%d"), 
                              ip_hdr->source_addr.ip3, 
                              ip_hdr->source_addr.ip2, 
                              ip_hdr->source_addr.ip1, 
                              ip_hdr->source_addr.ip0,
                              ip_hdr->dest_addr.ip3, 
                              ip_hdr->dest_addr.ip2, 
                              ip_hdr->dest_addr.ip1, 
                              ip_hdr->dest_addr.ip0 );
                */

            }
        }
    }

    netmsg_v_release( netmsg );
}

void gateway_v_receive_ethernet( uint16_t type, netmsg_t netmsg ){
    
    log_v_icmp( netmsg );

    // check protocol
    if( type == ETH_TYPE_IPv4 ){
        
        // send to local receive handler
        netmsg_v_local_receive( netmsg );

        // get the ip header
        ip_hdr_t *ip_hdr = (ip_hdr_t *)netmsg_vp_get_data( netmsg );
        
        // check broadcast
        if( ip_b_check_broadcast( ip_hdr->dest_addr ) ){
            
            // make sure protocol is UDP
            if( ip_hdr->protocol == IP_PROTO_UDP ){
            
                // get udp header
                udp_header_t *udp_header = (udp_header_t *)( (void *)ip_hdr + sizeof(ip_hdr_t) );

                for( uint8_t i = 0; i < cnt_of_array(broadcast_ports); i++ ){
                    
                    if( HTONS(udp_header->dest_port) == broadcast_ports[i] ){
                        
                        // check if local broadcast only
                        if( broadcast_locals[i] ){

                            // force TTL to 1, we will only do a local broadcast from the ethernet side.
                            // this is a bit of a hack to implement a "poor man's multicast"
                            // note we actually set it to 2, so the process TTL routine will decrement it.
                            ip_hdr->ttl = 2;
                        }
                        
                        // process time to live
                        if( process_ttl( ip_hdr ) >= 0 ){
                            
                            // add to wireless transmit queue
                            wcom_ipv4_i8_send_packet( netmsg );
                        }

                        break;
                    }
                }
            }
        }
        // unicast, to someone other than us
        else if( !ip_b_check_dest( ip_hdr->dest_addr ) ){
            
            // check bridge
            bridge_t *bridge = bridge_b_get_bridge( ip_hdr->dest_addr );
            
            // check if destination is in the bridge table
            if( bridge != 0 ){
                
                if( process_ttl( ip_hdr ) >= 0 ){

                    // add to wireless transmit queue
                    wcom_ipv4_i8_send_packet( netmsg );
                }
            }
            else{

                // destination not in bridge
                //
                // send host unreachable
                icmp_v_send_dest_unreachable( ip_hdr );
            }
            
            // check if the source is in the bridge table (hint: it shouldn't be)
            bridge = bridge_b_get_bridge( ip_hdr->source_addr );
            
            if( bridge != 0 ){
                
                log_v_warn_P( PSTR("Ether message from:%d.%d.%d.%d in wireless bridge table"), ip_hdr->source_addr );
            }
        }
    }
    else if( type == ETH_TYPE_ARP ){
        
        // send message to ARP module
        arp_v_recv( netmsg_vp_get_data( netmsg ) );
        
        //stats_v_increment( STAT_NETMSG_ARP_RECEIVED );
    }
    else{ // unknown typ
        
    }

    netmsg_v_release( netmsg );
}


// override config handler
bool cfg_b_is_gateway( void ){
    
    return TRUE;
}

void gateway_v_init( void ){
    
    // set RF channel
    uint8_t channel;

    if( cfg_i8_get( CFG_PARAM_NETWORK_CHANNEL, &channel ) < 0 ){

        channel = RF_LOWEST_CHANNEL;
    }

    rf_v_set_channel( channel );

    // init bridging
    bridge_v_init();        
    
    // init gateway server
    gate_svr_v_init();

    // init device database
    devdb_v_init();

    // init sntp
    sntp_v_init();

    // init network time source
    timesource_v_init();

    netmsg_i8_transmit_msg = send_message;
    netmsg_v_receive_msg = received_wcom_message;
    eth_v_receive_frame = gateway_v_receive_ethernet;
    route2_i8_proxy_routes = gateway_routes;
    
    // read broadcast port filter from config
    cfg_i8_get( CFG_PARAM_BROADCAST_PORT_0, &broadcast_ports[0] );
    cfg_i8_get( CFG_PARAM_BROADCAST_PORT_1, &broadcast_ports[1] );
    cfg_i8_get( CFG_PARAM_BROADCAST_PORT_2, &broadcast_ports[2] );
    cfg_i8_get( CFG_PARAM_BROADCAST_PORT_3, &broadcast_ports[3] );
    cfg_i8_get( CFG_PARAM_BROADCAST_PORT_0_LOCAL, &broadcast_locals[0] );
    cfg_i8_get( CFG_PARAM_BROADCAST_PORT_1_LOCAL, &broadcast_locals[1] );
    cfg_i8_get( CFG_PARAM_BROADCAST_PORT_2_LOCAL, &broadcast_locals[2] );
    cfg_i8_get( CFG_PARAM_BROADCAST_PORT_3_LOCAL, &broadcast_locals[3] );
    
    
    // preload bridge from devicedb
    //
    devdb_device_info_t info;
    
    for( uint16_t i = 0; i < devdb_u16_get_device_count(); i++ ){
               
        devdb_i8_get_device_by_index( i, &info );

        bridge_t bridge;

        bridge.short_addr   = info.short_addr;
        bridge.ip           = info.ip;
        bridge.lease        = 0;
        bridge.time_left    = 0;
        bridge.flags        = BRIDGE_FLAGS_REQUEST_IP;

        bridge_v_add_to_bridge( &bridge );
    }


/*
    bridge_v_add_to_bridge( ip_a_addr(192,168,2,200) );
    bridge_v_add_to_bridge( ip_a_addr(192,168,2,201) );
    bridge_v_add_to_bridge( ip_a_addr(192,168,2,202) );
    bridge_v_add_to_bridge( ip_a_addr(192,168,2,203) );
    bridge_v_add_to_bridge( ip_a_addr(192,168,2,204) );
    bridge_v_add_to_bridge( ip_a_addr(192,168,2,205) );
    bridge_v_add_to_bridge( ip_a_addr(192,168,2,206) );
    bridge_v_add_to_bridge( ip_a_addr(192,168,2,207) );
    bridge_v_add_to_bridge( ip_a_addr(192,168,2,208) );
    bridge_v_add_to_bridge( ip_a_addr(192,168,2,209) );
    bridge_v_add_to_bridge( ip_a_addr(192,168,2,210) );
    bridge_v_add_to_bridge( ip_a_addr(192,168,2,211) );
    bridge_v_add_to_bridge( ip_a_addr(192,168,2,212) );
    bridge_v_add_to_bridge( ip_a_addr(192,168,2,213) );
    bridge_v_add_to_bridge( ip_a_addr(192,168,2,214) );
    bridge_v_add_to_bridge( ip_a_addr(192,168,2,215) );
*/
}

