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

#include "config.h"
#include "crypt.h"
#include "at86rf230.h"
#include "wcom_neighbors.h"

#include "wcom_mac_sec.h"



static uint8_t session_iv[CRYPT_KEY_SIZE];
static uint32_t replay_counter;

void wcom_mac_sec_v_init( void ){
    
    // init session initialization vector
    for( uint8_t i = 0; i < sizeof(session_iv); i++ ){
        
        session_iv[i] = rf_u8_get_random();
    }

    // init replay counter.
    // 0 means no session, so init to 1
    replay_counter = 1;
}

void wcom_mac_sec_v_compute_auth_tag( const uint8_t iv[CRYPT_KEY_SIZE],
                                      const void *data,
                                      uint16_t len,
                                      uint8_t auth_tag[CRYPT_AUTH_TAG_SIZE] ){
    // NOTE:
    // Variable length arrays seem to have problems... need to change this!
    uint8_t buf[CRYPT_KEY_SIZE + len];

    // copy data to buffer
    memcpy( buf, iv, CRYPT_KEY_SIZE );
    memcpy( &buf[CRYPT_KEY_SIZE], data, len );
    
    // get key
    uint8_t key[CRYPT_KEY_SIZE];
    cfg_v_get_security_key( CFG_KEY_WCOM_AUTH, key );
    
    // compute auth tag
    crypt_v_aes_xcbc_mac_96( key, buf, CRYPT_KEY_SIZE + len, auth_tag );
}

uint32_t wcom_mac_sec_u32_get_replay_counter( void ){
    
    replay_counter++;

    return replay_counter;
}

void wcom_mac_sec_v_get_session_iv( uint8_t iv[CRYPT_KEY_SIZE] ){
    
    memcpy( iv, session_iv, CRYPT_KEY_SIZE );
}

void wcom_mac_sec_v_sign_message( const void *data,
                                  uint16_t len,
                                  uint8_t auth_tag[CRYPT_AUTH_TAG_SIZE] ){
    
    wcom_mac_sec_v_compute_auth_tag( session_iv, data, len, auth_tag );
}


int8_t wcom_mac_sec_i8_verify_message( uint16_t short_addr,
                                       uint32_t counter,
                                       const void *data,
                                       uint16_t len,
                                       uint8_t auth_tag[CRYPT_AUTH_TAG_SIZE] ){
    
    wcom_neighbor_t *neighbor = wcom_neighbors_p_get_neighbor( short_addr );
    
    // check if neighbor was found
    if( neighbor == 0 ){
        
        return -1;
    }
    
    // check replay
    if( counter <= neighbor->replay_counter ){
        
        return -2;
    }

    // compute auth tag for message
    uint8_t computed_auth_tag[CRYPT_AUTH_TAG_SIZE];
    wcom_mac_sec_v_compute_auth_tag( neighbor->iv, 
                                     data, 
                                     len, 
                                     computed_auth_tag );
    
    // compare tags
    if( memcmp( computed_auth_tag, auth_tag, CRYPT_AUTH_TAG_SIZE ) != 0 ){
        
        return -3;
    }

    // set replay counter
    neighbor->replay_counter = counter;

    return 0;
}


