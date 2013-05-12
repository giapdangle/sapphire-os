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

#ifndef _WCOM_MAC_SEC_H
#define _WCOM_MAC_SEC_H

#include "crypt.h"

void wcom_mac_sec_v_init( void );

void wcom_mac_sec_v_compute_auth_tag( const uint8_t iv[CRYPT_KEY_SIZE],
                                      const void *data,
                                      uint16_t len,
                                      uint8_t auth_tag[CRYPT_AUTH_TAG_SIZE] );
uint32_t wcom_mac_sec_u32_get_replay_counter( void );
void wcom_mac_sec_v_get_session_iv( uint8_t iv[CRYPT_KEY_SIZE] );
void wcom_mac_sec_v_sign_message( const void *data,
                                  uint16_t len,
                                  uint8_t auth_tag[CRYPT_AUTH_TAG_SIZE] );
int8_t wcom_mac_sec_i8_verify_message( uint16_t short_addr,
                                       uint32_t counter,
                                       const void *data,
                                       uint16_t len,
                                       uint8_t auth_tag[CRYPT_AUTH_TAG_SIZE] );


#endif

