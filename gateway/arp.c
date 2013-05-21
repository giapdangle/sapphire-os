

#include "target.h"
#include "config.h"
#include "timers.h"
#include "threading.h"
#include "system.h"

#include "appcfg.h"
#include "bridging.h"
#include "ip.h"
#include "fs.h"
#include "arp.h"
#include "enc28j60.h"

#include <string.h>

static arp_entry_t arp_cache[ARP_MAX_CACHE_ENTRIES];


static uint16_t vfile( vfile_op_t8 op, uint32_t pos, void *ptr, uint16_t len ){
    
    void *buf = arp_cache;

    // the pos and len values are already bounds checked by the FS driver
    switch( op ){
        
        case FS_VFILE_OP_READ:
    
            memcpy( ptr, buf + pos, len );            
            break;

        case FS_VFILE_OP_SIZE:
            len = sizeof(arp_cache); 
            break;

        default:
            len = 0;
            break;
    }

    return len;
}



PT_THREAD( arp_aging_thread( pt_t *pt, void *state ) );


void arp_v_init( void ){
	
    fs_f_create_virtual( PSTR("arp_cache"), vfile );
	
    thread_t_create( arp_aging_thread,
                     PSTR("arp_aging"),
                     0,
                     0 );
}

// returns TRUE if the target IP address is found in the ARP cache
bool arp_b_get_address( ip_addr_t *ip, eth_mac_addr_t *eth_mac ){
	
	// check for broadcast
	if( ip_b_check_broadcast( *ip ) == TRUE ){
		
		eth_mac_addr_t broadcast_mac;
		broadcast_mac.mac[0] = 0xff;
		broadcast_mac.mac[1] = 0xff;
		broadcast_mac.mac[2] = 0xff;
		broadcast_mac.mac[3] = 0xff;
		broadcast_mac.mac[4] = 0xff;
		broadcast_mac.mac[5] = 0xff;
		
		if( eth_mac != 0 ){
				
			*eth_mac = broadcast_mac;
		}
		
		return TRUE;
	}
	
	for( uint8_t i = 0; i < ARP_MAX_CACHE_ENTRIES; i++ ){
		
		if( ip_b_addr_compare( *ip, arp_cache[i].ip_addr ) == TRUE ){
			
			if( eth_mac != 0 ){
				
				*eth_mac = arp_cache[i].eth_addr;
			}
			
			return TRUE;
		}
	}
	
	return FALSE;
}

void arp_v_send_request( ip_addr_t *target_ip ){
	
	// create a request
	arp_t arp_request;
	
	arp_v_create_request( &arp_request, target_ip );
	
    // set broadcast mac
    eth_mac_addr_t mac;
    memset( &mac, 0xff, sizeof(mac) );
    
    // send reply
    eth_v_send_packet( &mac, ETH_TYPE_ARP, (void *)&arp_request, sizeof(arp_request) );
}
	
void arp_v_recv( arp_t *arp ){
	
	// check if this is a request
	if( arp->oper == ARP_OPER_REQUEST ){
		
		// check if request is for our IP address
        ip_addr_t our_ip;
        cfg_i8_get( CFG_PARAM_IP_ADDRESS, &our_ip );
		
		ip_addr_t target_addr;
		memcpy( &target_addr, arp->tpa, sizeof(target_addr) );
		
		// send the reply if:
		// 1) the request is for the dhcp configured ip address
		// OR 2) the request is for any ip on the wireless bridge
        if( ( ip_b_addr_compare( our_ip, target_addr ) == TRUE ) ||
            ( bridge_b_get_bridge( target_addr ) != 0 ) ){
            
			// create a reply
			arp_t arp_reply;
			
			arp_v_create_reply( &arp_reply, 
								(eth_mac_addr_t *)&arp->sha, 
								(ip_addr_t *)&arp->tpa,
								(ip_addr_t *)&arp->spa );
			
            // set broadcast mac for reply message
            eth_mac_addr_t mac;
            memset( &mac, 0xff, sizeof(mac) );
            
            // send reply
            eth_v_send_packet( &mac, ETH_TYPE_ARP, (void *)&arp_reply, sizeof(arp_reply) );
		}
	}
	// check if this is a reply
	else if( arp->oper == ARP_OPER_REPLY ){
		
		uint8_t oldest = 0;
		
		// find oldest cache entry
		for( uint8_t i = 0; i < ARP_MAX_CACHE_ENTRIES; i++ ){
			
			if( arp_cache[i].age > arp_cache[oldest].age ){
				
				oldest = i;
			}
		}
		
		// add entry
		memcpy( &arp_cache[oldest].eth_addr, &arp->sha, sizeof(arp_cache[oldest].eth_addr) );
		memcpy( &arp_cache[oldest].ip_addr, &arp->spa, sizeof(arp_cache[oldest].ip_addr) );
		arp_cache[oldest].age = 0;
	}
}

void arp_v_create_request( arp_t *arp, ip_addr_t *target_ip ){
	
	arp->htype 	= ARP_HTYPE_ETHERNET;
	arp->ptype 	= ARP_PTYPE_IPv4;
	arp->hlen 	= ARP_HLEN_ETH; 
	arp->plen 	= ARP_PLEN_ETH; 
	arp->oper   = ARP_OPER_REQUEST;
	
	// copy sender hardware address
	eth_mac_addr_t sha;
	cfg_i8_get( CFG_PARAM_ETH_MAC_ADDRESS, &sha );
	memcpy( arp->sha, &sha, sizeof( arp->sha ) );
	
	// copy sender protocol address
	//ip_addr_t spa;
	cfg_i8_get( CFG_PARAM_IP_ADDRESS, &arp->spa );
	/*
	dhcp_config_t dhcp_config;
	
	dhcp_v_get_config( &dhcp_config );
	
	memcpy( arp->spa, &dhcp_config.ip_addr, sizeof( arp->spa ) );
	*/
	// set target hardware address to 0
	memset( arp->tha, 0, sizeof( arp->tha ) );
	
	// set target protocol address
	memcpy( arp->tpa, target_ip, sizeof( arp->tpa ) );
}
	
void arp_v_create_reply( arp_t *arp, 
						  eth_mac_addr_t *target_mac, 
						  ip_addr_t *sender_ip,
						  ip_addr_t *target_ip ){
	
	arp->htype 	= ARP_HTYPE_ETHERNET;
	arp->ptype 	= ARP_PTYPE_IPv4;
	arp->hlen 	= ARP_HLEN_ETH; 
	arp->plen 	= ARP_PLEN_ETH; 
	arp->oper   = ARP_OPER_REPLY;
	
	// copy sender hardware address
	eth_mac_addr_t sha;
	cfg_i8_get( CFG_PARAM_ETH_MAC_ADDRESS, &sha );
	memcpy( arp->sha, &sha, sizeof( arp->sha ) );
	
	// copy sender protocol address
//	ip_addr_t spa;
	cfg_i8_get( CFG_PARAM_IP_ADDRESS, &arp->spa );
	/*
	dhcp_config_t dhcp_config;
	
	dhcp_v_get_config( &dhcp_config );
*/
	//memcpy( arp->spa, &dhcp_config.ip_addr, sizeof( arp->spa ) );

	// Proxy ARP setting: DO NOT USE THIS! *** actually maybe this is ok...
	// must ensure replies will only be sent for this node's IP address, or
	// for the wireless subnet!
	memcpy( arp->spa, sender_ip, sizeof( arp->spa ) );
	
	// set target hardware address
	memcpy( arp->tha, target_mac, sizeof( arp->tha ) );
	
	// set target protocol address
	memcpy( arp->tpa, target_ip, sizeof( arp->tpa ) );
}


PT_THREAD( arp_aging_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
	
	while(1){
		
		static uint32_t timewait;
		
		timewait = ARP_AGING_TICK_MS;
		
		TMR_WAIT( pt, timewait );
		
		for( uint8_t i = 0; i < ARP_MAX_CACHE_ENTRIES; i++ ){
			
			// check if age needs to be incremented
			if( arp_cache[i].age < ARP_MAX_AGE_TICKS ){
				
				arp_cache[i].age++;
				
				// if entry is too old, clear it
				if( arp_cache[i].age >= ARP_MAX_AGE_TICKS ){
					
					memset( &arp_cache[i].ip_addr, 0, sizeof(arp_cache[i].ip_addr) );
				}
			}
		}
	}
	
PT_END( pt );
}

// (C)2009-2011 by Jeremy Billheimer
