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
#include "target.h"

#include "eeprom.h"

#ifdef __SIM__
static uint8_t array[EE_ARRAY_SIZE];
#endif


bool ee_b_busy( void ){
    
    return ( EECR & ( 1 << EEPE ) ) != 0;
}

// write a byte to eeprom and wait for the completion of the write
void ee_v_write_byte_blocking( uint16_t address, uint8_t data ){
	
    // read original byte
    uint8_t original = ee_u8_read_byte( address );

    // check if data is not changing
    if( data == original ){
        
        return;
    }

    #ifdef __SIM__
    
    array[address] = data;
    
    #else
    
    // busy wait
    while( ee_b_busy() );

	ATOMIC; // must be atomic, an interrupt here will cause the write to fail
	
	EEAR = address; // set address
	EEDR = data; // set byte to be written
	
    // clear control bits, this sets mode to erase and write
    EECR &= ~( ( 1 << EEPM0 ) | ( 1 << EEPM1 ) );

    // check if erasing
    if( data == 0xff ){
        
        // set for erase only
        EECR |= ( 1 << EEPM0 );
    }

	EECR |= ( 1 << EEMPE ); // set master programming enable
	EECR |= ( 1 << EEPE ); // set programming enable
	
	END_ATOMIC;
    
    #endif
}

void ee_v_write_block( uint16_t address, const uint8_t *data, uint16_t len ){
    
    while( len > 0 ){
        
        ee_v_write_byte_blocking( address, *data );
        
        data++;
        len--;
        address++;
        
        wdt_reset();
    }
}

uint8_t ee_u8_read_byte( uint16_t address ){
	
    #ifdef __SIM__
    
    return array[address];
    
    #else
    
    // busy wait
    while( ee_b_busy() );

    ATOMIC;
	
	EEAR = address; // set address
	
	EECR |= ( 1 << EERE ); // set read enable
    
    END_ATOMIC;
	
	return EEDR; // return byte
    
    #endif
}

void ee_v_read_block( uint16_t address, uint8_t *data, uint16_t length ){
	
    // busy wait
    while( ee_b_busy() );

	for( uint16_t i = 0; i < length; i++ ){
		
        #ifdef __SIM__
    
        *data = array[address];
        
        #else

        ATOMIC;
        
        EEAR = address; // set address
        
        EECR |= ( 1 << EERE ); // set read enable
        
        END_ATOMIC;
        
        *data = EEDR; // return byte
        
        #endif

		data++;
		address++;
	}
}

