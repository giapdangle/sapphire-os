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

#include "at86rf230.h"
#include "io.h"
#include "system.h"

#include "power.h"


#include "logging.h"


// initiates an immediate shutdown of the entire system
// this sets all IO to inputs with pull ups, forces the transceiver to
// sleep mode, and puts the CPU in power down with interrupts disabled.
// the system cannot wake up from a full shutdown, it must be power cycled.
void pwr_v_shutdown( void ){

    // disable interrupts
    cli();

    log_v_critical_P( PSTR("Full stop shutdown") );

	// disable watchdog timer
	wdt_reset();
	WDTCSR |= (1<<WDCE) | (1<<WDE);
	WDTCSR = 0x00;
    
    // set the tranceiver to sleep
    rf_v_set_mode( CMD_FORCE_TRX_OFF );
    TRXPR |= ( 1 << SLPTR ); 
    
    // set all IO to input and pull up
    for( uint8_t i = 0; i < IO_PIN_COUNT; i++ ){
        
        io_v_set_mode( i, IO_MODE_INPUT_PULLUP );
    }
    
    // turn off LEDs
    io_v_digital_write( IO_PIN_LED0, LOW );    
    io_v_digital_write( IO_PIN_LED1, LOW );    
    io_v_digital_write( IO_PIN_LED2, LOW );    
    
    // set all LEDs to outputs
    // this minimizes leakage from the pull up through the LED
    // without this, you'll actually see the red LED glowing faintly.
    io_v_set_mode( IO_PIN_LED0, IO_MODE_OUTPUT );
    io_v_set_mode( IO_PIN_LED1, IO_MODE_OUTPUT );
    io_v_set_mode( IO_PIN_LED2, IO_MODE_OUTPUT );
   
    // set CPU to power down, this is the lowest power mode
    sys_v_sleep( SLP_PWRDN );
    
    for(;;);
}




