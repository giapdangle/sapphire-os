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
#include "timers.h"
#include "threading.h"
#include "fs.h"
#include "sockets.h"
#include "config.h"
#include "usart.h"
#include "crc.h"
#include "keyvalue.h"
#include "wcom_time.h"

#include "command2.h"

#include "logging.h"


static socket_t sock;
static file_t file;


static PROGMEM char build_date_str[] = __DATE__;
static PROGMEM char build_time_str[] = __TIME__;

PT_THREAD( command2_server_thread( pt_t *pt, void *state ) );
PT_THREAD( command2_serial_thread( pt_t *pt, void *state ) );


int16_t app_i16_cmd( cmd2_t16 cmd, 
                    const void *data, 
                    uint16_t len,
                    void *response ){
    
    return -1;
}


void cmd2_v_init( void ){

    // create socket
    sock = sock_s_create( SOCK_UDPX_SERVER );

    ASSERT( sock >= 0 );

    // bind to server port
    sock_v_bind( sock, CMD2_SERVER_PORT );
    
    // create server thread
    thread_t_create( command2_server_thread,
                     PSTR("command_server"),
                     0,
                     0 );
    
    // create serial thread
    thread_t_create( command2_serial_thread,
                     PSTR("command_serial_monitor"),
                     0,
                     0 );

    file = -1;
}

// runs command, returns response packet in handle
// returns -1 if the command could not be processed
mem_handle_t cmd2_h_process_cmd( const cmd2_header_t *cmd, int16_t len ){
    
    void *data = (void *)cmd + sizeof(cmd2_header_t);
    len -= sizeof(cmd2_header_t);

    void *response = 0;
    int16_t response_len = 0;

    uint8_t buf[576];
    uint32_t sentinel = 0x12345678;

    if( cmd->cmd == CMD2_ECHO ){
        
        response        = data;
        response_len    = len;
    }
    else if( cmd->cmd == CMD2_REBOOT ){
        
        sys_v_reboot_delay( SYS_MODE_NORMAL );
    }
    else if( cmd->cmd == CMD2_SAFE_MODE ){
        
        sys_v_reboot_delay( SYS_MODE_SAFE );
    }
    else if( cmd->cmd == CMD2_LOAD_FW ){
        
        sys_v_load_fw();
    }
    else if( cmd->cmd == CMD2_FORMAT_FS ){
        
        sys_v_reboot_delay( SYS_MODE_FORMAT );
    }
    else if( cmd->cmd == CMD2_GET_FILE_ID ){

        file_id_t8 *id = (file_id_t8 *)buf;

        *id = fs_i8_get_file_id( data );

        response = buf;
        response_len = sizeof(file_id_t8);
    }
    else if( cmd->cmd == CMD2_CREATE_FILE ){

        file_t f = fs_f_open( data, FS_MODE_CREATE_IF_NOT_FOUND );

        if( f >= 0 ){

            // close file handle if exists
            fs_f_close( f );
        }

        file_id_t8 *id = (file_id_t8 *)buf;

        // return file id
        *id = fs_i8_get_file_id( data );

        response = buf;
        response_len = sizeof(file_id_t8);
    }
    else if( cmd->cmd == CMD2_READ_FILE_DATA ){

        cmd2_file_request_t *req = (cmd2_file_request_t *)data;

        // converting signed to unsigned, return 0 means EOF or not exists
        uint16_t read_len = fs_i16_read_id( req->file_id, req->pos, buf, req->len );       
        
        response_len = read_len;
        response = buf;
    }
    else if( cmd->cmd == CMD2_WRITE_FILE_DATA ){

        cmd2_file_request_t *req = (cmd2_file_request_t *)data;
        data += sizeof(cmd2_file_request_t);

        // bounds check
        if( req->len > sizeof(buf) ){

            req->len = sizeof(buf);
        }

        uint16_t *write_len = (uint16_t *)buf;

        // converting signed to unsigned, return 0 means EOF or not exists
        *write_len = fs_i16_write_id( req->file_id, req->pos, data, req->len );       
        
        response_len = sizeof(uint16_t);
        response = buf;
    }
    else if( cmd->cmd == CMD2_REMOVE_FILE ){

        file_id_t8 *id = (file_id_t8 *)data;
        int8_t *status = (int8_t *)buf;

        *status = fs_i8_delete_id( *id );

        response = buf;
        response_len = sizeof(int8_t);
    }
    else if( cmd->cmd == CMD2_RESET_CFG ){
        
        cfg_v_default_all();
    }
    else if( cmd->cmd == CMD2_REQUEST_ROUTE ){
        
        route_query_t *query = (route_query_t *)data;
        
        route2_i8_discover( query );
    }
    else if( cmd->cmd == CMD2_RESET_WCOM_TIME_SYNC ){
        
        wcom_time_v_reset();
    }
    else if( cmd->cmd == CMD2_SET_KV ){
        
        response_len = kv_i16_batch_set( data, len, buf, sizeof(buf) );
        response = buf;
    }
    else if( cmd->cmd == CMD2_GET_KV ){
        
        response_len = kv_i16_batch_get( data, len, buf, sizeof(buf) );
        response = buf;
    }
    else if( cmd->cmd == CMD2_SET_KV_SERVER ){

        ip_addr_t *ip = (ip_addr_t *)data;
        uint16_t *port = (uint16_t *)(ip + 1);

        // check if command doesn't list an IP address
        if( ip_b_is_zeroes( *ip ) ){
            
            // use command socket source address
            sock_addr_t raddr;
            sock_v_get_raddr( sock, &raddr );

            *ip = raddr.ipaddr;
        }

        kv_v_set_server( *ip, *port );
    }
    else if( cmd->cmd == CMD2_SET_SECURITY_KEY ){
        
        cmd2_set_sec_key_t *k = (cmd2_set_sec_key_t *)data;
        
        cfg_v_set_security_key( k->key_id, k->key );
    }
    else if( cmd->cmd >= CMD2_APP_CMD_BASE ){
        
        response = buf;
        response_len = app_i16_cmd( cmd->cmd, data, len, buf );
    }
    else{
        
        return -1;
    }
    
    // check sentinel
    ASSERT( sentinel == 0x12345678 );

    // check for response
    if( response_len >= 0 ){
        
        // allocate memory for response
        mem_handle_t h = mem2_h_alloc( response_len + 
                                       sizeof(cmd2_header_t) );

        if( h < 0 ){
            
            return -1;
        }

        // get pointer to header
        cmd2_header_t *header = mem2_vp_get_ptr( h );
        
        header->cmd = cmd->cmd;
        
        data = (void *)header + sizeof(cmd2_header_t);

        // copy data
        memcpy( data, response, response_len );
        
        return h;
    }

    return -1;
}



PT_THREAD( command2_server_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
    
    while(1){
        
        THREAD_WAIT_WHILE( pt, sock_i8_recvfrom( sock ) < 0 );
        
        // process command
        mem_handle_t h = cmd2_h_process_cmd( sock_vp_get_data( sock ),
                                             sock_i16_get_bytes_read( sock ) );
        
        // check if there is a response to send
        if( h >= 0 ){
            
            sock_i16_sendto( sock, 
                             mem2_vp_get_ptr( h ),
                             mem2_u16_get_size( h ),
                             0 );
            
            mem2_v_free( h );
        }
    }
    
PT_END( pt );
}


PT_THREAD( command2_serial_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
    
    while(1){
        
        THREAD_YIELD( pt );

        // wait for a frame start indicator
        THREAD_WAIT_WHILE( pt, us0_i16_get_char() != CMD2_SERIAL_SOF );
        
        ATOMIC;

        mem_handle_t handle = -1;
        uint32_t timeout = 0;
        uint16_t len = 0;

        // send ACK
        us0_v_send_char( CMD2_SERIAL_ACK );

        // start timeout
        timeout = CMD2_SERIAL_TIMEOUT_COUNT;

        // copy header
        cmd2_serial_frame_header_t header;
        uint8_t *ptr = (uint8_t *)&header;
        
        for( uint8_t i = 0; i < sizeof(header); i++ ){
            
            // wait for data
            while( ( !us0_b_received_char() ) &&
                   ( timeout > 0 ) ){
                
                timeout--;
            }         
            
            // check for timeout
            if( timeout == 0 ){
                
                // send NAK
                us0_v_send_char( CMD2_SERIAL_NAK );

                goto restart;
            }

            *ptr++ = us0_i16_get_char();
        }
        
        // check header
        if( ( header.len != ~header.inverted_len ) ||
            ( header.len > CMD2_MAX_PACKET_LEN ) ){

            // send NAK
            us0_v_send_char( CMD2_SERIAL_NAK );

            goto restart;
        }
        
        // allocate memory, with room for CRC
        handle = mem2_h_alloc( header.len + sizeof(uint16_t) );

        // check if allocation succeeded
        if( handle < 0 ){
            
            // send NAK
            us0_v_send_char( CMD2_SERIAL_NAK );

            goto restart;
        }
        
        // set receive length
        len = header.len + sizeof(uint16_t);
        
        // start timeout
        timeout = CMD2_SERIAL_TIMEOUT_COUNT;
        
        // get data pointer
        uint8_t *buf = mem2_vp_get_ptr( handle );
        
        // send ACK
        us0_v_send_char( CMD2_SERIAL_ACK );

        // receive loop
        while( len > 0 ){
            
            // wait for data
            while( ( !us0_b_received_char() ) &&
                   ( timeout > 0 ) ){
                
                timeout--;
            }         
            
            // check for timeout
            if( timeout == 0 ){
                
                // send NAK
                us0_v_send_char( CMD2_SERIAL_NAK );
        
                // timeout, restart
                goto cleanup;
            }
            
            // add byte to buffer
            *buf = us0_i16_get_char();
            
            // advance buffer
            buf++;

            // adjust count
            len--;
        }
        
        // received entire command packet

        // check CRC
        if( crc_u16_block( mem2_vp_get_ptr( handle ), 
                           mem2_u16_get_size( handle ) ) != 0 ){
         
            // send NAK
            us0_v_send_char( CMD2_SERIAL_NAK );

            goto cleanup;
        }

        // send ACK
        us0_v_send_char( CMD2_SERIAL_ACK );


        // run command processor
        mem_handle_t response = cmd2_h_process_cmd( mem2_vp_get_ptr( handle ),
                                                    mem2_u16_get_size( handle ) - sizeof(uint16_t) );
        
        // check response
        if( response >= 0 ){
            
            // transmit frame header
            cmd2_serial_frame_header_t header;

            header.len          = mem2_u16_get_size( response );
            header.inverted_len = ~header.len;
            
            us0_v_send_data( (uint8_t *)&header, sizeof(header) );
            
            // set up packet transfer
            len = header.len;
            uint8_t *ptr = mem2_vp_get_ptr( response );
            
            while( len > 0 ){
                
                // send byte
                us0_v_send_char( *ptr );
                
                // adjust pointer and count
                ptr++;
                len--;
            }

            // compute CRC for data
            uint16_t crc = crc_u16_block( mem2_vp_get_ptr( response ),
                                          header.len );
            
            // switch CRC to big endian
            crc = HTONS(crc);

            // send CRC
            us0_v_send_data( (uint8_t *)&crc, sizeof(crc) );
            
            // release memory
            mem2_v_free( response );
        }

cleanup:
    
        // release memory
        mem2_v_free( handle );
        handle = -1;

restart:        
        END_ATOMIC;

    }
    
PT_END( pt );
}

