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
 
#ifndef _AES_128RFA1_H
#define _AES_128RFA1_H

#define AES_BLOCK_SIZE 16


uint8_t aes_u8_reset( void );
void aes_v_set_key( const uint8_t key[AES_BLOCK_SIZE] );

void aes_v_encrypt( const uint8_t in[AES_BLOCK_SIZE],
                    uint8_t out[AES_BLOCK_SIZE],
                    const uint8_t key[AES_BLOCK_SIZE],
                    uint8_t o_key[AES_BLOCK_SIZE] );

void aes_v_decrypt( const uint8_t in[AES_BLOCK_SIZE],
                    uint8_t out[AES_BLOCK_SIZE],
                    const uint8_t key[AES_BLOCK_SIZE],
                    uint8_t o_key[AES_BLOCK_SIZE] );

#endif

