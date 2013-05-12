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

#ifndef _LOGGING_H
#define _LOGGING_H

#include "netmsg.h"

// uncomment to disable the logging module.
// this can save considerable ROM space and improve runtime performance.
//#define NO_LOGGING

// uncomment this to enable ICMP packet tracing
//#define LOG_ICMP

#ifndef NO_LOGGING
    #define LOG_ENABLE
#endif

#define LOG_STR_BUF_SIZE        256

#define LOG_LEVEL_DEBUG         0
#define LOG_LEVEL_INFO          1
#define LOG_LEVEL_WARN          2
#define LOG_LEVEL_ERROR         3
#define LOG_LEVEL_CRITICAL      4

#define LOG_LEVEL               LOG_LEVEL_DEBUG


#ifdef LOG_ENABLE
    void log_v_init( void );
    void _log_v_icmp( netmsg_t netmsg, PGM_P file, uint16_t line );
    void _log_v_print_P( uint8_t level, PGM_P file, uint16_t line, PGM_P format, ... );
    
    #define log_v_print_P( level, format, ... ) _log_v_print_P( level, PSTR( __FILE__ ), __LINE__, format, ##__VA_ARGS__ )
    
    #define log_v_debug_P( format, ... )    _log_v_print_P( 0, PSTR( __FILE__ ), __LINE__, format, ##__VA_ARGS__ )
    #define log_v_info_P( format, ... )     _log_v_print_P( 1, PSTR( __FILE__ ), __LINE__, format, ##__VA_ARGS__ )
    #define log_v_warn_P( format, ... )     _log_v_print_P( 2, PSTR( __FILE__ ), __LINE__, format, ##__VA_ARGS__ )
    #define log_v_error_P( format, ... )    _log_v_print_P( 3, PSTR( __FILE__ ), __LINE__, format, ##__VA_ARGS__ )
    #define log_v_critical_P( format, ... ) _log_v_print_P( 4, PSTR( __FILE__ ), __LINE__, format, ##__VA_ARGS__ )

    #define log_v_icmp( netmsg ) _log_v_icmp( netmsg, PSTR( __FILE__ ), __LINE__ )

#else
    #define log_v_init()
    #define log_v_icmp( ... )
    #define log_v_print_P( ... )
    #define log_v_debug_P( ... )
    #define log_v_info_P( ... )  
    #define log_v_warn_P( ... )    
    #define log_v_error_P( ... )    
    #define log_v_critical_P( ... ) 

#endif

#endif

