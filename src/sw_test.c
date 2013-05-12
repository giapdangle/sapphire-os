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


 
/*

Framework for software tests

*/



#include "cpu.h"

#include "io.h"
#include "usart.h"
#include "config.h"

#include "sw_test.h"

#include <stdio.h>


static uint16_t tests_ran;
static uint16_t tests_passed;
static uint16_t tests_failed;

static int uart_putchar( char c, FILE *stream );
static FILE uart_stdout = FDEV_SETUP_STREAM( uart_putchar, NULL, _FDEV_SETUP_WRITE );

static int uart_putchar( char c, FILE *stream )
{
	if( c == '\n' ){
		
        us0_v_send_char( '\r' );
	}
	
	us0_v_send_char( c );
	
	return 0;
}

void sw_test_v_init( void ){
    
	// disable watchdog timer
	wdt_reset();
	MCUSR &= ~( 1 << WDRF ); // ensure watchdog reset is clear
	WDTCSR |= ( 1 << WDCE ) | ( 1 << WDE );
	WDTCSR = 0x00;

    sys_v_init();

    // init serial port
    us0_v_init();
    
    // set stdout
	stdout = &uart_stdout;

    // set all LEDs to outputs
    io_v_set_mode( IO_PIN_LED0, IO_MODE_OUTPUT );
    io_v_set_mode( IO_PIN_LED1, IO_MODE_OUTPUT );
    io_v_set_mode( IO_PIN_LED2, IO_MODE_OUTPUT );

    // clear LEDs
    io_v_digital_write( IO_PIN_LED_GREEN, LOW );
    io_v_digital_write( IO_PIN_LED_YELLOW, LOW );
    io_v_digital_write( IO_PIN_LED_RED, LOW );
    
    // note that we don't call io_v_init() because it will create a thread.
    // we don't want any threads in the test mode since they may interfere with
    // the tests.

    sei();

    // check sys mode
    if( sys_u8_get_mode() == SYS_MODE_SAFE ){
        
        printf_P( PSTR("\nASSERT\n") );
        
        cfg_error_log_t error_log;
        cfg_v_read_error_log( &error_log );
        
        printf_P( PSTR("%s"), error_log.log );

        io_v_digital_write( IO_PIN_LED_RED, HIGH );

        for(;;);
    }

}

void sw_test_v_start( PGM_P module_name ){
    
    printf_P( PSTR("SapphireOS Test Framework\n") );
    printf_P( PSTR("Testing module: ") );
    printf_P( module_name );
    printf_P( PSTR("\n") );

    // set yellow LED
    io_v_digital_write( IO_PIN_LED_YELLOW, HIGH );
}

void sw_test_finish( void ){
    
    printf_P( PSTR("\n--------------------------------\n") );
    printf_P( PSTR("Tests ran: %d\n"), tests_ran );
    printf_P( PSTR("Tests passed: %d\n"), tests_passed );

    if( tests_failed > 0 ){
        
        printf_P( PSTR("!!! Tests failed: %d\n"), tests_failed );

        io_v_digital_write( IO_PIN_LED_RED, HIGH );
    }
    else{
        
        io_v_digital_write( IO_PIN_LED_GREEN, HIGH );
    }

    io_v_digital_write( IO_PIN_LED_YELLOW, LOW );
    
    printf_P( PSTR("--------------------------------\n") );
    printf_P( PSTR("Press 'R' to reboot\n") );
    printf_P( PSTR("Press 'L' to reboot into the bootloader\n") );
    printf_P( PSTR("--------------------------------\n") );

    printf_P( PSTR("\n\n") );

    // disable interrupts
    //cli();
    
    // infinite loop, this is the end of the test
    while(1){
        
        // wait for data from serial port
        while( !us0_b_received_char() );

        // get character
        char c = us0_i16_get_char();
        
        if( c == 'R' ){
            
            printf_P( PSTR("Rebooting...\n") );
            
            sys_reboot();    
        }
        else if( c == 'L' ){

            printf_P( PSTR("Rebooting into bootloader...\n") );

            sys_reboot_to_loader();    
        }
    }
}

static void running_test( void ){

    tests_ran++;
    printf_P( PSTR("Test: %d "), tests_ran );
}

static void test_ok( PGM_P file, uint16_t line ){

    printf_P( PSTR("OK File: ") );
    printf_P( file );
    printf_P( PSTR(" Line: %d\n"), line );

    tests_passed++;
}

void sw_test_v_assert_true( bool expr, PGM_P expr_str, PGM_P file, uint16_t line ){
    
    running_test();

    if( !expr ){

        printf_P( PSTR("Failed: Assertion failed: ") );
        printf_P( expr_str );
        printf_P( PSTR(" File: ") );
        printf_P( file );
        printf_P( PSTR(" Line: %d\n"), line );
        
        tests_failed++;
    }
    else{
        
        test_ok( file, line );
    }
}

void sw_test_v_assert_int_equals( int32_t actual, int32_t expected, PGM_P file, uint16_t line ){
 
    running_test();

    if( expected != actual ){

        printf_P( PSTR("Failed: Expected: %ld Actual: %ld"), expected, actual );
        printf_P( PSTR(" File: ") );
        printf_P( file );
        printf_P( PSTR(" Line: %d\n"), line );
        
        tests_failed++;
    }
    else{

        test_ok( file, line );
    }
}


