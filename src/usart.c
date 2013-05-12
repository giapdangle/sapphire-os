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

#include "usart.h"


#ifdef PRINTF_SUPPORT
#include <stdio.h>
#endif


#ifdef PRINTF_SUPPORT
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
#endif

void us0_v_init( void ){

    UCSR0A = ( 1 << U2X0 ); // enable double speed mode
    UCSR0B = ( 1 << RXEN0 ) | ( 1 << TXEN0 ); // receive interrupt enabled, rx and tx enabled
    UCSR0C = ( 1 << UCSZ01 ) | ( 1 << UCSZ00 ); // select 8 bit characters

    #ifdef PRINTF_SUPPORT
	stdout = &uart_stdout;
	#endif

    us0_v_set_baud( USART_DEFAULT_BAUD );
}

void us0_v_set_baud( uint32_t baud ){

    // all baud settings assume 16 MHz main clock and double speed bit set

    switch( baud ){
        
        case 2400:
            UBRR0 = 832;
            break;

        case 4800:
            UBRR0 = 416;
            break;

        case 9600:
            UBRR0 = 207;
            break;

        case 14400:
            UBRR0 = 138;
            break;

        case 19200:
            UBRR0 = 103;
            break;

        case 28800:
            UBRR0 = 68;
            break;

        case 38400:
            UBRR0 = 51;
            break;

        case 57600:
            UBRR0 = 34;
            break;

        case 76800:
            UBRR0 = 25;
            break;

        case 115200:
            UBRR0 = 16;
            break;

        case 230400:
            UBRR0 = 8;
            break;

        case 250000:
            UBRR0 = 7;
            break;

        case 500000:
            UBRR0 = 3;
            break;

        case 1000000:
            UBRR0 = 1;
            break;

        default:
            ASSERT( FALSE );
            break;
    }
}

bool us0_b_received_char( void ){
    
    return ( UCSR0A & ( 1 << RXC0 ) );
}

void us0_v_send_char( uint8_t data ){

    // wait for data register empty
    while( ( UCSR0A & ( 1 << UDRE0 ) ) == 0 );
    
    UDR0 = data;
}

void us0_v_send_data( const uint8_t *data, uint8_t len ){
    
    while( len > 0 ){
        
        us0_v_send_char( *data );

        data++;
        len--;
    }   
}

int16_t us0_i16_get_char( void ){
	
    if( UCSR0A & ( 1 << RXC0 ) ){
    
        return UDR0;
    }

    return -1;
}

