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


#include "system.h"
#include "threading.h"
#include "timers.h"
#include "config.h"
#include "io.h"
#include "flash_fs.h"
#include "statistics.h"

#include "status_led.h"
#include "wcom_time.h"

static uint16_t blink_rate;



PT_THREAD( status_led_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
    
    static uint32_t led_timer;
    
	while(1){
        
        if( !cfg_b_wcom_configured() ){
            
            led_timer = 1000;
            TMR_WAIT( pt, led_timer );
            
            io_v_digital_write( IO_PIN_LED_GREEN, HIGH );
            io_v_digital_write( IO_PIN_LED_YELLOW, HIGH );
            
            led_timer = 500;
            TMR_WAIT( pt, led_timer );
            
            io_v_digital_write( IO_PIN_LED_GREEN, LOW );
            io_v_digital_write( IO_PIN_LED_YELLOW, LOW );
            
            led_timer = 500;
            TMR_WAIT( pt, led_timer );
            
            io_v_digital_write( IO_PIN_LED_GREEN, HIGH );
            io_v_digital_write( IO_PIN_LED_YELLOW, HIGH );
            
            led_timer = 500;
            TMR_WAIT( pt, led_timer );
            
            io_v_digital_write( IO_PIN_LED_GREEN, LOW );
            io_v_digital_write( IO_PIN_LED_YELLOW, LOW );
        }
        else if( !cfg_b_ip_configured() ){
            
            led_timer = 100;
            TMR_WAIT( pt, led_timer );
            
            io_v_digital_write( IO_PIN_LED_GREEN, HIGH );
            
            led_timer = 100;
            TMR_WAIT( pt, led_timer );
            
            io_v_digital_write( IO_PIN_LED_GREEN, LOW );
            
            led_timer = 100;
            TMR_WAIT( pt, led_timer );
            
            io_v_digital_write( IO_PIN_LED_GREEN, HIGH );
            
            led_timer = 100;
            TMR_WAIT( pt, led_timer );
            
            io_v_digital_write( IO_PIN_LED_GREEN, LOW );
            
            led_timer = 1000;
            TMR_WAIT( pt, led_timer );
        }
        else{
            
            THREAD_WAIT_WHILE( pt, ( wcom_time_u32_get_network_time_ms() % 1000 ) < blink_rate );
            
            io_v_digital_write( IO_PIN_LED_GREEN, HIGH );

            if( sys_u8_get_mode() != SYS_MODE_NORMAL ){
                
                io_v_digital_write( IO_PIN_LED_RED, HIGH );
            }
       
            THREAD_WAIT_WHILE( pt, ( wcom_time_u32_get_network_time_ms() % 1000 ) > blink_rate );
            
            io_v_digital_write( IO_PIN_LED_GREEN, LOW );
            
            if( sys_u8_get_mode() != SYS_MODE_NORMAL ){
                
                io_v_digital_write( IO_PIN_LED_RED, LOW );
            }
        }
	}
	
PT_END( pt );
}

PT_THREAD( comm_led_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
    
    static uint32_t led_timer;
    static uint32_t comm_rx_count;
    
    io_v_set_mode( IO_PIN_LED_YELLOW, IO_MODE_OUTPUT );
    
    while(1){
        
        THREAD_WAIT_WHILE( pt, comm_rx_count == stats_u32_read( STAT_WCOM_MAC_DATA_FRAMES_RECEIVED ) );
        
        io_v_digital_write( IO_PIN_LED_YELLOW, HIGH );
        
        led_timer = 50;
        TMR_WAIT( pt, led_timer );
        
        io_v_digital_write( IO_PIN_LED_YELLOW, LOW );
        
        led_timer = 50;
        TMR_WAIT( pt, led_timer );
        
        comm_rx_count = stats_u32_read( STAT_WCOM_MAC_DATA_FRAMES_RECEIVED );
    }

PT_END( pt );
}



static uint32_t error_led_timer;

PT_THREAD( hw_error_led_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
    
    io_v_set_mode( IO_PIN_LED_RED, IO_MODE_OUTPUT );
    
	while(1){    
        
        error_led_timer = 200;
        
		TMR_WAIT( pt, error_led_timer );
		
		io_v_digital_write( IO_PIN_LED_RED, HIGH );
		
        error_led_timer = 200;
        
		TMR_WAIT( pt, error_led_timer );
        
		io_v_digital_write( IO_PIN_LED_RED, LOW );
	}
	
PT_END( pt );
}

void status_led_v_init( void ){
    
    thread_t_create( status_led_thread,
                     PSTR("status_led"),
                     0,
                     0 );

    thread_t_create( comm_led_thread,
                     PSTR("comm_led"),
                     0,
                     0 );
    
    // check flash fs status
    /*if( flash_fs_u8_get_status() & FLASH_FS_HW_ERROR ){
        
        thread_t_create( hw_error_led_thread );
    }*/

    status_led_v_set_blink_speed( LED_BLINK_NORMAL );
}


void status_led_v_set_blink_speed( uint16_t speed ){
    
    blink_rate = speed; 
}


