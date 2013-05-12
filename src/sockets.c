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

#include "threading.h"
#include "timers.h"
#include "random.h"
#include "system.h"
#include "sockets.h"
#include "statistics.h"
#include "udp.h"
#include "udpx.h"

#include "list.h"
#include "memory.h"
#include "netmsg.h"

#include <string.h>



// raw socket structure
// the beginning of any specific socket structure must
// match this exactly.
typedef struct{
	sock_type_t8 type;          // socket type
	sock_options_t8 options;    // socket options
} sock_state_raw_t;

// datagram structure
typedef struct{
    sock_state_raw_t raw;
    uint8_t state;              // current state
    uint16_t lport;             // local port
    mem_handle_t handle;        // received data handle
    sock_addr_t raddr;          // bound remote address
    struct{
        unsigned setting :4;
        unsigned current :4;
    } timer;                    // receive timeout state
} sock_state_dgram_t;

// UDPX client structure
typedef struct{
    sock_state_dgram_t dgram;
    uint8_t msg_id;
    uint8_t time_left;
    uint8_t tries;
} sock_state_udpx_client_t;

// UDPX server structure
typedef struct{
    sock_state_dgram_t dgram;
    uint8_t msg_id;   
    bool ack_request;
} sock_state_udpx_server_t;


static list_t sockets;

static uint16_t current_ephemeral_port;


PT_THREAD( timeout_thread( pt_t *pt, void *state ) );
PT_THREAD( udpx_timeout_thread( pt_t *pt, void *state ) );


bool sock_b_port_in_use( uint16_t port ){
    
    socket_t sock = sockets.head;

    while( sock >= 0 ){
        
        sock_state_raw_t *raw_state = list_vp_get_data( sock );
        
        if( SOCK_IS_DGRAM( raw_state->type ) ){
            
            sock_state_dgram_t *dgram_state = (sock_state_dgram_t *)raw_state;
            
            // check port
            if( dgram_state->lport == port ){
                
                return TRUE;
            }
        }
        
        sock = list_ln_next( sock );
    }
    
    return FALSE;
}

// this function gets a local port that is between the given bounds and is
// guaranteed to not be in use by any other port.
static uint16_t get_lport( void ){
	
	do{
	    
		current_ephemeral_port++;
		
		if( current_ephemeral_port < SOCK_EPHEMERAL_PORT_LOW ){
			
			current_ephemeral_port = SOCK_EPHEMERAL_PORT_LOW;
		}
		else if( current_ephemeral_port == SOCK_EPHEMERAL_PORT_HIGH ){
			
            // wraparound to low range
			current_ephemeral_port = SOCK_EPHEMERAL_PORT_LOW;
		}
        
	} while( sock_b_port_in_use( current_ephemeral_port ) );

	return current_ephemeral_port;
}


// allocate a socket and return its descriptor
// returns -1 if no sockets are available
socket_t sock_s_create( sock_type_t8 type ){
	
    uint16_t state_size = 0;

    // check state size based on type
    if( type == SOCK_DGRAM ){
        
        state_size = sizeof(sock_state_dgram_t);
    }
    else if( type == SOCK_UDPX_CLIENT ){
        
        state_size = sizeof(sock_state_udpx_client_t);
    }
    else if( type == SOCK_UDPX_SERVER ){
        
        state_size = sizeof(sock_state_udpx_server_t);
    }
    else{
        
        // invalid socket type
        ASSERT( FALSE );
    }

    // allocate a memory handle for the socket
    list_node_t ln = list_ln_create_node( 0, state_size );
    
    // check if allocation succeeded
    if( ln < 0 ){
        
        return -1;
    }

    // get raw state
    sock_state_raw_t *socket = list_vp_get_data( ln );
    
    // set type
    socket->type = type;

    // initialize options
    socket->options = 0;
    
    // set up type specific state
    if( SOCK_IS_DGRAM( socket->type ) ){
    
        // upgrade pointer to datagram structure
        sock_state_dgram_t *dgram = (sock_state_dgram_t *)socket;

        dgram->lport            = get_lport();
        dgram->handle           = -1;
        dgram->state            = SOCK_UDP_STATE_IDLE;
        dgram->timer.setting    = 0;
        dgram->timer.current    = 0;
    }

    if( SOCK_IS_UDPX_CLIENT( socket->type ) ){
        
        // upgrade pointer to datagram structure
        sock_state_udpx_client_t *client = (sock_state_udpx_client_t *)socket;

        client->msg_id      = 0;
        client->time_left   = 0;
        client->tries       = 0;
    }
    else if( SOCK_IS_UDPX_SERVER( socket->type ) ){
            
        // upgrade pointer to datagram structure
        sock_state_udpx_server_t *server = (sock_state_udpx_server_t *)socket;

        server->msg_id      = 0;
    }

    // add socket to list
    list_v_insert_tail( &sockets, ln );

    return ln;
}

// release a socket
void sock_v_release( socket_t sock ){
    
    sock_state_raw_t *s = list_vp_get_data( sock );
    
    // check type for protocol specific cleanup
    if( SOCK_IS_DGRAM( s->type ) ){
        
        sock_state_dgram_t *dgram = (sock_state_dgram_t *)s;
        
        // check if there is data in the socket's buffer
        if( dgram->handle >= 0 ){

            mem2_v_free( dgram->handle );
        }
    }
    
    // remove socket from list
    list_v_remove( &sockets, sock );

    // release socket
    list_v_release_node( sock );
}


// bind a port to a socket
// generally only a server will need to call this, since on a client the
// port will automatically be assigned.
// This function will assert if the requested port is already in use.
// Note that because of this, it is probably a bad idea to request
// any of the ephemeral ports.
void sock_v_bind( socket_t sock, uint16_t port ){
	
    sock_state_raw_t *s = list_vp_get_data( sock );
	
    if( SOCK_IS_DGRAM( s->type ) ){
			
        // assert if port is in use
        ASSERT( sock_b_port_in_use( port ) == FALSE );
        
        // get more specific pointer
        sock_state_dgram_t *dgram = (sock_state_dgram_t *)s;
        
        // set port
        dgram->lport = port;
	}
    else{
        
        // invalid socket type
        ASSERT( FALSE ); 
    }
}

void sock_v_set_options( socket_t sock, sock_options_t8 options ){
    
    sock_state_raw_t *s = list_vp_get_data( sock );
    
    s->options = options;
}

// timeout is in seconds
void sock_v_set_timeout( socket_t sock, uint8_t timeout ){
    
	sock_state_raw_t *s = list_vp_get_data( sock );
    
    if( SOCK_IS_DGRAM( s->type ) ){
        
        // get more specific pointer
        sock_state_dgram_t *dgram = (sock_state_dgram_t *)s;
        
        // range check timeout
        if( timeout > SOCK_MAXIMUM_TIMEOUT ){
            
            timeout = SOCK_MAXIMUM_TIMEOUT;
        }

        // set timer
        dgram->timer.setting = timeout;
        dgram->timer.current = 0;
    }
    else{
        
        // invalid socket type
        ASSERT( FALSE ); 
    }
}

// get the remote address for a socket
void sock_v_get_raddr( socket_t sock, sock_addr_t *raddr ){
	
	sock_state_raw_t *s = list_vp_get_data( sock );
    
    if( SOCK_IS_DGRAM( s->type ) ){
        
        // get more specific pointer
        sock_state_dgram_t *dgram = (sock_state_dgram_t *)s;

        *raddr = dgram->raddr;
    }
    else{
        
        // invalid socket type
        ASSERT( FALSE ); 
    }
}

// set the remote address for a socket
void sock_v_set_raddr( socket_t sock, sock_addr_t *raddr ){

    sock_state_raw_t *s = list_vp_get_data( sock );
    
    if( SOCK_IS_DGRAM( s->type ) ){
        
        // get more specific pointer
        sock_state_dgram_t *dgram = (sock_state_dgram_t *)s;

        dgram->raddr = *raddr;
    }
    else{
        
        // invalid socket type
        ASSERT( FALSE ); 
    }
}

uint16_t sock_u16_get_lport( socket_t sock ){

    sock_state_raw_t *s = list_vp_get_data( sock );
    
    uint16_t port = 0;
    
    if( SOCK_IS_DGRAM( s->type ) ){
    
        // get more specific pointer
        sock_state_dgram_t *dgram = (sock_state_dgram_t *)s;

        port = dgram->lport;
    }
    else{
        
        // invalid socket type
        ASSERT( FALSE ); 
    }
    
    return port;
}

int16_t sock_i16_get_bytes_read( socket_t sock ){
	
    sock_state_raw_t *s = list_vp_get_data( sock );
    
    if( SOCK_IS_DGRAM( s->type ) ){
    
        // get more specific pointer
        sock_state_dgram_t *dgram = (sock_state_dgram_t *)s;
        
        if( dgram->handle >= 0 ){
            
            return mem2_u16_get_size( dgram->handle );
        }
    }
    else{
        
        // invalid socket type
        ASSERT( FALSE ); 
    }
    
    return -1;
}

// Gets a pointer to the data received by the socket.
// This will NOT release the memory!
void *sock_vp_get_data( socket_t sock ){
    
    // get pointer to socket state
    sock_state_raw_t *s = list_vp_get_data( sock );
    
    if( SOCK_IS_DGRAM( s->type ) ){
    
        // get more specific pointer
        sock_state_dgram_t *dgram = (sock_state_dgram_t *)s;
        
        // ensure data has been received for the socket
        ASSERT( dgram->handle >= 0 );
            
        return mem2_vp_get_ptr( dgram->handle );
    }
    else{
        
        // invalid socket type
        ASSERT( FALSE ); 
    }

    return 0;
}

// check if a socket is busy, meaning it will
// not be possible to transmit a message on it.
// note this function does not guarantee that a send
// call will complete successfully, but will give a good
// indication if it will definitely fail.
bool sock_b_busy( socket_t sock ){

    // check netmsg count
    if( netmsg_u8_count() >= NETMSG_MAX_MESSAGES ){
        
        return FALSE;
    }
    
    // check memory availability
    if( mem2_u16_get_free() < SOCK_MEM_BUSY_THRESHOLD ){
        
        return FALSE;
    }
    
    // the sock parameter is unused, but here because eventually
    // we might want to check other parameters, such as flow control
    // by socket, or source address, routing partners, etc.

    return TRUE;
}

// receive data from a socket.
// returns -1 if socket is busy waiting for data
// returns 0 if data is available.
int8_t sock_i8_recvfrom( socket_t sock ){
	
	sock_state_raw_t *s = list_vp_get_data( sock );
    
    if( SOCK_IS_DGRAM( s->type ) ){
    
        // get more specific pointer
        sock_state_dgram_t *dgram = (sock_state_dgram_t *)s;
        
        // check UDPX client
        if( SOCK_IS_UDPX_CLIENT( s->type ) ){
            
            // if waiting for ack
            if( dgram->state == SOCK_UDPX_STATE_WAIT_ACK ){
                
                return -1;
            }
        }

        // check if idle
        if( dgram->state == SOCK_UDP_STATE_IDLE ){
            
            // reset timer
            dgram->timer.current = dgram->timer.setting;
            
            // advance state
            dgram->state = SOCK_UDP_STATE_RX_WAITING;
            
            return -1;
        }
        // check if waiting
        else if( dgram->state == SOCK_UDP_STATE_RX_WAITING ){
            
            return -1;
        }
        // check if data has been buffered
        else if( dgram->state == SOCK_UDP_STATE_RX_DATA_PENDING ){
            
            ASSERT( dgram->handle >= 0 );
            
            // advance state to received
            dgram->state = SOCK_UDP_STATE_RX_DATA_RECEIVED;

            return 0;
        }
        // check if data has been received by the application
        else if( dgram->state == SOCK_UDP_STATE_RX_DATA_RECEIVED ){
            
            // reset state
            dgram->state = SOCK_UDP_STATE_IDLE;
            
            // free the receive buffer
            mem2_v_free( dgram->handle );
            
            // mark handle as empty
            dgram->handle = -1;

            return -1;
        }
        // check if socket was timed out
        else if( dgram->state == SOCK_UDP_STATE_TIMED_OUT ){
            
            // reset state
            dgram->state = SOCK_UDP_STATE_IDLE;
            
            return 0;
        }
    }
    else{
        
        // invalid socket type
        ASSERT( FALSE ); 
    }

    // socket is busy waiting for data
	return -1;
}

// raw transmit function for a socket.
// NOT a user API
int8_t sock_i8_transmit( socket_t sock, void *buf, uint16_t bufsize ){
    
	sock_state_raw_t *s = list_vp_get_data( sock );
    
    if( SOCK_IS_DGRAM( s->type ) ){
        
        // get more specific pointer
        sock_state_dgram_t *dgram = (sock_state_dgram_t *)s;

        uint8_t ttl = 0;

        // check TTL setting
        if( s->options & SOCK_OPTIONS_TTL_1 ){
            
            // set ttl option
            ttl = 1;
        }
        
        netmsg_t netmsg = -1;

        // check for UDPX client
        if( SOCK_IS_UDPX_CLIENT( s->type ) ){
            
            // get more specific pointer
            sock_state_udpx_client_t *udpx_client_dgram = (sock_state_udpx_client_t *)s;

            uint8_t flags = 0;
            
            if( !( udpx_client_dgram->dgram.raw.options & SOCK_OPTIONS_UDPX_NO_ACK_REQUEST ) ){

                flags |= UDPX_FLAGS_ARQ;
            }

            // attempt to create a netmsg object
            netmsg = udpx_nm_create( udpx_client_dgram->msg_id,
                                     flags,
                                     udpx_client_dgram->dgram.lport,
                                     dgram->raddr.port,
                                     dgram->raddr.ipaddr,
                                     ttl,
                                     buf,
                                     bufsize );
        }
        else if( SOCK_IS_UDPX_SERVER( s->type ) ){
            
            // get more specific pointer
            sock_state_udpx_server_t *udpx_server_dgram = (sock_state_udpx_server_t *)s;
            
            uint8_t flags = UDPX_FLAGS_SVR;

            if( udpx_server_dgram->ack_request ){
                
                flags |= UDPX_FLAGS_ACK;
            }

            // attempt to create a netmsg object
            netmsg = udpx_nm_create( udpx_server_dgram->msg_id,
                                     flags,
                                     udpx_server_dgram->dgram.lport,
                                     dgram->raddr.port,
                                     dgram->raddr.ipaddr,
                                     ttl,
                                     buf,
                                     bufsize );
            
            // clear message ID and ack request
            udpx_server_dgram->msg_id       = 0;
            udpx_server_dgram->ack_request  = FALSE;
        }
        else{

            // attempt to create a netmsg object
            netmsg = udp_nm_create( dgram->lport,
                                    dgram->raddr.port,
                                    dgram->raddr.ipaddr,
                                    ttl,
                                    buf,
                                    bufsize );
        }

        // check if netmsg was created
        if( netmsg < 0 ){
            
            return -1;
        }
        
        uint8_t nm_flags = 0;

        // check options
        if( s->options & SOCK_OPTIONS_NO_SECURITY ){
            
            nm_flags |= NETMSG_FLAGS_WCOM_SECURITY_DISABLE;
        }

        if( s->options & SOCK_OPTIONS_NO_WIRELESS ){
            
            nm_flags |= NETMSG_FLAGS_NO_WCOM;
        }

        netmsg_v_set_flags( netmsg, nm_flags );
        
        // check local loopback compile option
        #ifdef SOCK_LOOPBACK
        
        // check loopback
        if( ip_b_check_loopback( dgram->raddr.ipaddr ) ){
            
            // receive immediately
            sock_v_recv_netmsg( netmsg );

            // release the netmsg
            netmsg_v_release( netmsg );
        }
        else{
            
            // add to transmit queue
            netmsg_v_add_to_transmit_q( netmsg );
        }
    
        #else

        // add to transmit queue
        netmsg_v_add_to_transmit_q( netmsg );

        #endif
    }
    else{
        
        // invalid socket type
        ASSERT( FALSE ); 
    }

	return 0;
}


// send data through a given socket, bufsize bytes from buf, to raddr.
// returns number of bytes written
// for TCP sockets, this function will not work, use send()
// for UDP sockets, bufsize must be less than or equal to the maximum datagram
// size
// raddr contains the remote address to send to.
// if 0 is passed, this will use the auto-bound address in the socket
// returns status based on socket type.
// for UDP, returns 0 if successfully queued, -1 if queueing failed
int16_t sock_i16_sendto( socket_t sock, void *buf, uint16_t bufsize, sock_addr_t *raddr ){
	
	sock_state_raw_t *s = list_vp_get_data( sock );
    
    // check socket type
    if( SOCK_IS_UDPX_CLIENT( s->type ) ){
        
        // get more specific pointer
        sock_state_udpx_client_t *udpx_client_dgram = (sock_state_udpx_client_t *)s;
        
        // check if a remote address was given
        if( raddr != 0 ){
            
            udpx_client_dgram->dgram.raddr = *raddr;
        }

        // set up message id
        udpx_client_dgram->msg_id = rnd_u16_get_int();
        
        // get memory to buffer the message
        mem_handle_t h = mem2_h_alloc( bufsize );

        // check if memory was allocated
        if( h <  0 ){
            
            return -1;
        }
        
        // copy data into new buffer
        memcpy( mem2_vp_get_ptr( h ), buf, mem2_u16_get_size( h ) );

        // check if we already have a buffer
        if( udpx_client_dgram->dgram.handle >= 0 ){
            
            // release it
            mem2_v_free( udpx_client_dgram->dgram.handle );
        }

        // assign handle
        udpx_client_dgram->dgram.handle = h;

        // set up timeouts
        udpx_client_dgram->time_left = 0;
        udpx_client_dgram->tries = UDPX_MAX_TRIES;
       
        // NOTE:
        // we don't call the transmit API here.  time left is 0, so the retry thread will immediately
        // initiate the transmit next time it runs.

        // set state to wait ack
        udpx_client_dgram->dgram.state = SOCK_UDPX_STATE_WAIT_ACK;
        
        return 0;
    }
    else if( SOCK_IS_DGRAM( s->type ) ){
        
        // get more specific pointer
        sock_state_dgram_t *dgram = (sock_state_dgram_t *)s;

        // check if a remote address was given
        if( raddr != 0 ){
            
            dgram->raddr = *raddr;
        }

        return sock_i8_transmit( sock, buf, bufsize );
    }
    else{
        
        // invalid socket type
        ASSERT( FALSE ); 
    }

	return 0;
}

// send data contained in a memory handle.
// NOTE: this will release the handle!!!
int16_t sock_i16_sendto_m( socket_t sock, mem_handle_t handle, sock_addr_t *raddr ){
    
    // attempt to send
    int16_t status = sock_i16_sendto( sock, 
                                      mem2_vp_get_ptr( handle ),
                                      mem2_u16_get_size( handle ),
                                      raddr );
    
    // release handle
    mem2_v_free( handle );
    
    return status;
}


// receive an IP packet contained in a netmsg
void sock_v_recv_netmsg( netmsg_t netmsg ){
    
    // get the ip header
    ip_hdr_t *ip_hdr = netmsg_vp_get_data( netmsg );
    
    // check protocol
    if( ip_hdr->protocol != IP_PROTO_UDP ){

        return;
    }
        
    // get UDP header
    udp_header_t *udp_header = (udp_header_t *)( (void *)ip_hdr + sizeof(ip_hdr_t) );
    
    // check UDP checksum, if checksum is enabled
    if( ( udp_header->checksum != 0 ) && 
        ( udp_u16_checksum( ip_hdr ) != HTONS(udp_header->checksum) ) ){
        
        stats_v_increment( STAT_UDP_CHECKSUM_FAILS );
        
        return;
    }
    
    // search for a matching socket
    socket_t sock = sockets.head;
    sock_state_dgram_t *dgram;
    
    while( sock >= 0 ){

        // derefence to datagram
        dgram = list_vp_get_data( sock );
        
        // check socket type and port number (if datagram)
        if( ( SOCK_IS_DGRAM( dgram->raw.type ) ) &&
            ( dgram->lport == HTONS(udp_header->dest_port) ) ){
            
            break;
        }
        
        sock = list_ln_next( sock );
    }
    
    // check if we got a matching socket
    if( sock < 0 ){
        
        return;
    }

    // if we got here, we have the appropriate socket
    
    // check if send only
    if( dgram->raw.options & SOCK_OPTIONS_SEND_ONLY ){
        
        return;
    }

    // check security flags
    if( netmsg_u8_get_flags( netmsg ) & NETMSG_FLAGS_WCOM_SECURITY_DISABLE ){
        // security disabled on this netmsg
        
        // check if this socket requires secure messages
        if( !( dgram->raw.options & SOCK_OPTIONS_NO_SECURITY ) ){
            
            // socket requires secure messages

            return;
        }
    }
    
    void *data_ptr = 0;
    uint16_t data_len = 0;

    // check for UDPX client
    if( SOCK_IS_UDPX_CLIENT( dgram->raw.type ) ){ 
        
        // get pointer to client state
        sock_state_udpx_client_t *client_dgram = (sock_state_udpx_client_t *)dgram;

        // get UDPX header
        udpx_header_t *udpx_header = (udpx_header_t *)( (void *)udp_header + sizeof(udp_header_t) );
        
        // check flags for appropriate bits (SVR and ACK), check message ID, and check
        // that the socket is actually waiting for an ACK
        if( ( ( udpx_header->flags & UDPX_FLAGS_VER1 ) == 0 ) &&
            ( ( udpx_header->flags & UDPX_FLAGS_VER0 ) == 0 ) &&
            ( ( udpx_header->flags & UDPX_FLAGS_SVR )  != 0 ) &&
            ( ( udpx_header->flags & UDPX_FLAGS_ARQ )  == 0 ) &&
            ( ( udpx_header->flags & UDPX_FLAGS_ACK )  != 0 ) &&
            ( client_dgram->msg_id == udpx_header->id )       &&
            ( client_dgram->dgram.state == SOCK_UDPX_STATE_WAIT_ACK ) ){
            
            // delete send buffer
            if( client_dgram->dgram.handle >= 0 ){
                
                mem2_v_free( client_dgram->dgram.handle );
                client_dgram->dgram.handle = -1;
            }

            // set data pointer
            data_ptr = (void *)udpx_header + sizeof(udpx_header_t);
            data_len = HTONS(udp_header->length) - ( sizeof(udp_header_t) + sizeof(udpx_header_t) );
        }
        else{

            // invalid message, throw it away
            return;
        }
    }
    // check for UDPX server
    else if( SOCK_IS_UDPX_SERVER( dgram->raw.type ) ){ 
        
        // get pointer to state
        sock_state_udpx_server_t *server_dgram = (sock_state_udpx_server_t *)dgram;

        // get UDPX header
        udpx_header_t *udpx_header = (udpx_header_t *)( (void *)udp_header + sizeof(udp_header_t) );
        
        // check flags for appropriate bits (SVR and ACK), check message ID, and check
        // that the socket is actually waiting for an ACK
        if( ( ( udpx_header->flags & UDPX_FLAGS_VER1 ) == 0 ) &&
            ( ( udpx_header->flags & UDPX_FLAGS_VER0 ) == 0 ) &&
            ( ( udpx_header->flags & UDPX_FLAGS_SVR )  == 0 ) &&
            ( ( udpx_header->flags & UDPX_FLAGS_ACK )  == 0 ) ){
            
            // assign message ID
            server_dgram->msg_id = udpx_header->id;
            
            // check ack request
            if( udpx_header->flags & UDPX_FLAGS_ARQ ){
                
                server_dgram->ack_request = TRUE;
            }
            else{
                
                server_dgram->ack_request = FALSE;
            }

            // set data pointer
            data_ptr = (void *)udpx_header + sizeof(udpx_header_t);
            data_len = HTONS(udp_header->length) - ( sizeof(udp_header_t) + sizeof(udpx_header_t) );
        }
        else{
            
            // invalid message
            return;
        }
    }
    else{
        
        // regular UDP datagram 
        data_ptr = (void *)udp_header + sizeof(udp_header_t);
        data_len = HTONS(udp_header->length) - sizeof(udp_header_t);
    }


    // check if the socket is already holding data that has not been 
    // received by the owning application.  if so, we'll throw away the 
    // incoming data
    if( dgram->handle >= 0 ){
        
        stats_v_increment( STAT_SOCKETS_PACKETS_DROPPED );
        
        return;
    }
    
    // attempt to allocate a memory handle
    mem_handle_t handle = mem2_h_alloc( data_len );
    
    // check if allocation succeeded
    if( handle < 0 ){
        
        return;
    }
    
    // assign handle to socket
    dgram->handle = handle;
    
    // copy data
    memcpy( mem2_vp_get_ptr( handle ), data_ptr, mem2_u16_get_size( handle ) );
    
    // set remote address
    dgram->raddr.ipaddr = ip_hdr->source_addr;
    dgram->raddr.port   = HTONS(udp_header->source_port);

    // set state
    dgram->state = SOCK_UDP_STATE_RX_DATA_PENDING;
}

void sock_v_init( void ){
	
    list_v_init( &sockets );
	
    // set a random starting port number
	current_ephemeral_port = SOCK_EPHEMERAL_PORT_LOW + ( rnd_u16_get_int() >> 3 );

    // start timeout thread
    thread_t_create( timeout_thread,
                     PSTR("socket_timeout"),
                     0,
                     0 );

    thread_t_create( udpx_timeout_thread,
                     PSTR("udpx_timeout"),
                     0,
                     0 );
}

uint8_t sock_u8_count( void ){
    
    return list_u8_count( &sockets );
}


// timeout thread
PT_THREAD( timeout_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
    
    static uint32_t timer;

    while(1){
        
        timer = SOCK_TIMER_TICK_MS;
        TMR_WAIT( pt, timer );
        
        // scan through all sockets with timers set
        socket_t sock = sockets.head;

        while( sock >= 0 ){
            
            sock_state_raw_t *s = list_vp_get_data( sock );
            
            if( SOCK_IS_DGRAM( s->type ) ){
                
                sock_state_dgram_t *dgram = (sock_state_dgram_t *)s;
                
                // check if timer is active
                if( dgram->timer.current > 0 ){
                    
                    // decrement
                    dgram->timer.current--;
                }

                // check for timeout
                if( ( dgram->timer.setting > 0 ) &&
                    ( dgram->timer.current == 0 ) &&
                    ( dgram->state == SOCK_UDP_STATE_RX_WAITING ) ){
                    
                    // set state to timed out
                    dgram->state = SOCK_UDP_STATE_TIMED_OUT;
                }
            }
            
            // get next socket
            sock = list_ln_next( sock );
        }
    }

PT_END( pt );
}


// UDPX timeout thread
PT_THREAD( udpx_timeout_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
    
    static uint32_t timer;

    while(1){
        
        timer = SOCK_UDPX_TIMER_TICK_MS;
        TMR_WAIT( pt, timer );
        
        // scan through all sockets with timers set
        socket_t sock = sockets.head;

        while( sock >= 0 ){
            
            sock_state_raw_t *s = list_vp_get_data( sock );
            
            if( SOCK_IS_UDPX_CLIENT( s->type ) ){
                
                // get more specific pointer
                sock_state_udpx_client_t *client_dgram = (sock_state_udpx_client_t *)s;
                
                // check state
                if( client_dgram->dgram.state == SOCK_UDPX_STATE_WAIT_ACK ){
                    
                    if( client_dgram->time_left > 0 ){
                        
                        client_dgram->time_left--;
                    }
                    else{
                        
                        client_dgram->tries--;
                           
                        if( client_dgram->tries > 0 ){
                            
                            // attempt transmission
                            sock_i8_transmit( sock, 
                                              mem2_vp_get_ptr( client_dgram->dgram.handle ),
                                              mem2_u16_get_size( client_dgram->dgram.handle ) );
                            
                            // reset timeout
                            client_dgram->time_left = ( UDPX_INITIAL_TIMEOUT / SOCK_UDPX_TIMER_TICK_MS ) * 
                                                      ( UDPX_MAX_TRIES - client_dgram->tries );
                        }
                        else{
                            
                            // delete send buffer
                            if( client_dgram->dgram.handle >= 0 ){
                                
                                mem2_v_free( client_dgram->dgram.handle );
                                client_dgram->dgram.handle = -1;
                            }

                            // no tries left
                            client_dgram->dgram.state = SOCK_UDP_STATE_TIMED_OUT;
                        }
                    }
                }
            }
            
            // get next socket
            sock = list_ln_next( sock );
        }
    }

PT_END( pt );
}



