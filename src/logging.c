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

#include <stdarg.h>

#include "cpu.h"

#include "config.h"
#include "netmsg.h"
#include "ip.h"
#include "timers.h"
#include "fs.h"

#include "logging.h"

#ifdef LOG_ENABLE

void log_v_init( void ){
    
    log_v_info_P( PSTR("Sapphire start") );
}

static void append_log( char *buf ){
    
    file_t f = fs_f_open_P( PSTR("log.txt"), FS_MODE_WRITE_APPEND | FS_MODE_CREATE_IF_NOT_FOUND );
    
    if( f < 0 ){
        
        return;
    }
    
    uint16_t max_log_size;
    cfg_i8_get( CFG_PARAM_MAX_LOG_SIZE, &max_log_size );

    // check file size
    if( fs_i32_get_size( f ) >= max_log_size ){
        
        goto cleanup;
    }
    
    // write to file
    fs_i16_write( f, buf, strnlen( buf, LOG_STR_BUF_SIZE ) );

cleanup:
    fs_f_close( f );
}


void _log_v_print_P( uint8_t level, PGM_P file, uint16_t line, PGM_P format, ... ){
    /*
     * TODO: 
     * The print statements in this function will allow a buffer overflow,
     * despite my best intentions.  For now, the buffer is large enough for
     * most logging needs, and there is a canary check at the end of the buffer,
     * so if it does overflow, it will assert here.
     *
     */


    // check log level
    if( (int8_t)level < LOG_LEVEL ){
        
        return;
    }

    char buf[LOG_STR_BUF_SIZE + 1];
    buf[sizeof(buf) - 1] = 0xff; // set a canary at the end of the buffer
    
    char filename[32];
    strncpy_P( filename, file, sizeof(filename) );
    
    // print level
    char level_str[8];
    
    switch( level ){
        case LOG_LEVEL_INFO:
            strncpy_P( level_str, PSTR("info"), sizeof(level_str) );           
            break;

        case LOG_LEVEL_WARN:
            strncpy_P( level_str, PSTR("warn"), sizeof(level_str) );           
            break;

        case LOG_LEVEL_ERROR:
            strncpy_P( level_str, PSTR("error"), sizeof(level_str) );           
            break;

        case LOG_LEVEL_CRITICAL:
            strncpy_P( level_str, PSTR("crit"), sizeof(level_str) );           
            break;

        default:
            strncpy_P( level_str, PSTR("debug"), sizeof(level_str) );           
            break;
    }

    // print system time, file, and lien
    uint8_t len = snprintf_P( buf, 
                              sizeof(buf), 
                              PSTR("%5s:%8ld:%16s:%4d:"),
                              level_str,
                              tmr_u32_get_system_time_ms(),
                              filename,
                              line );

    va_list ap;

    // parse variable arg list
    va_start( ap, format );
    
    // print string
    len += vsnprintf_P( &buf[len], sizeof(buf) - len, format, ap );
    
    va_end( ap );
    
    // attach new line
    snprintf_P( &buf[len], sizeof(buf) - len, PSTR("\r\n") );
    
    // check canary
    ASSERT( buf[sizeof(buf) - 1] == 0xff );

    // write to log file
    append_log( buf );
}

void _log_v_icmp( netmsg_t netmsg, PGM_P file, uint16_t line ){
    
    #ifdef LOG_ICMP    
    
    // get the ip header
    ip_hdr_t *ip_hdr = (ip_hdr_t *)netmsg_vp_get_data( netmsg );
    
    if( ip_hdr->protocol == IP_PROTO_ICMP ){
        
        //_log_v_print_P( LOG_LEVEL_DEBUG, file, line, PSTR("ICMP") );


        _log_v_print_P( LOG_LEVEL_DEBUG, 
                        file, 
                        line, 
                        PSTR("ICMP From:%d.%d.%d.%d To:%d.%d.%d.%d"),
                        ip_hdr->source_addr.ip3,
                        ip_hdr->source_addr.ip2,
                        ip_hdr->source_addr.ip1,
                        ip_hdr->source_addr.ip0,
                        ip_hdr->dest_addr.ip3,
                        ip_hdr->dest_addr.ip2,
                        ip_hdr->dest_addr.ip1,
                        ip_hdr->dest_addr.ip0 );

    }

    #endif
}


#endif


