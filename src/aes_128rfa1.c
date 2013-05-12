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

#include "aes_128rfa1.h"

/*

Generic, sychronous interface to the hardware AES engine.

Future improvements:
Ability to process in a background thread, running the crypo engine parallel to the CPU

Use CBC mode in hardware:
Current, we do the CBC XOR in software in the crypt module.  If we tweak those routines,
we can use the hardware module to do the XOR to eek a bit more speed out of the module.


*/

uint8_t aes_u8_reset( void ){
     
    return AES_CTRL;
}

void aes_v_set_key( const uint8_t key[AES_BLOCK_SIZE] ){
    
    for( uint8_t i = 0; i < AES_BLOCK_SIZE; i++ ){
        
        AES_KEY = key[i];
    }
}


void aes_v_encrypt( const uint8_t in[AES_BLOCK_SIZE],
                    uint8_t out[AES_BLOCK_SIZE],
                    const uint8_t key[AES_BLOCK_SIZE],
                    uint8_t o_key[AES_BLOCK_SIZE] ){
    
    // init key
    aes_v_set_key( key );
    
    // write data to AES state
    for( uint8_t i = 0; i < AES_BLOCK_SIZE; i++ ){
        
        AES_STATE = in[i];
    }

    // start encrypt cycle, EBC mode
    AES_CTRL = ( 1 << AES_REQUEST ) | ( 0 << AES_MODE ) | ( 0 << AES_DIR );
    
    // wait until finished
    while( ( AES_STATUS & ( 1 << AES_DONE ) ) == 0 );
    
    // read data from AES state
    for( uint8_t i = 0; i < AES_BLOCK_SIZE; i++ ){
        
        out[i] = AES_STATE;
    }

    // read last round key
    for( uint8_t i = 0; i < AES_BLOCK_SIZE; i++ ){
        
        o_key[i] = AES_KEY;
    }
}


void aes_v_decrypt( const uint8_t in[AES_BLOCK_SIZE],
                    uint8_t out[AES_BLOCK_SIZE],
                    const uint8_t key[AES_BLOCK_SIZE],
                    uint8_t o_key[AES_BLOCK_SIZE] ){
    
    // init key
    aes_v_set_key( key );
    
    // write data to AES state
    for( uint8_t i = 0; i < AES_BLOCK_SIZE; i++ ){
        
        AES_STATE = in[i];
    }

    // start decrypt cycle, EBC mode
    AES_CTRL = ( 1 << AES_REQUEST ) | ( 0 << AES_MODE ) | ( 1 << AES_DIR );
    
    // wait until finished
    while( ( AES_STATUS & ( 1 << AES_DONE ) ) == 0 );
    
    // read data from AES state
    for( uint8_t i = 0; i < AES_BLOCK_SIZE; i++ ){
        
        out[i] = AES_STATE;
    }

    // read last round key
    for( uint8_t i = 0; i < AES_BLOCK_SIZE; i++ ){
        
        o_key[i] = AES_KEY;
    }
}


