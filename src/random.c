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
#include "timers.h"
#include "config.h"
#include "threading.h"

#include "at86rf230.h"

#include "random.h"

static uint16_t lfsr;


void init_random( void ){
    
    // initialize random value
    lfsr = (uint16_t)rf_u8_get_random();
    lfsr |= (uint16_t)rf_u8_get_random() << 8;

	// check if we got a 0, the LFSR will not work if initialized with all 0s
	if( lfsr == 0 ){
		
		lfsr = 1;
	}
}

PT_THREAD( rnd_mix_thread( pt_t *pt, void *state ) )
{
PT_BEGIN( pt );  
    
    static uint32_t timer;

    while(1){
        
        timer = 8000;
        TMR_WAIT( pt, timer );
        
        init_random();
    }

PT_END( pt );
}

void rnd_v_init( void ){
    
    init_random();
    
    thread_t_create( rnd_mix_thread,
                     PSTR("random_mix"),
                     0,
                     0 );

}

void rnd_v_seed( uint32_t seed ){
    
    lfsr = seed;
}

uint16_t rnd_u16_get_int( void ){
	
	// pass random value through LFSR
	//lfsr = ( lfsr >> 1 ) ^ 
	//			( -( int16_t )( lfsr & 1 ) & 0b1101000000001000 ); // taps 16 15 13 4

    
    //lfsr = ( lfsr >> 1 ) ^ ( -( lfsr & 1 ) & 0xD0000001 );
    /*
    uint16_t value = 0;

    for( uint8_t i = 0; i < 16; i++ ){
        
        lfsr = ( lfsr >> 1 ) ^ 
                    ( -( lfsr & 1 ) & 0xB400 ); // taps 16 14 13 11
        
        value |= ( lfsr & 1 );
        value <<= 1;
    }*/
    

    uint8_t value0 = 0;

    lfsr = ( lfsr >> 1 ) ^ 
                ( -( lfsr & 1 ) & 0xB400 ); // taps 16 14 13 11

    value0 |= ( lfsr & 1 );
    value0 <<= 1;

    lfsr = ( lfsr >> 1 ) ^ 
                ( -( lfsr & 1 ) & 0xB400 ); // taps 16 14 13 11

    value0 |= ( lfsr & 1 );
    value0 <<= 1;

    lfsr = ( lfsr >> 1 ) ^ 
                ( -( lfsr & 1 ) & 0xB400 ); // taps 16 14 13 11

    value0 |= ( lfsr & 1 );
    value0 <<= 1;

    lfsr = ( lfsr >> 1 ) ^ 
                ( -( lfsr & 1 ) & 0xB400 ); // taps 16 14 13 11

    value0 |= ( lfsr & 1 );
    value0 <<= 1;

    lfsr = ( lfsr >> 1 ) ^ 
                ( -( lfsr & 1 ) & 0xB400 ); // taps 16 14 13 11

    value0 |= ( lfsr & 1 );
    value0 <<= 1;

    lfsr = ( lfsr >> 1 ) ^ 
                ( -( lfsr & 1 ) & 0xB400 ); // taps 16 14 13 11

    value0 |= ( lfsr & 1 );
    value0 <<= 1;

    lfsr = ( lfsr >> 1 ) ^ 
                ( -( lfsr & 1 ) & 0xB400 ); // taps 16 14 13 11

    value0 |= ( lfsr & 1 );
    value0 <<= 1;

    lfsr = ( lfsr >> 1 ) ^ 
                ( -( lfsr & 1 ) & 0xB400 ); // taps 16 14 13 11

    value0 |= ( lfsr & 1 );
    value0 <<= 1;


    uint8_t value1 = 0;

    lfsr = ( lfsr >> 1 ) ^ 
                ( -( lfsr & 1 ) & 0xB400 ); // taps 16 14 13 11

    value1 |= ( lfsr & 1 );
    value1 <<= 1;

    lfsr = ( lfsr >> 1 ) ^ 
                ( -( lfsr & 1 ) & 0xB400 ); // taps 16 14 13 11

    value1 |= ( lfsr & 1 );
    value1 <<= 1;

    lfsr = ( lfsr >> 1 ) ^ 
                ( -( lfsr & 1 ) & 0xB400 ); // taps 16 14 13 11

    value1 |= ( lfsr & 1 );
    value1 <<= 1;

    lfsr = ( lfsr >> 1 ) ^ 
                ( -( lfsr & 1 ) & 0xB400 ); // taps 16 14 13 11

    value1 |= ( lfsr & 1 );
    value1 <<= 1;

    lfsr = ( lfsr >> 1 ) ^ 
                ( -( lfsr & 1 ) & 0xB400 ); // taps 16 14 13 11

    value1 |= ( lfsr & 1 );
    value1 <<= 1;

    lfsr = ( lfsr >> 1 ) ^ 
                ( -( lfsr & 1 ) & 0xB400 ); // taps 16 14 13 11

    value1 |= ( lfsr & 1 );
    value1 <<= 1;

    lfsr = ( lfsr >> 1 ) ^ 
                ( -( lfsr & 1 ) & 0xB400 ); // taps 16 14 13 11

    value1 |= ( lfsr & 1 );
    value1 <<= 1;

    lfsr = ( lfsr >> 1 ) ^ 
                ( -( lfsr & 1 ) & 0xB400 ); // taps 16 14 13 11

    value1 |= ( lfsr & 1 );
    value1 <<= 1;

    return ( (uint16_t)value1 << 8 ) + value0;
}

uint16_t rnd_u16_get_int_hw( void ){
 
    return ( (uint16_t)rf_u8_get_random() << 8 ) + rf_u8_get_random();
}

// fill random data
void rnd_v_fill( void *data, uint16_t len ){
	
	while( len > 0 ){
		
		*(uint8_t *)data = (uint8_t)rnd_u16_get_int();
		
		data++;
		len--;
	}
}

