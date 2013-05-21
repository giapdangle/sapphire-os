
#include "cpu.h"

#include "system.h"
#include "config.h"
#include "timers.h"
#include "threading.h"
#include "sockets.h"
#include "random.h"
#include "ip.h"

#include "memory.h"

#include "appcfg.h"
#include "dhcp.h"

#include <string.h>


PT_THREAD( dhcp_client_thread( pt_t *pt, dhcp_thread_state_t *state ) );

uint16_t dhcp_u16_create_dhcp_discover( void *packet, 
                                        ip_addr_t *requested_ip, 
                                        uint32_t *xid,
                                        uint8_t *client_id,
                                        uint8_t client_id_len,
                                        char *hostname );

uint16_t dhcp_u16_create_dhcp_request( void *packet, 
                                       ip_addr_t *requested_ip, 
                                       ip_addr_t *server_ip, 
                                       uint32_t *xid,
                                       uint8_t *client_id,
                                       uint8_t client_id_len,
                                       char *hostname );

void dhcp_v_get_data( void *packet, 
                      uint16_t packet_len, 
                      dhcp_data_t *data, 
                      uint32_t *xid );


dhcp_thread_t dhcp_t_create_client( uint8_t *client_id, 
                                    uint8_t client_id_len, 
                                    char *host_name,
                                    ip_addr_t requested_ip ){
    
    // create dhcp client thread
    thread_t t = thread_t_create( THREAD_CAST(dhcp_client_thread), 
                                  PSTR("dhcp_client"),
                                  0, 
                                  sizeof(dhcp_thread_state_t) );

    // check if thread was created
    if( t < 0 ){
        
        return -1;
    }

    // get thread state
    dhcp_thread_state_t *state = thread_vp_get_data( t );
    
    // create socket
    state->sock = sock_s_create( SOCK_DGRAM );

    // check allocation:
    if( state->sock < 0 ){
        
        thread_v_kill( t );

        return -1;
    }
    
    // set socket flags to disable wireless
    sock_v_set_options( state->sock, SOCK_OPTIONS_NO_WIRELESS );

    // init thread state
    state->status = DHCP_STATUS_UNCONFIGURED;
    
    // set up transaction id
    state->xid = ( (uint32_t)rnd_u16_get_int() << 16 ) + rnd_u16_get_int();
    
    // bounds check client id
    if( client_id_len > sizeof(state->client_id) ){

        client_id_len = sizeof(state->client_id);
    }
    
    // copy client id
    memcpy( state->client_id, client_id, client_id_len );
    state->client_id_len = client_id_len;

    // copy hostname
    strncpy( state->hostname, host_name, sizeof(state->hostname) );

    // copy requested IP
    memcpy( &state->requested_ip, &requested_ip, sizeof(state->requested_ip) );
    
    return t;
}

dhcp_thread_t dhcp_t_kill( dhcp_thread_t t ){
    
    // get thread state
    dhcp_thread_state_t *state = thread_vp_get_data( t );
    
    // close socket
    sock_v_release( state->sock );

    // kill thread
    thread_v_kill( t );

    return -1;
}

uint8_t dhcp_u8_get_status( dhcp_thread_t t ){
    
    // get thread state
    dhcp_thread_state_t *state = thread_vp_get_data( t );
    
    return state->status;
}

void dhcp_v_get_config( dhcp_thread_t t, dhcp_config_t *config ){
    
    // get thread state
    dhcp_thread_state_t *state = thread_vp_get_data( t );

    // apply configuration
    config->ip_addr 		= state->dhcp_data.your_addr;
    config->subnet_mask 	= state->dhcp_data.subnet_mask;
    config->server_ip 		= state->dhcp_data.server_ip;
    config->router_ip 		= state->dhcp_data.router;
    config->dns_server_ip   = state->dhcp_data.dns_server;
    config->ip_lease_time 	= HTONL(state->dhcp_data.ip_lease_time);
}

PT_THREAD( dhcp_client_thread( pt_t *pt, dhcp_thread_state_t *state ) )
{
PT_BEGIN( pt );  
	
	// bind to port
	sock_v_bind( state->sock, DHCP_CLIENT_PORT );
	
    // init timeout
    state->timeout = 500;
    
	while( state->status == DHCP_STATUS_UNCONFIGURED ){
		
        // set up timeout
        state->timer = state->timeout;
        TMR_WAIT( pt, state->timer );
        
        if( state->timeout < 4000 ){
            
            state->timeout += 500;
        }
        
        // set up remote broadcast address for discovery
		state->raddr.ipaddr = ip_a_addr(255,255,255,255);
		state->raddr.port = DHCP_SERVER_PORT;
        
		// create dhcp discover packet
		state->pkt_len = dhcp_u16_create_dhcp_discover( state->pkt_buf,
                                                        &state->requested_ip,
                                                        &state->xid,
                                                        state->client_id,
                                                        state->client_id_len,
                                                        state->hostname );

        // send dhcp discover
        THREAD_WAIT_WHILE( pt, sock_i16_sendto( state->sock, state->pkt_buf, state->pkt_len, &state->raddr ) < 0 );
       
        // set up timeout
        state->timer = tmr_u32_get_system_time() + state->timeout;
        
		// wait for packet (we want a DHCP OFFER)
        THREAD_WAIT_WHILE( pt, ( sock_i8_recvfrom( state->sock ) < 0 ) &&
                               ( tmr_i8_compare_time( state->timer ) > 0 ) );

        // check for timeout (no data received)
        if( sock_i16_get_bytes_read( state->sock ) < 0 ){
            
            continue;
        }
       
        // process data from the packet
        dhcp_v_get_data( sock_vp_get_data( state->sock ), 
                         sock_i16_get_bytes_read( state->sock ),
                         &state->dhcp_data,
                         &state->xid );
        
        // check packet type, if it was not an offer, restart the loop
        if( state->dhcp_data.type != DHCP_MESSAGE_TYPE_DHCPOFFER ){
            
            
            continue;
        }
		
        // offer received:
        
        THREAD_YIELD( pt );
        
		// create a dhcp request
		state->pkt_len = dhcp_u16_create_dhcp_request(  state->pkt_buf, 
                                                        &state->dhcp_data.your_addr, 
                                                        &state->dhcp_data.server_ip, 
                                                        &state->xid,
                                                        state->client_id,
                                                        state->client_id_len,
                                                        state->hostname );
        
        // set up remote broadcast address
		state->raddr.ipaddr = ip_a_addr(255,255,255,255);
		state->raddr.port = DHCP_SERVER_PORT;
        
        // send it
        THREAD_WAIT_WHILE( pt, sock_i16_sendto( state->sock, state->pkt_buf, state->pkt_len, &state->raddr ) < 0 );
        
        // set a timeout
        state->timer = tmr_u32_get_system_time() + state->timeout;
        
		// wait for packet (DHCP ACK)
        THREAD_WAIT_WHILE( pt, ( sock_i8_recvfrom( state->sock ) < 0 ) &&
                               ( tmr_i8_compare_time( state->timer ) > 0 ) );
        
        // check for timeout (no data received)
        if( sock_i16_get_bytes_read( state->sock ) < 0 ){
            
            continue;
        }

        // process data from the packet
        dhcp_v_get_data( sock_vp_get_data( state->sock ), 
                         sock_i16_get_bytes_read( state->sock ),
                         &state->dhcp_data,
                         &state->xid );
        
        // check that packet type is an ack, otherwise we need to start over
		if( state->dhcp_data.type != DHCP_MESSAGE_TYPE_DHCPACK ){
            
            continue;
        }

        // set status to configured
        state->status = DHCP_STATUS_CONFIGURED;
	}
	
    // dhcp configured, wait until app kills the thread
    while(1){
        
        state->timer = 1000000;

        TMR_WAIT( pt, state->timer );
    }

PT_END( pt );
}


uint16_t dhcp_u16_create_dhcp_discover( void *packet, 
                                        ip_addr_t *requested_ip, 
                                        uint32_t *xid,
                                        uint8_t *client_id,
                                        uint8_t client_id_len,
                                        char *hostname ){
	
    // set to 0s
    memset( packet, 0, DHCP_PACKET_BUF_SIZE );
    
	dhcp_t *dhcp = (dhcp_t *)packet;
	
	// set up the message header
	dhcp->op 		= DHCP_OP_BOOTREQUEST;
    
	dhcp->htype 	= DHCP_HTYPE_ETHERNET;
	dhcp->hlen	 	= DHCP_HLEN_ETHERNET;

    // set client hardware address
	memset( dhcp->chaddr, 0, sizeof(dhcp->chaddr) );
    cfg_i8_get( CFG_PARAM_ETH_MAC_ADDRESS, dhcp->chaddr );
    
	dhcp->hops	 	= 0;
	
	dhcp->xid	 	= *xid;
	
	dhcp->secs	 	= 0;
	dhcp->flags	 	= HTONS(0x8000); // broadcast
	dhcp->ciaddr	= ip_a_addr(0,0,0,0);
	dhcp->yiaddr	= ip_a_addr(0,0,0,0);
	dhcp->siaddr	= ip_a_addr(0,0,0,0);
	dhcp->giaddr	= ip_a_addr(0,0,0,0);
	
	memset( dhcp->sname, 0, sizeof(dhcp->sname) );
	memset( dhcp->file, 0, sizeof(dhcp->file) );
	dhcp->magic_cookie	= DHCP_MAGIC_COOKIE;
	
	// get a pointer to the start of options
	uint8_t *options = (uint8_t *)packet + sizeof(dhcp_t);
	uint8_t *start_options = options;
	
	// set up options:
	// dhcp message type
	*options++ = DHCP_OPTION_MESSAGE_TYPE; // op
	*options++ = 1; // length
	*options++ = DHCP_MESSAGE_TYPE_DHCPDISCOVER; // message type
	
    // check if requesting an IP address
    if( !ip_b_is_zeroes( *requested_ip ) ){
        
        *options++ = DHCP_OPTION_REQUESTED_IP;
        *options++ = 4;
        memcpy( options, requested_ip, 4 );
        options += 4;
    }

	// client id
	*options++ = DHCP_OPTION_CLIENT_ID;
    
	*options++ = client_id_len + 1;
	*options++ = 0;
    memcpy( options, client_id, client_id_len );
	options += client_id_len;
    
    uint8_t name_len = strnlen( hostname, CFG_STR_LEN );
    
	// host name
	*options++ = DHCP_OPTION_HOST_NAME;
	*options++ = name_len;
    memcpy( options, hostname, name_len );
    options += name_len;
	
	// parameter request list
	*options++ = DHCP_OPTION_PARAMETER_REQUEST_LIST;
	*options++ = 3;
	*options++ = DHCP_OPTION_SUBNET_MASK;
	*options++ = DHCP_OPTION_ROUTER;
	*options++ = DHCP_OPTION_DNS_SERVER;
	
	// end option
	*options++ = DHCP_OPTION_END;
	
    // compute packet length
    uint16_t len = sizeof(dhcp_t) + ( options - start_options );
    
    // check minimum size.
    // this is to handle buggy servers.
    if( len < 300 ){
        
        len = 300;
    }
    
	// return packet length
	return len;
}


uint16_t dhcp_u16_create_dhcp_request( void *packet, 
                                       ip_addr_t *requested_ip, 
                                       ip_addr_t *server_ip, 
                                       uint32_t *xid,
                                       uint8_t *client_id,
                                       uint8_t client_id_len,
                                       char *hostname ){
	
	dhcp_t *dhcp = (dhcp_t *)packet;
	
	// set up the message header
	dhcp->op 		= DHCP_OP_BOOTREQUEST;
    
	dhcp->htype 	= DHCP_HTYPE_ETHERNET;
	dhcp->hlen	 	= DHCP_HLEN_ETHERNET;

    // set client hardware address
	memset( dhcp->chaddr, 0, sizeof(dhcp->chaddr) );
	cfg_i8_get( CFG_PARAM_ETH_MAC_ADDRESS, dhcp->chaddr );
    
	dhcp->hops	 	= 0;
	dhcp->xid	 	= *xid;
	dhcp->secs	 	= 0;
	dhcp->flags	 	= HTONS(0x8000); // broadcast
	dhcp->ciaddr	= ip_a_addr(0,0,0,0);
	dhcp->yiaddr	= ip_a_addr(0,0,0,0);
	dhcp->siaddr	= ip_a_addr(0,0,0,0);
	dhcp->giaddr	= ip_a_addr(0,0,0,0);
	
	memset( dhcp->sname, 0, sizeof(dhcp->sname) );
	memset( dhcp->file, 0, sizeof(dhcp->file) );
	dhcp->magic_cookie	= DHCP_MAGIC_COOKIE;
	
	// get a pointer to the start of options
	uint8_t *options = (uint8_t *)packet + sizeof(dhcp_t);
	uint8_t *start_options = options;
	
	// set up options:
	// dhcp message type
	*options++ = DHCP_OPTION_MESSAGE_TYPE; // op
	*options++ = 1; // length
	*options++ = DHCP_MESSAGE_TYPE_DHCPREQUEST; // message type
	
	// client id
	*options++ = DHCP_OPTION_CLIENT_ID;
    
	*options++ = client_id_len + 1;
	*options++ = 0;
    memcpy( options, client_id, client_id_len );
	options += client_id_len;
    
	// requested IP address
	*options++ = DHCP_OPTION_REQUESTED_IP;
	*options++ = 4;
	memcpy( options, requested_ip, 4 );
	options += 4;
	
	// server IP address
	*options++ = DHCP_OPTION_SERVER_IDENTIFIER;
	*options++ = 4;
	memcpy( options, server_ip, 4 );
	options += 4;
	
    uint8_t name_len = strnlen( hostname, CFG_STR_LEN );
    
	// host name
	*options++ = DHCP_OPTION_HOST_NAME;
	*options++ = name_len;
    memcpy( options, hostname, name_len );
    options += name_len;
	
	// fully qualified domain name
	//*options++ = DHCP_OPTION_FQDN;
	
	// parameter request list
	*options++ = DHCP_OPTION_PARAMETER_REQUEST_LIST;
	*options++ = 3;
	*options++ = DHCP_OPTION_SUBNET_MASK;
	*options++ = DHCP_OPTION_ROUTER;
	*options++ = DHCP_OPTION_DNS_SERVER;
	
	// end option
	*options++ = DHCP_OPTION_END;
	
	// return packet length
	return ( sizeof(dhcp_t) + ( options - start_options ) );
}

static void scan_options( uint8_t *options, uint8_t *end_options, dhcp_data_t *data ){
	
	// scan through options
	while( ( *options != DHCP_OPTION_END ) && ( options < end_options ) ){
		
		uint8_t type = *options++;
		uint8_t len = *options++;
		
		if( type == DHCP_OPTION_MESSAGE_TYPE ){
			
			if( len == 1 ){
				
				data->type = *options;
			}
		}
		else if( type == DHCP_OPTION_SUBNET_MASK ){
			
			if( len == 4 ){
				
				data->subnet_mask = *(ip_addr_t *)options;
			}
		}
		else if( type == DHCP_OPTION_ROUTER ){
			
			if( len == 4 ){
				
				data->router = *(ip_addr_t *)options;
			}
		}
		else if( type == DHCP_OPTION_DNS_SERVER ){
			
			if( len >= 4 ){
				
				data->dns_server = *(ip_addr_t *)options;
			}
		}
		else if( type == DHCP_OPTION_SERVER_IDENTIFIER ){
			
			if( len == 4 ){
				
				data->server_ip = *(ip_addr_t *)options;
			}
		}
		else if( type == DHCP_OPTION_IP_LEASE_TIME ){
			
			if( len == 4 ){
				
				data->ip_lease_time = *(uint32_t *)options;
			}
		}
		else if( type == DHCP_OPTION_OVERLOAD ){
			
			if( len == 1 ){
				
				data->overload = *(uint8_t *)options;
			}
		}
		
		if( ( type != DHCP_OPTION_PAD ) && ( type != DHCP_OPTION_END ) ){
			
			options += len;
		}
	}
}

void dhcp_v_get_data( void *packet, uint16_t packet_len, dhcp_data_t *data, uint32_t *xid ){

    // initialize data to all 0s!
    memset( data, 0, sizeof(dhcp_data_t) );

    data->type = 0;
    
	// make sure the packet size is correct
	if( packet_len < sizeof(dhcp_t) ){
        
        return;
    }
	
	dhcp_t *dhcp = (dhcp_t *)packet;
	
	// check transaction id
	if( dhcp->xid != *xid ){
		
		return;
	}
	
	data->your_addr = dhcp->yiaddr;
	
	// get length of options
	uint16_t options_len = packet_len - sizeof(dhcp_t);
	
	// get a pointer to the start of options
	uint8_t *options = (uint8_t *)packet + sizeof(dhcp_t);
	uint8_t *end_options = options + options_len;
	
	// scan through options
	scan_options( options, end_options, data );
	
	// check overload
	if( ( data->overload == DHCP_OVERLOAD_FILE ) ||
		( data->overload == DHCP_OVERLOAD_BOTH ) ){
		
		options = (uint8_t *)dhcp->file;
		end_options = options + sizeof(dhcp->file);
		
		scan_options( options, end_options, data );
	}
	
	if( ( data->overload == DHCP_OVERLOAD_SNAME ) ||
		( data->overload == DHCP_OVERLOAD_BOTH ) ){
		
		options = (uint8_t *)dhcp->sname;
		end_options = options + sizeof(dhcp->sname);
		
		scan_options( options, end_options, data );
	}
}

