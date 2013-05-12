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


#include <avr/io.h>
#include <avr/pgmspace.h>

#include "cpu.h"

#include "system.h"
#include "target.h"
#include "timers.h"
#include "threading.h"
#include "adc.h"

#include "io.h"

typedef struct{
    volatile uint8_t *port_reg;
    volatile uint8_t *pin_reg;
    volatile uint8_t *ddr_reg;
    uint8_t pin;
    uint8_t type;
} pin_map_t;


static PROGMEM pin_map_t pin_map[] = {
    //{0, 0, 0, 0, 0},                                                                                  // 1 - V+
    //{0, 0, 0, 0, 0},                                                                                  // 2 - G
    {&PORTE, &PINE, &DDRE, 0, IO_TYPE_INPUT | IO_TYPE_OUTPUT},                                          // 3 - RX
    {&PORTE, &PINE, &DDRE, 1, IO_TYPE_INPUT | IO_TYPE_OUTPUT},                                          // 4 - TX
    {&PORTE, &PINE, &DDRE, 3, IO_TYPE_INPUT | IO_TYPE_OUTPUT | IO_TYPE_ANALOG_OUT},                     // 5 - P0
    {&PORTE, &PINE, &DDRE, 4, IO_TYPE_INPUT | IO_TYPE_OUTPUT | IO_TYPE_ANALOG_OUT},                     // 6 - P1
    {&PORTE, &PINE, &DDRE, 5, IO_TYPE_INPUT | IO_TYPE_OUTPUT | IO_TYPE_ANALOG_OUT},                     // 7 - P2
    //{0, 0, 0, 0, 0},                                                                                  // 8 - G
    {&PORTF, &PINF, &DDRF, 0, IO_TYPE_INPUT | IO_TYPE_OUTPUT | IO_TYPE_ANALOG_IN},                      // 9 - A0
    {&PORTF, &PINF, &DDRF, 1, IO_TYPE_INPUT | IO_TYPE_OUTPUT | IO_TYPE_ANALOG_IN},                      // 10 - A1
    {&PORTF, &PINF, &DDRF, 2, IO_TYPE_INPUT | IO_TYPE_OUTPUT | IO_TYPE_ANALOG_IN},                      // 11 - A2
    {&PORTF, &PINF, &DDRF, 3, IO_TYPE_INPUT | IO_TYPE_OUTPUT | IO_TYPE_ANALOG_IN},                      // 12 - A3
    {&PORTF, &PINF, &DDRF, 4, IO_TYPE_INPUT | IO_TYPE_OUTPUT | IO_TYPE_ANALOG_IN},                      // 13 - A4
    {&PORTF, &PINF, &DDRF, 5, IO_TYPE_INPUT | IO_TYPE_OUTPUT | IO_TYPE_ANALOG_IN},                      // 14 - A5
    {&PORTF, &PINF, &DDRF, 6, IO_TYPE_INPUT | IO_TYPE_OUTPUT | IO_TYPE_ANALOG_IN},                      // 15 - A6
    {&PORTF, &PINF, &DDRF, 7, IO_TYPE_INPUT | IO_TYPE_OUTPUT | IO_TYPE_ANALOG_IN},                      // 16 - A7
    
    {&PORTG, &PING, &DDRG, 0, IO_TYPE_INPUT | IO_TYPE_OUTPUT},                                          // 17 - D0
    {&PORTG, &PING, &DDRG, 1, IO_TYPE_INPUT | IO_TYPE_OUTPUT},                                          // 18 - D1
    {&PORTG, &PING, &DDRG, 2, IO_TYPE_INPUT | IO_TYPE_OUTPUT},                                          // 19 - D2
    {&PORTG, &PING, &DDRG, 5, IO_TYPE_INPUT | IO_TYPE_OUTPUT},                                          // 20 - D3
    {&PORTD, &PIND, &DDRD, 0, IO_TYPE_INPUT | IO_TYPE_OUTPUT},                                          // 21 - D4
    {&PORTD, &PIND, &DDRD, 1, IO_TYPE_INPUT | IO_TYPE_OUTPUT},                                          // 22 - D5
    {&PORTD, &PIND, &DDRD, 2, IO_TYPE_INPUT | IO_TYPE_OUTPUT},                                          // 23 - D6
    {&PORTD, &PIND, &DDRD, 3, IO_TYPE_INPUT | IO_TYPE_OUTPUT},                                          // 24 - D7
    //{0, 0, 0, 0},                                                                                     // 25 - G
    {&PORTB, &PINB, &DDRB, 0, IO_TYPE_INPUT | IO_TYPE_OUTPUT},                                          // 26 - S0
    {&PORTB, &PINB, &DDRB, 1, IO_TYPE_INPUT | IO_TYPE_OUTPUT},                                          // 27 - S1
    {&PORTB, &PINB, &DDRB, 2, IO_TYPE_INPUT | IO_TYPE_OUTPUT},                                          // 28 - S2
    {&PORTB, &PINB, &DDRB, 3, IO_TYPE_INPUT | IO_TYPE_OUTPUT},                                          // 29 - S3
    //{0, 0, 0, 0, 0},                                                                                  // 30 - RST
    //{0, 0, 0, 0, 0},                                                                                  // 31 - G
    //{0, 0, 0, 0, 0},                                                                                  // 32 - VCC
    {&PORTD, &PIND, &DDRD, 5, IO_TYPE_OUTPUT},                                                          // 33 - LED0
    {&PORTD, &PIND, &DDRD, 6, IO_TYPE_OUTPUT},                                                          // 34 - LED1
    {&PORTD, &PIND, &DDRD, 7, IO_TYPE_OUTPUT},                                                          // 35 - LED2
    
    {&PORTE, &PINE, &DDRE, 2, IO_TYPE_INPUT},                                                           // 36 - HW ID
};


PT_THREAD( push_button_monitor_thread( pt_t *pt, void *state ) );

static io_int_handler_t int_handlers[IO_N_INT_HANDLERS];


ISR(INT0_vect){
    
    if( int_handlers[0] != 0 ){
        
        int_handlers[0]();
    }
}

ISR(INT1_vect){
    
    if( int_handlers[1] != 0 ){
        
        int_handlers[1]();
    }
}

ISR(INT2_vect){
    
    if( int_handlers[2] != 0 ){
        
        int_handlers[2]();
    }
}

ISR(INT3_vect){
    
    if( int_handlers[3] != 0 ){
        
        int_handlers[3]();
    }
}

void io_v_init( void ){
    
    // set all LEDs to outputs
    io_v_set_mode( IO_PIN_LED0, IO_MODE_OUTPUT );
    io_v_set_mode( IO_PIN_LED1, IO_MODE_OUTPUT );
    io_v_set_mode( IO_PIN_LED2, IO_MODE_OUTPUT );
    
    // check if safe mode
    if( sys_u8_get_mode() != SYS_MODE_SAFE ){
        
        // disable jtag
        //io_v_disable_jtag();
    }

    // create button monitor thread
	thread_t_create( push_button_monitor_thread,
                     PSTR("io_button_monitor"),
                     0,
                     0 );
}

uint8_t io_u8_get_board_rev( void ){
    
    // enable id pin is input
    io_v_set_mode( IO_PIN_HW_ID, IO_MODE_INPUT_PULLUP );
    
    // read hw id pin strapping
    if( io_b_digital_read( IO_PIN_HW_ID ) == LOW ){
        
        return 1;
    }
    
    return 0;
}


void io_v_set_mode( uint8_t pin, io_mode_t8 mode ){
    
    pin_map_t mapping;
    memcpy_P( &mapping, &pin_map[pin], sizeof(mapping) );
    
    // check current mode
    // see section 14.2.4 of the ATMega128RFA1 data sheet:
    // switching from input states to output states requires
    // an intermediate transition.
    // Note we're actually covering additional transistions for simplicity,
    // we only need to cover high Z to output 1 and pull up to output 0.
    // However, we're always going to transition through the alternate input
    // state before switching, it makes the code much simpler.
    // input tristate:
    if( ( ( *mapping.ddr_reg & _BV(mapping.pin) ) == 0 ) &&
        ( ( *mapping.port_reg & _BV(mapping.pin) ) == 0 ) ){
        
        // set to input pull up
        *mapping.port_reg |= _BV(mapping.pin);
    }
    // input pull up:
    else if( ( ( *mapping.ddr_reg & _BV(mapping.pin) ) == 0 ) &&
             ( ( *mapping.port_reg & _BV(mapping.pin) ) != 0 ) ){
        
        // set to input high Z
        *mapping.port_reg &= ~_BV(mapping.pin);
    }

    if( mode == IO_MODE_INPUT ){
        
        // DDR to input
        *mapping.ddr_reg &= ~_BV(mapping.pin);

        // PORT to High Z
        *mapping.port_reg &= ~_BV(mapping.pin);
    }
    else if( mode == IO_MODE_INPUT_PULLUP ){
        
        // DDR to input
        *mapping.ddr_reg &= ~_BV(mapping.pin);
        
        // PORT to pull up
        *mapping.port_reg |= _BV(mapping.pin);
    }
    else if( mode == IO_MODE_OUTPUT ){
        
        // DDR to output
        *mapping.ddr_reg |= _BV(mapping.pin);
        
        // set pin to output low
        *mapping.port_reg &= ~_BV(mapping.pin);
    }
    else{
        
        ASSERT( FALSE );
    }
}

void io_v_digital_write( uint8_t pin, bool state ){
    
    pin_map_t mapping;
    memcpy_P( &mapping, &pin_map[pin], sizeof(mapping) );
    
    if( state ){
        
        *mapping.port_reg |= _BV(mapping.pin);
    }
    else{
        
        *mapping.port_reg &= ~_BV(mapping.pin);
    }
}

bool io_b_digital_read( uint8_t pin ){

    pin_map_t mapping;
    memcpy_P( &mapping, &pin_map[pin], sizeof(mapping) );
    
    return ( *mapping.pin_reg & _BV(mapping.pin) ) != 0;
}

bool io_b_button_down( void ){
    
    return ( adc_u16_read_mv( ADC_CHANNEL_VSUPPLY ) < IO_BUTTON_TRIGGER_THRESHOLD ); 
}

void io_v_disable_jtag( void ){
    
    ATOMIC;
    
    // disable JTAG
    MCUCR |= ( 1 << JTD );
    MCUCR |= ( 1 << JTD );
    
    END_ATOMIC;
}

void io_v_enable_interrupt( 
    uint8_t int_number, 
    io_int_handler_t handler,
    io_int_mode_t8 mode )
{
    
    ASSERT( int_number < IO_N_INT_HANDLERS );

    ATOMIC;
    
    // set interrupt handler
    int_handlers[int_number] = handler;
    
    // set up mode bits
    uint8_t mode_bits;

    switch( mode ){
        case IO_INT_LOW:
            mode_bits = 0;
            break;

        case IO_INT_CHANGE:
            mode_bits = 1;
            break;

        case IO_INT_FALLING:
            mode_bits = 2;
            break;

        case IO_INT_RISING:
            mode_bits = 3;
            break;
        
        default:
            ASSERT( FALSE );
            break;
    }

    // enable hardware interrupt
    switch( int_number ){
        case 0:
            EICRA &= ~( ( 1 << ISC01 ) | ( 1 << ISC00 ) );
            EICRA |= ( mode_bits << 0 );
            EIMSK |= ( 1 << INT0 );
            break;

        case 1:
            EICRA &= ~( ( 1 << ISC11 ) | ( 1 << ISC10 ) );
            EICRA |= ( mode_bits << 2 );
            EIMSK |= ( 1 << INT1 );
            break;

        case 2:
            EICRA &= ~( ( 1 << ISC21 ) | ( 1 << ISC20 ) );
            EICRA |= ( mode_bits << 4 );
            EIMSK |= ( 1 << INT2 );
            break;

        case 3:
            EICRA &= ~( ( 1 << ISC31 ) | ( 1 << ISC30 ) );
            EICRA |= ( mode_bits << 6 );
            EIMSK |= ( 1 << INT3 );
            break;
    }

    END_ATOMIC;
}

void io_v_disable_interrupt( uint8_t int_number )
{
 
    ASSERT( int_number < IO_N_INT_HANDLERS );
    
    ATOMIC;
    
    // clear interrupt handler
    int_handlers[int_number] = 0;
    
    // disable hardware interrupt
    switch( int_number ){
        case 0:
            EIMSK &= ~( 1 << INT0 );
            break;

        case 1:
            EIMSK &= ~( 1 << INT1 );
            break;

        case 2:
            EIMSK &= ~( 1 << INT2 );
            break;

        case 3:
            EIMSK &= ~( 1 << INT3 );
            break;
    }

    END_ATOMIC;
}


PT_THREAD( push_button_monitor_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );
	
	static uint32_t timer;
	static uint8_t hold_time;

	while(1){
	
		timer = IO_BUTTON_CHECK_INTERVAL;	
		
		TMR_WAIT( pt, timer );
		
		// check if button is pressed
		if( io_b_button_down() ){ 
			
			// increment hold time
			hold_time++;

			// check hold timer
			if( hold_time >= IO_BUTTON_RESTART_TIME ){
				
				// restart!
				sys_reboot();	
			}
		}
		// button released
		else{
						
			// reset hold timer
			hold_time = 0;
		}	
	}
	
PT_END( pt );
}
