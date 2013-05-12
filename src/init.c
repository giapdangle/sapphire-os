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

#include <avr/wdt.h>

#include "system.h"
#include "usart.h"
#include "spi.h"
#include "at86rf230.h"
#include "random.h"
#include "eeprom.h"
#include "config.h"
#include "adc.h"
#include "crc.h"
#include "threading.h"
#include "timers.h"
#include "netmsg.h"
#include "wcom_ipv4.h"
#include "wcom_mac.h"
#include "wcom_time.h"
#include "wcom_mac_sec.h"
#include "wcom_neighbors.h"
#include "tftp.h"
#include "sockets.h"
#include "flash25.h"
#include "flash_fs.h"
#include "fs.h"
#include "statistics.h"
#include "routing2.h"
#include "io.h"
#include "status_led.h"
#include "command2.h"
#include "logging.h"
#include "gateway_services.h"
#include "datetime.h"
#include "keyvalue.h"
#include "heartbeat.h"

#include "init.h"



int8_t sapphire_i8_init( void ){
    
    // reset watchdog timer
	wdt_reset();

    cli();	
    
    // Must be called first!    
	sys_v_init(); // init system controller
		
	// disable watchdog timer
	wdt_reset();
	MCUSR &= ~( 1 << WDRF ); // ensure watchdog reset is clear
	WDTCSR |= ( 1 << WDCE ) | ( 1 << WDE );
	WDTCSR = 0x00;
    
	// Must be called second!
	thread_v_init();
	
	// Must be called third!
	mem2_v_init();
    
	// init analog module       
	adc_v_init();               

    // initialize board IO
    io_v_init();
    
    // system check IO for safe mode
    sys_v_check_io_for_safe_mode();

    // turn on green LED during init
    io_v_digital_write( IO_PIN_LED_GREEN, HIGH );
    
    // turn off yellow LED (left on from loader)
    io_v_digital_write( IO_PIN_LED_YELLOW, LOW );
    
    // if safe mode, turn on red led during init
    if( sys_u8_get_mode() == SYS_MODE_SAFE ){
        
        io_v_digital_write( IO_PIN_LED_RED, HIGH );
    }

	// init CRC module
	crc_v_init();
        
	// init SPI port
	spi_v_init();
    
    // init flash driver
    flash25_v_init();

    // init flash file system
    ffs_v_init();		                        

    // init user file system
    fs_v_init();
    
    // init key value service
    kv_v_init();

	// init config manager
	cfg_v_init();		

	// init IP module                       
	ip_v_init();			                    

	// init serial port     
	us0_v_init();													

	// init netmsg				
	netmsg_v_init();
	
    // init sockets
	sock_v_init();      

	// init radio	
	rf_u8_init();
    
	// init random number generator
	rnd_v_init();	

	// init wcom mac	
	wcom_mac_v_init();

	// init wcom mac security
	wcom_mac_sec_v_init();
	
    // init wcom neighbor manager
	wcom_neighbors_v_init();

	// init wcom ipv4 adaptation layer	
	wcom_ipv4_v_init();
    
    // init TFTP
	tftp_v_init();					    																				
    
    // init statistics
	stats_v_init(); 
    
    // init command interface
    cmd2_v_init();

    // init routing module
    route2_v_init();
    
    // init gateway services module
    gate_svc_v_init();

    // init timekeeping
    datetime_v_init();

    // check if safe mode
    if( sys_u8_get_mode() != SYS_MODE_SAFE ){
        
        // init wcom time sync
        wcom_time_v_init();

        // init logging module
        log_v_init();
    }

    // init heartbeat
    hb_v_init();
    
    // initialize status LED
    status_led_v_init();
    
    // turn off green LED after init
    io_v_digital_write( IO_PIN_LED_GREEN, LOW );
    
    if( sys_u8_get_mode() == SYS_MODE_SAFE ){
        return -1;
    }
    else if( sys_u8_get_mode() == SYS_MODE_NO_APP ){
        return -2;
    }
    
    // return system OK
    return 0;
}


void sapphire_run( void ){
    
    // init timers											    	    	     
	// do this just before starting the scheduler so that we won't miss the 
	// first timer interrupt		    
	tmr_v_init();					       
    
    // enable watchdog timer
    sys_v_init_watchdog();
    
	// enable global interrupts	            
	sei();      
    
	// start the thread scheduler					
	thread_start();				
}


