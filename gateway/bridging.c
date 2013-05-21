
#include "cpu.h"

#include "system.h"
 
#include "threading.h"
#include "timers.h"
#include "dhcp.h"
#include "fs.h"

#include "appcfg.h"

#include "bridging.h"

//#define NO_LOGGING
#include "logging.h"

PT_THREAD( local_dhcp_lease_thread( pt_t *pt, void *state ) );
PT_THREAD( ip_pool_thread( pt_t *pt, void *state ) );

static bridge_t bridges[BRIDGE_TABLE_SIZE];

static int32_t our_lease; // in seconds
static int32_t our_lease_remaining; // in seconds


static uint16_t vfile( vfile_op_t8 op, uint32_t pos, void *ptr, uint16_t len ){
    
    void *buf = bridges;

    // the pos and len values are already bounds checked by the FS driver
    switch( op ){
        
        case FS_VFILE_OP_READ:
    
            memcpy( ptr, buf + pos, len );            
            break;

        case FS_VFILE_OP_SIZE:
            len = sizeof(bridges); 
            break;

        default:
            len = 0;
            break;
    }

    return len;
}

void bridge_v_init( void ){
 
    // init local dhcp lease thread
    thread_t_create( local_dhcp_lease_thread,
                     PSTR("local_dhcp_lease_manager"),
                     0,
                     0 );

    // init ip pool thread
    thread_t_create( ip_pool_thread,
                     PSTR("ip_pool"),
                     0,
                     0 );
  
    // add bridging table to virtual file system
    fs_f_create_virtual( PSTR("bridge"), vfile );
}

bridge_t *bridge_b_get_bridge( ip_addr_t ip ){
    
    for( uint16_t i = 0; i < BRIDGE_TABLE_SIZE; i++ ){
        
        if( ip_b_addr_compare( bridges[i].ip, ip ) ){
            
            return &bridges[i];
        }
    }
    
    return 0;
}

bridge_t *bridge_b_get_bridge2( uint16_t short_addr ){
    
    for( uint16_t i = 0; i < BRIDGE_TABLE_SIZE; i++ ){
        
        if( bridges[i].short_addr == short_addr ){
            
            return &bridges[i];
        }
    }
    
    return 0;
}


// return an unallocated bridge entry that has an IP address
bridge_t *bridge_b_get_new( void ){
    
    for( uint16_t i = 0; i < BRIDGE_TABLE_SIZE; i++ ){
        
        if( bridges[i].short_addr == 0 ){
            
            return &bridges[i];
        }
    }

    return 0;
}

void bridge_v_add_to_bridge( bridge_t *bridge ){
    
    for( uint16_t i = 0; i < BRIDGE_TABLE_SIZE; i++ ){
        
        if( bridges[i].short_addr == 0 ){
            
            bridges[i] = *bridge;

            return;
        }
    }
}


PT_THREAD( local_dhcp_lease_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
    
    static dhcp_thread_t dhcp_thread = -1;
    static uint32_t timer;
    
    THREAD_YIELD( pt );
    
    // check if our IP address is manually configured
    if( cfg_b_ip_configured() ){
        
        THREAD_EXIT( pt );
    }

    while(1){

        // get our hostname
        char hostname[CFG_STR_LEN];
        cfg_i8_get( CFG_PARAM_USER_NAME, hostname );
        
        // get our client ID
        // for the gateway, this is our hardware address
        uint8_t client_id[DHCP_HLEN_ETHERNET];
        cfg_i8_get( CFG_PARAM_ETH_MAC_ADDRESS, client_id );
        
        // get our IP
        ip_addr_t our_ip;
        cfg_i8_get( CFG_PARAM_IP_ADDRESS, &our_ip );

        // start a dhcp thread to request gateway's IP address
        dhcp_thread = dhcp_t_create_client( client_id, 
                                            sizeof(client_id),
                                            hostname,
                                            our_ip );
        
        // check thread
        if( dhcp_thread < 0 ){
            
            // restart and try again
            THREAD_RESTART( pt );
        }
        
        log_v_debug_P( PSTR("DHCP request") );

        // wait until DHCP is configured
        THREAD_WAIT_WHILE( pt, dhcp_u8_get_status( dhcp_thread ) == DHCP_STATUS_UNCONFIGURED );
        
        dhcp_config_t dhcp_config;

        // get configuration
        dhcp_v_get_config( dhcp_thread, &dhcp_config );
        
        // apply ip configuration
        cfg_ip_config_t ip_config;
        ip_config.ip                = dhcp_config.ip_addr;
        ip_config.subnet            = dhcp_config.subnet_mask;
        ip_config.dns_server        = dhcp_config.dns_server_ip;
        ip_config.internet_gateway  = dhcp_config.router_ip;
        
        cfg_v_set_ip_config( &ip_config );

        log_v_debug_P( PSTR("DHCP: IP:%d.%d.%d.%d Subnet:%d.%d.%d.%d DNS:%d.%d.%d.%d Internet:%d.%d.%d.%d"),
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

        
        // set our lease
        our_lease = dhcp_config.ip_lease_time;
        our_lease_remaining = our_lease;

        // kill the thread
        dhcp_thread = dhcp_t_kill( dhcp_thread );

        // wait until we need to renew
        while( our_lease_remaining > ( our_lease / 8 ) ){
            
            // 4 second delay
            timer = 4000;
            TMR_WAIT( pt, timer );

            // decrement remaining lease time
            our_lease_remaining -= 4;
        }
    }
    
PT_END( pt );
}


PT_THREAD( ip_pool_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
    
    static dhcp_thread_t dhcp_thread = -1;
    static uint8_t i;
    static uint32_t timer;
    
    THREAD_YIELD( pt );
    
    // check if we have DHCP enabled
    if( !cfg_b_get_boolean( CFG_PARAM_ENABLE_DHCP ) ){
        
        THREAD_EXIT( pt );
    }

    // wait until we get our initial IP config
    THREAD_WAIT_WHILE( pt, !cfg_b_ip_configured() );

    while(1){

        timer = 1000;
        TMR_WAIT( pt, timer );
        
        // scan ip pool
        for( i = 0; i < BRIDGE_TABLE_SIZE; i++ ){
                
            // look for entries which need to be renewed and are
            // not manually configured, and for which an IP is requested
            if( ( bridges[i].time_left <= ( bridges[i].lease / 8 ) ) &&
                ( ( bridges[i].flags & BRIDGE_FLAGS_MANUAL_IP ) == 0 ) &&
                ( ( bridges[i].flags & BRIDGE_FLAGS_REQUEST_IP ) != 0 ) ){
                
                // request an IP address for this bridge entry
                log_v_debug_P( PSTR("DHCP request for:%d"), bridges[i].short_addr );
                
                // print a string for the hostname
                char hostname[CFG_STR_LEN];
                snprintf_P( hostname, 
                            sizeof(hostname),
                            PSTR("sapphire_ip_pool_seq_%d"),
                            i );
                
                // set up a client ID
                // this will be our hardware address with an added
                // index field
                uint8_t client_id[DHCP_HLEN_ETHERNET + 1];
                cfg_i8_get( CFG_PARAM_ETH_MAC_ADDRESS, client_id );
                
                client_id[DHCP_HLEN_ETHERNET] = i;

                // start a dhcp thread to request IP address.
                // if the bridge entry already has an IP address listed,
                // the DHCP client will attempt to reserve that IP
                dhcp_thread = dhcp_t_create_client( client_id, 
                                                    sizeof(client_id),
                                                    hostname,
                                                    bridges[i].ip );
                
                // check thread
                if( dhcp_thread < 0 ){
                    
                    // restart and try again
                    THREAD_RESTART( pt );
                }
                
                // wait until DHCP is configured
                THREAD_WAIT_WHILE( pt, dhcp_u8_get_status( dhcp_thread ) == DHCP_STATUS_UNCONFIGURED );
                
                dhcp_config_t dhcp_config;

                // get configuration
                dhcp_v_get_config( dhcp_thread, &dhcp_config );
                
                // set config
                bridges[i].ip           = dhcp_config.ip_addr;
                bridges[i].lease        = dhcp_config.ip_lease_time;
                bridges[i].time_left    = bridges[i].lease;
                bridges[i].flags        |= BRIDGE_FLAGS_IP_VALID;

                // kill the thread
                dhcp_thread = dhcp_t_kill( dhcp_thread );

                log_v_debug_P( PSTR("DHCP for:%d IP:%d.%d.%d.%d"),
                               bridges[i].short_addr,
                               bridges[i].ip.ip3,
                               bridges[i].ip.ip2,
                               bridges[i].ip.ip1,
                               bridges[i].ip.ip0 );
            }
            // check if there is time remaining in the lease
            else if( bridges[i].time_left > 0 ){
                
                // decrement time
                bridges[i].time_left--;
            }
        }
    }

PT_END( pt );
}


